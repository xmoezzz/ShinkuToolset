//使用Lua 5.3 作为目标语言

/*
基本上就是一个精简版的PE
opcode 
0x00   NOP 没有用于Dispatch的Handle，在主循环被处理掉了
0x01   InitStack 函数的开始
       u8 ArgsCount, u8 LocalVarCount
0x02   Call 
       DWORD offset
0x03   CallSystem
       WORD index（这个记录在hcb尾部的输入表中）
0x04   return void(注意，0x04会被用于标记stack的尾部，一个函数的结尾可能是0x05 0x04 或者 0x04 0x04)
0x05   return EAX
0x06   JMP 
       DWORD offset
0x07   JZ (JMP if zero)
       DWORD offset
0x08   Push 0
0x09   Push 1
0x0A   PushInt32
       DWORD(Imm)
0x0B   PushInt16
       WORD(Imm)
0x0C   PushInt8
       u8(Imm)
0x0D   PushFloat32
       float(Imm)
0x0E   PushString
       u8 len, const char* pBuffer (注意，len包含了结尾0)
0x0F   Push Global
       WORD(Imm)  (全局数组)
0x10   PushStack
       u8 index  （Local的大小为0x00 - 0xFE，实际上0xFF在原始vm中作为EAX使用）
0x11   PushMap1
       WORD key
       假设stack.top为var_a
       那么看起来就像Map[key][var_a]
0x12   PushMap2
       u8 key
       和0x11同理，一个是全局记录，另一个存档相关的
0x13   PushTop 很傻逼的一条指令，至今为止，F社自家的编译器都不会生成这个代码
0x14   Push EAX
0x15   Pop Global
       WORD index Glabal[index] = stack.top();stack.pop()
0x16   PopStack
       u8 index  等同于local_var_index = stack.top(); stack.pop()
0x17   Pop Map1
       WORD index   同理
0x18   Pop Map2
       u8 index  同理
0x19   Negative  stack.top() = -stack.top()
0x1A   Add; Local_a=stack.pop();Local_b=stack.pop();stack.push(a+b) 只有add包含字符串连接
       后面几条都是这个鬼，略过
0x1B   Sub
0x1C   Mul
0x1D   Div
0x1E   Mod
0x1F   BitTest Local_a=stack.pop();Local_b=stack.pop();stack.push(a&(1<<b)?1:0)
0x20   Condition Add 根据stack.top来判断 然后pop掉
0x21   Condition Or 同上
0x22   SetE  读取顺序和Add系列指令相同
0x23   SetNE
0x24   SetG
0x25   SetLE
0x26   SetL
0x27   SetGE

目前（指白永正式版），已经有148条SystemCall
解析进度100%，解析文档懒得写
*/

#include "stdafx.h"
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <map>
#include <list>
#include <vector>
#include <stack>
#include <string>
#include <iostream>
#include <utility> 

#define MAX_SYSTEMCALL 150

using std::map;
using std::stack;
using std::string;
using std::vector;
using std::pair;

typedef unsigned char u8;

static char Buffer[4096] = {0};

#define Clear memset(Buffer, 0, sizeof(Buffer));

#define SJISConv(s) MultiByteToUTF8(s, Buffer, 4096, 932)
#define CHSConv(s) MultiByteToUTF8(s, Buffer, 4096, 936)

int MultiByteToUTF8(const char * lpGBKStr, const char * lpUTF8Str, int nUTF8StrLen, ULONG cp = 932)
{
	wchar_t * lpUnicodeStr = NULL;
	int nRetLen = 0;

	if (!lpGBKStr)
		return 0;

	nRetLen = ::MultiByteToWideChar(cp, 0, (char *)lpGBKStr, -1, NULL, NULL);
	lpUnicodeStr = new WCHAR[nRetLen + 1];
	nRetLen = ::MultiByteToWideChar(cp, 0, (char *)lpGBKStr, -1, lpUnicodeStr, nRetLen);
	if (!nRetLen)
		return 0;

	nRetLen = ::WideCharToMultiByte(CP_UTF8, 0, lpUnicodeStr, -1, NULL, 0, NULL, NULL);

	if (!lpUTF8Str)
	{
		if (lpUnicodeStr)
			delete[]lpUnicodeStr;
		return nRetLen;
	}

	if (nUTF8StrLen < nRetLen)
	{
		if (lpUnicodeStr)
			delete[]lpUnicodeStr;
		return 0;
	}
	nRetLen = ::WideCharToMultiByte(CP_UTF8, 0, lpUnicodeStr, -1, (char *)lpUTF8Str, nUTF8StrLen, NULL, NULL);

	if (lpUnicodeStr)
		delete[]lpUnicodeStr;

	return nRetLen;
}

static int TabCount = 0;
#define IncTab TabCount++;
#define DecTab if(TabCount >= 1) TabCount--;

int PrintTab(FILE* fin, unsigned int count)
{
	if (!fin)
		return -1;
	while (count--)
	{
		fprintf(fin, "\t");
	}
	return 0;
}

void Write(FILE* out, string& str)
{
	if (!out)
		return;

	PrintTab(out, TabCount);
	fprintf(out, str.c_str());
}

void WriteDirect(FILE* out, string& str)
{
	if (!out)
		return;

	fprintf(out, str.c_str());
}

void FormatString(string& dest, const char* fmt, string& str1, string& str2)
{
	static char StringBuffer[2048];
	memset(StringBuffer, 0, sizeof(StringBuffer));
	sprintf(StringBuffer, fmt, str1.c_str(), str2.c_str());
	dest = StringBuffer;
}

class userFunctable
{
public:
	DWORD userOffset;
	DWORD userVarCount;
	DWORD popVarCount;
	userFunctable()
	{
		userOffset = 0;
		userVarCount = 0;
		popVarCount = 0;
	}
};

typedef enum vmTYPE
{
	BOOL0 = 0,
	BOOL1 = 1,
	pINT = 2,
	pFLOAT = 3,
	STRING = 4,
};

struct VarInfo
{
	//处理lua字符串的连接
	vmTYPE       type;
	__int32      value;
	string         str;
	bool         isLocal;
	u8           LocalIndex;
	bool         isParsed;
	bool         is_op;

	VarInfo();
	~VarInfo();
};


VarInfo::VarInfo()
{
	str.clear();
	value = 0;
	isLocal = false;
	LocalIndex = 0;
	isParsed = false;
	is_op = false;
}

VarInfo::~VarInfo()
{
	//str = NULL;
	str.clear();
	value = 0;
}


struct userFunctionInfo
{
	int local;
	int funvar;
	bool ret_temp;
	userFunctionInfo()
	{
		local = 0;
		funvar = 0;
		ret_temp = false;
	}
	~userFunctionInfo(){}
};

class sysFuncPair
{
public:
	WORD  order;
	WORD  sysVarCount;
	char  sysFuncName[256];
	bool ret_temp;
	bool ret_string;
	sysFuncPair() : ret_string(false)
	{
		memset(sysFuncName, 0, sizeof(sysFuncName));
		ret_temp = false;
	}
};

typedef struct vmImportEntry
{
	u8    varCount;
	u8    functionLen;
	char* funtionName;
};


static ULONG CodePage = 932;

void EntryCode(FILE* fin, unsigned short w, unsigned short h, string& title, unsigned int entry_proc, int cp);
void InitCode(FILE* fin);
void LuaAddCode(FILE* fin);


static bool First = true;


/**********************************************/
int Main(int argc, char **argv)
{
	fprintf(stdout, "[FVP2Lua] coded by X'moe\n");
	fprintf(stdout, "xmoe.project@gmail.com\n");
	if (argc != 2 && argc != 3)
	{
		return -1;
	}
	FILE *fin = fopen(argv[1], "rb");
	if (fin == NULL)
	{
		printf("Usage : %s <infile>\n", argv[0]);
		return 0;
	}
	if (argc == 3)
	{
		if (!stricmp(argv[2], "chs"))
		{
			CodePage = 936;
		}
	}

	FILE *stackinfo = NULL;
	stackinfo = fopen((string(argv[1]) + ".lua").c_str(), "wb");
	if (stackinfo == NULL)
	{
		fprintf(stderr, "Failed to open output file\n");
		return -1;
	}


	fseek(fin, 0, SEEK_END);
	unsigned long FileSize = ftell(fin);
	rewind(fin);
	char *oFile = new char[FileSize];
	memset(oFile, 0, sizeof(oFile));
	fread(oFile, FileSize, 1, fin);
	fclose(fin);

	map<DWORD, bool> JMPTable;
	map<DWORD, bool>::iterator it;
	DWORD JUMPADD;
	DWORD JZADD;

	VarInfo EAX;

	map<WORD, VarInfo> GlobalVarList;
	map<WORD, VarInfo>::iterator GlobalVarListPointer;
	typedef pair<WORD, VarInfo> GlobalVarListInsert;


	map<u8, VarInfo> LocalVarList;
	map<u8, VarInfo>::iterator LocalVarListPointer;
	typedef pair<u8, VarInfo> LocalVarListInsert;



	map<DWORD, userFunctionInfo> userFunc;
	map<DWORD, userFunctionInfo>::iterator userFuncPointer;
	typedef pair<DWORD, userFunctionInfo> userFuncInsert;

	/************************************/
	stack<VarInfo> temp;

	char tempWord[4096];
	memset(tempWord, 0, sizeof(tempWord));

	char stackname[256];
	DWORD iPos = 0; //EIP

	DWORD RealStackADD;
	memcpy(&RealStackADD, oFile, 4);

	iPos = RealStackADD;
	
	iPos += 4;

	static unsigned int entry_proc = *(unsigned int*)(oFile + iPos);
	iPos += 2;
	iPos += 2;

	static unsigned short game_mode = *(unsigned short*)(oFile + iPos);
	iPos += 2;

	u8  pTitleLen = *(oFile + iPos);
	iPos++;

	string title((oFile + iPos));
	iPos += pTitleLen;


	WORD FunctionCount;
	memcpy(&FunctionCount, (oFile + iPos), 2);
	iPos += 2;

	printf("[FVP2Lua] Found [%d] system functions.\n", FunctionCount);
	sysFuncPair sysFuncTable[MAX_SYSTEMCALL];

	int FuncIndex = 0;
	while (FunctionCount--)
	{
		if (FuncIndex == 3 || FuncIndex == 17 || FuncIndex == 24 || FuncIndex == 25 || FuncIndex == 28 || FuncIndex == 29 ||
			FuncIndex == 30 || FuncIndex == 31 || FuncIndex == 33 || FuncIndex == 34 || FuncIndex == 35 || FuncIndex == 36 ||
			FuncIndex == 44 || FuncIndex == 47 || FuncIndex == 50 || FuncIndex == 53 || FuncIndex == 56 || FuncIndex == 59 ||
			FuncIndex == 62 || FuncIndex == 69 || FuncIndex == 76 || FuncIndex == 91 || FuncIndex == 92 || FuncIndex == 94 ||
			FuncIndex == 110 || FuncIndex == 111 || FuncIndex == 127 || FuncIndex == 134 || FuncIndex == 141 || FuncIndex == 143)
		{
			sysFuncTable[FuncIndex].ret_temp = true;
			//没有用了，通过PushEax指令进行判断
		}
		vmImportEntry _vmImportEntry;
		_vmImportEntry.varCount = *(oFile + iPos);
		sysFuncTable[FuncIndex].sysVarCount = (u8)*(oFile + iPos);
		iPos++;
		_vmImportEntry.functionLen = *(oFile + iPos);
		int e = 0;
		iPos++;

		while (e < _vmImportEntry.functionLen - 1)
		{
			sysFuncTable[FuncIndex].sysFuncName[e] = *(oFile + iPos + e);
			e++;
		}
		iPos += _vmImportEntry.functionLen;
		FuncIndex++;
	}


	printf("[FVP2Lua] Reading user call(s)...\n");
	DWORD userCallCount = 0;
	iPos = 4;

	DWORD TempADD = 0;
	while (iPos < RealStackADD)
	{
		if (*(oFile + iPos) == 0x0E)
		{
			iPos++;
			DWORD pLen = (u8)*(oFile + iPos);
			iPos++;
			iPos += pLen;
		}
		else if (*(oFile + iPos) == 0x06)//加入JMP记录 
		{
			iPos++;
			memcpy(&JUMPADD, (oFile + iPos), 4);
			bool addFlag = true; //检查是否重复 
			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (it->first == JUMPADD)
				{
					addFlag = false;
					break;
				}
			}
			*/
			JMPTable.insert(std::make_pair(JUMPADD, false));
			iPos += 4;
		}
		else if (*(oFile + iPos) == 0x07)
		{
			iPos++;
			bool jzAddFlag = true;
			memcpy(&JZADD, (oFile + iPos), 4);
			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (it->first && (it->Printed == false) == JZADD)
				{
					jzAddFlag = false;
					break;
				}
			}*/
			JMPTable.insert(std::make_pair(JZADD, false));
			iPos += 4;
		}
		else if (*(oFile + iPos) == 0x02 || *(oFile + iPos) == 0x0D || *(oFile + iPos) == 0x0A)
		{
			iPos += 5;
		}
		else if (*(oFile + iPos) == 0x03 || *(oFile + iPos) == 0x0B || *(oFile + iPos) == 0x0F ||
			*(oFile + iPos) == 0x11 || *(oFile + iPos) == 0x15 || *(oFile + iPos) == 0x17)
		{
			iPos += 3;
		}
		else if (*(oFile + iPos) == 0x08 ||
			*(oFile + iPos) == 0x09 || *(oFile + iPos) == 0x13 || *(oFile + iPos) == 0x14 ||
			*(oFile + iPos) == 0x19 || *(oFile + iPos) == 0x1A || *(oFile + iPos) == 0x1B ||
			*(oFile + iPos) == 0x1C || *(oFile + iPos) == 0x1D || *(oFile + iPos) == 0x1E ||
			*(oFile + iPos) == 0x1F || *(oFile + iPos) == 0x20 || *(oFile + iPos) == 0x21 ||
			*(oFile + iPos) == 0x22 || *(oFile + iPos) == 0x23 || *(oFile + iPos) == 0x24 ||
			*(oFile + iPos) == 0x25 || *(oFile + iPos) == 0x26 || *(oFile + iPos) == 0x27)
		{
			iPos++;
		}
		else if (*(oFile + iPos) == 0x04)//void
		{
			userFuncPointer = userFunc.find(TempADD);
			userFuncPointer->second.ret_temp = false;
			iPos++;

		}
		else if (*(oFile + iPos) == 0x05)
		{
			userFuncPointer = userFunc.find(TempADD);
			userFuncPointer->second.ret_temp = true;
			iPos++;
		}
		else if (*(oFile + iPos) == 0x0C || *(oFile + iPos) == 0x10 || *(oFile + iPos) == 0x12 ||
			*(oFile + iPos) == 0x16 || *(oFile + iPos) == 0x18)
		{
			iPos += 2;
		}
		else if (*(oFile + iPos) == 0x01)
		{
			TempADD = iPos;
			userFunctionInfo ai;
			ai.funvar = (u8)*(oFile + iPos + 1);
			ai.local = (u8)*(oFile + iPos + 2);
			userFuncInsert ak;
			ak.first = iPos;
			ak.second = ai;
			userFunc.insert(ak);
			iPos += 3;
			userCallCount++;
		}
		else
		{
			iPos++;
		}
	}
	printf("[FVP2Lua] Found [%08x] user function(s).\n", userCallCount);


	static bool NextTempReturn = false;
	LuaAddCode(stackinfo);
	InitCode(stackinfo);
	/*****************************************************/
	iPos = 4;
	while (iPos < RealStackADD)
	{
		if (*(oFile + iPos) == 0x0E)
		{
			char tmp[256] = { 0 };
			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (iPos == it->add && (it->Printed == false))
				{
					sprintf(tmp, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(tmp));
					break;
				}
			}
			*/

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			if ((unsigned char)*(oFile + iPos + 1) == 1)
			{
				VarInfo var;
				var.type = STRING;
				var.str += "\"\"";
				EAX.str = var.str;
				temp.push(var);
				iPos += 3;
			}
			else
			{
				unsigned char PopStrLen = *(oFile + iPos + 1);
				iPos += 2;
				VarInfo var;
				var.isParsed = true;
				var.type = STRING;

				var.str += '\"';

				Clear
				if (CodePage == 932)
				{
					SJISConv((oFile + iPos));
				}
				else
				{
					CHSConv((oFile + iPos));
				}
				var.str += Buffer;
				var.str += '\"';
				iPos += PopStrLen;
				EAX.str = var.str;
				temp.push(var);
			}
		}
		else if (*(oFile + iPos) == 0x06)
		{//jump : DWORD offset
			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}
			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (iPos == it->add && (it->Printed == false))
				{
					it->Printed = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
					break;
				}
			}*/

			char MakeString[128] = { 0 };
			sprintf(MakeString, "goto hcb_", iPos);
			Write(stackinfo, string(MakeString));

			iPos++;
			memcpy(&JUMPADD, (oFile + iPos), 4);
			iPos += 4;

			memset(MakeString, 0, sizeof(MakeString));
			sprintf(MakeString, "%08x;\r\n", JUMPADD);
			WriteDirect(stackinfo, string(MakeString));
		}
		else if (*(oFile + iPos) == 0x02)
		{ //call : DWORD Offset

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (iPos == it->add && (it->Printed == false))
				{
					it->Printed = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
					break;
				}
			}
			*/

			iPos++;
			DWORD CALLADD;
			//printf("Running\n");
			memcpy(&CALLADD, (oFile + iPos), 4);
			iPos += 4;
			userFuncPointer = userFunc.find(CALLADD);
			if (userFuncPointer == userFunc.end())
			{
				printf("An unknown function found!\n");
				return 0;
			}

			int key = userFuncPointer->second.funvar;

			char *call_t = new char[2048];
			memset(call_t, 0, 2048);

			//PushTemp 0x14
			//只是通过扫描Last Chunk是不准确的
			//!userFuncPointer->second.ret_temp
			//*(u8*)(oFile + iPos) != 0x14
			if (*(u8*)(oFile + iPos) != 0x14)
			{
				char tmp[256] = { 0 };
				sprintf(tmp, "funtion_%08x(", CALLADD);
				Write(stackinfo, string(tmp));
				while (key--)
				{
					VarInfo var9 = temp.top();
					memset(tmp, 0, sizeof(tmp));
					sprintf(tmp, " %s%s", var9.str.c_str(), key == 0 ? "" : ",");
					WriteDirect(stackinfo, string(tmp));
					temp.pop();
				}
				//memset(tmp, 0, sizeof(tmp));
				WriteDirect(stackinfo, string(");\r\n"));
			}
			else
			{
				char *t_c = new char[1024];
				VarInfo a;
				EAX.str.clear();
				char tmp[256] = { 0 };
				sprintf(tmp, "funtion_%08x(", CALLADD);
				EAX.str = tmp;
				while (key--)
				{
					a = temp.top();
					memset(t_c, 0, 1024);
					sprintf(t_c, " %s%s", a.str.c_str(), key == 0 ? "" : ",");
					EAX.str += t_c;
					temp.pop();
				}
				EAX.str += ")";
				delete[] t_c;
				EAX.type = STRING;
			}
			delete[] call_t;
		}
		else if (*(oFile + iPos) == 0x03)
		{ //sysCall : WORD Offset
			//通过return指令判断是否需要返回 还是直接调用形式
			char tmp[256] = { 0 };

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (iPos == it->add && (it->Printed == false))
				{
					it->Printed = true;
					sprintf(tmp, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(tmp));
					break;
				}
			}
			*/

			iPos++;
			WORD CALLSYSADD;
			memcpy(&CALLSYSADD, (oFile + iPos), 2);
			iPos += 2;
			//fprintf(stackinfo,"%s (",sysFuncTable[CALLSYSADD].sysFuncName);
			bool ret_temp = false;
			if (*(unsigned char*)(oFile + iPos) == 0x14)
			{
				ret_temp = true;
			}
			int key = sysFuncTable[CALLSYSADD].sysVarCount;

			if (!ret_temp)
			{
				char tmp[256] = { 0 };
				sprintf(tmp, "%s (", sysFuncTable[CALLSYSADD].sysFuncName);
				Write(stackinfo, string(tmp));
				while (key--)
				{
					VarInfo var1 = temp.top();
					sprintf(tmp, " %s%s", var1.str.c_str(), key == 0 ? "" : ",");
					WriteDirect(stackinfo, string(tmp));
					temp.pop();
				}
				WriteDirect(stackinfo, string(");\r\n"));
			}

			else
			{
				if (EAX.str.length())
				{
					EAX.str.clear();
				}
				char *ee = new char[1024];
				memset(ee, 0, 1024);
				sprintf(ee, "%s (", sysFuncTable[CALLSYSADD].sysFuncName);
				EAX.str = ee;
				memset(ee, 0, 1024);
				while (key--)
				{
					VarInfo var1 = temp.top();
					sprintf(ee, " %s%s", var1.str.c_str(), key == 0 ? "" : ",");
					EAX.str += ee;
					memset(ee, 0, sizeof(ee));
					temp.pop();
				}
				EAX.str += ")";
				EAX.type = STRING;
				delete[] ee;
			}
		}
		else if (*(oFile + iPos) == 0x01)
		{ //initStack,len = 2
			if (!First)
			{
				DecTab
				WriteDirect(stackinfo, string("end\r\n"));
				WriteDirect(stackinfo, string("-----------End of Funtion---------\r\n"));
			}
			First = false;
			while (temp.size())
			{
				temp.pop();
			}
			iPos++;
			unsigned char num = *(oFile + iPos);
			unsigned char local = *(oFile + iPos + 1);

			iPos += 2;
			char tmp[256] = { 0 };
			sprintf(tmp, "Decompiled function %08x", iPos - 3);
			WriteDirect(stackinfo, string("\r\n--------"));
			WriteDirect(stackinfo, string(tmp));
			WriteDirect(stackinfo, string("--------\r\n"));
			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "function function_%08x ( ", iPos - 3);
			Write(stackinfo, string(tmp));
			if (num != 0)
			{
				for (int var = 1; var <= num; var++)
				{
					memset(tmp, 0, sizeof(tmp));
					sprintf(tmp, "var%02x%s", 0xff - var, var == num ? "" : ",");
					WriteDirect(stackinfo, string(tmp));
				}
				WriteDirect(stackinfo, string(" )\r\n"));
			}
			else
			{
				WriteDirect(stackinfo, string(")\r\n"));
			}
			IncTab
			for (unsigned int index = 0; index < local; index++)
			{
				memset(tmp, 0, sizeof(tmp));
				sprintf(tmp, "local var%02x;\r\n", index);
				Write(stackinfo, string(tmp));
			}
		}
		else if (*(oFile + iPos) == 0x04)
		{ //return(void),len = 0
			char tmp[256] = { 0 };

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (iPos == it->add && (it->Printed == false))
				{
					it->Printed = true;
					sprintf(tmp, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(tmp));
					break;
				}
			}
			*/

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if ((iPos + 1) == it->add && (it->Printed == false))
				{
					it->Printed = true;
					sprintf(tmp, "::hcb_%08x::\r\n", iPos + 1);
					Write(stackinfo, string(tmp));
					break;
				}
			}*/

			if (*(u8*)(oFile + iPos))
			{
				it = JMPTable.find(iPos + 1);
				if (it != JMPTable.end())
				{
					if (!it->second)
					{
						it->second = true;
						char MakeString[128] = { 0 };
						sprintf(MakeString, "::hcb_%08x::\r\n", iPos + 1);
						Write(stackinfo, string(MakeString));
					}
				}
			}

			iPos++;
			//DecTab
			if (NextTempReturn)
			{
				Write(stackinfo, string("else return end\r\n"));
			}
			NextTempReturn = false;
			if (*(unsigned char*)(oFile + iPos) == 0x04)
			{
				iPos++;
			}
		}
		else if (*(oFile + iPos) == 0x05)
		{ //return with values --> scriptObject.temp=pop;ret
			memset(stackname, 0, sizeof(stackname));
			char tmp[256] = { 0 };

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (iPos == it->add && (it->Printed == false))
				{
					it->Printed = true;
					sprintf(tmp, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(tmp));
					break;
				}
			}
			*/

			
			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if ((iPos + 1) == it->add && (it->Printed == false))
				{
					it->Printed = true;
					sprintf(tmp, "::hcb_%08x::\r\n", iPos + 1);
					Write(stackinfo, string(tmp));
					break;
				}
			}*/
			if (*(u8*)(oFile + iPos + 1) == 0x04)
			{
				it = JMPTable.find(iPos + 1);
				if (it != JMPTable.end())
				{
					if (!it->second)
					{
						it->second = true;
						char MakeString[128] = { 0 };
						sprintf(MakeString, "::hcb_%08x::\r\n", iPos + 1);
						Write(stackinfo, string(MakeString));
					}
				}
			}

			iPos++;
			//Never
			if (*(unsigned char*)(oFile + iPos) == 0x05)
			{
				if (NextTempReturn)
				{
					WriteDirect(stackinfo, string("else "));
				}
				WriteDirect(stackinfo, string("return "));
				sprintf(tmp, "%s\r\n", EAX.str.c_str());
				WriteDirect(stackinfo, string(tmp));
				memset(tmp, 0, sizeof(tmp));
				//DecTab
				if (NextTempReturn)
				{
					WriteDirect(stackinfo, string(" end\r\n"));
				}
			}
			else
			{
				if (NextTempReturn)
				{
					Write(stackinfo, string("else return "));
					WriteDirect(stackinfo, EAX.str);
					WriteDirect(stackinfo, string(" end\r\n"));
				}
				else
				{
					//函数最后一个返回
					Write(stackinfo, string("return "));
					WriteDirect(stackinfo, EAX.str);
					WriteDirect(stackinfo, string("\r\n"));
				}
			}
			NextTempReturn = false;
			if (*(unsigned char*)(oFile + iPos) == 0x05 || *(unsigned char*)(oFile + iPos) == 0x04)
			{
				iPos++;
			}
		}
		else if (*(oFile + iPos) == 0x07)
		{ //jz (DWORD)-->???????0????? s--
			char tmp[2048] = { 0 };

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (iPos == it->add && (it->Printed == false))
				{
					it->Printed = true;
					sprintf(tmp, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(tmp));
					break;
				}
			}
			*/

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			VarInfo s2 = temp.top();
			temp.pop();

			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "if(false == %s) then goto ", s2.str.c_str());
			Write(stackinfo, string(tmp));
			
			iPos++;
			memcpy(&JZADD, (oFile + iPos), 4);
			iPos += 4;

			
			memset(tmp, 0, sizeof(tmp));
			if (*(unsigned char*)(oFile + iPos) == 0x04 || *(unsigned char*)(oFile + iPos) == 0x05)
			{
				sprintf(tmp, "hcb_%08x\r\n", JZADD);
				NextTempReturn = true;
			}
			else if (*(unsigned char*)(oFile + iPos) == 0x0E || *(unsigned char*)(oFile + iPos) == 0x08 ||
				*(unsigned char*)(oFile + iPos) == 0x09 || *(unsigned char*)(oFile + iPos) == 0x0A ||
				*(unsigned char*)(oFile + iPos) == 0x0B || *(unsigned char*)(oFile + iPos) == 0x0C ||
				*(unsigned char*)(oFile + iPos) == 0x0D || *(unsigned char*)(oFile + iPos) == 0x0F ||
				*(unsigned char*)(oFile + iPos) == 0x10 || *(unsigned char*)(oFile + iPos) == 0x11 ||
				*(unsigned char*)(oFile + iPos) == 0x12)
			{
				unsigned char *pBuffer = (unsigned char*)(oFile + iPos);
				//下一个指令是一个Push
				//再下一个才是0x05 的return
				//注意：Push的时候，需要设置EAX
				unsigned int offset = 0;
				if (*(u8*)pBuffer == 0x08 || *(u8*)pBuffer == 0x09)
				{
					offset++;
				}
				else if (*(u8*)pBuffer == 0x0A)
				{
					offset++;
					offset += 4;
				}
				else if (*(u8*)pBuffer == 0x0B)
				{
					offset++;
					offset += 2;
				}
				else if (*(u8*)pBuffer == 0x0C)
				{
					offset++;
					offset++;
				}
				else if (*(u8*)pBuffer == 0x0D)
				{
					offset++;
					offset += 4;
				}
				else if (*(u8*)pBuffer == 0x0F)
				{
					offset++;
					offset += 2;
				}
				else if (*(u8*)pBuffer == 0x10)
				{
					offset++;
					offset++;
				}
				else if (*(u8*)pBuffer == 0x11)
				{
					offset += 3;
				}
				else if (*(u8*)pBuffer == 0x12)
				{
					offset += 2;
				}
				else if (*(u8*)pBuffer == 0x0E)
				{
					offset++;
					u8 uLen = *(u8*)(pBuffer + offset);
					offset++;
					offset += uLen;
				}


				if (*(u8*)(pBuffer + offset) == 0x05)
				{
					sprintf(tmp, "hcb_%08x\r\n", JZADD);
					NextTempReturn = true;
				}
				else sprintf(tmp, "hcb_%08x end;\r\n", JZADD);
			}
			else sprintf(tmp, "hcb_%08x end;\r\n", JZADD);
			WriteDirect(stackinfo, string(tmp));
		}
		else if (*(oFile + iPos) == 0x08)
		{//push0()
			char tmp[256] = { 0 };

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (iPos == it->add && (it->Printed == false))
				{
					it->Printed = true;
					sprintf(tmp, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(tmp));
					break;
				}
			}
			*/

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			VarInfo var;
			var.type = STRING;
			var.str =  "false";
			EAX.str = "false";
			temp.push(var);
			iPos++;
		}
		else if (*(oFile + iPos) == 0x09)
		{//push1()

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				char tmp[256] = { 0 };
				if (iPos == it->add && (it->Printed == false))
				{
					it->Printed = true;
					sprintf(tmp, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(tmp));
					break;
				}
			}
			*/


			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			VarInfo var;
			var.type = STRING;
			var.str =  "true";
			EAX.str = "true";
			temp.push(var);
			iPos++;
		}
		else if (*(oFile + iPos) == 0x0A)
		{//PushInt32() : DWORD
			char tmp[256] = { 0 };

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (iPos == it->add && (it->Printed == false))
				{
					it->Printed = true;
					sprintf(tmp, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(tmp));
					break;
				}
			}

			*/

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			iPos++;
			DWORD INT32ADD;
			memcpy(&INT32ADD, (oFile + iPos), 4);
			iPos += 4;
			VarInfo var;
			//var.type = STRING;
			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "%d", INT32ADD);
			var.str = tmp;
			EAX.str = tmp;
			temp.push(var);
		}
		else if (*(oFile + iPos) == 0x0B)
		{//PushInt16() : WORD
			char tmp[256] = { 0 };

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (iPos == it->add && (it->Printed == false))
				{
					it->Printed = true;
					sprintf(tmp, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(tmp));
					break;
				}
			}
			*/

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			iPos++;
			WORD INT16ADD;
			memcpy(&INT16ADD, (oFile + iPos), 2);
			iPos += 2;
			VarInfo var;
			//var.type = STRING;
			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "%d", INT16ADD);
			var.str = tmp;
			EAX.str = tmp;
			temp.push(var);
		}
		else if (*(oFile + iPos) == 0x0C)
		{//PushInt8() : unsigned char
			char tmp[256] = { 0 };

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (iPos == it->add && (it->Printed == false))
				{
					it->Printed = true;
					sprintf(tmp, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(tmp));
					break;
				}
			}
			*/

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			iPos++;
			VarInfo var;
			//var.type = STRING;
			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "%d", (u8)*(oFile + iPos));
			var.str = tmp;
			EAX.str = tmp;
			temp.push(var);
			iPos++;
		}
		else if (*(oFile + iPos) == 0x0D)
		{//pushFloat(f32) : DWORD;
			char tmp[256] = { 0 };

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (iPos == it->add && (it->Printed == false))
				{
					it->Printed = true;
					sprintf(tmp, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(tmp));
					break;
				}
			}
			*/

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			iPos++;
			VarInfo var;
			DWORD Float32;
			memcpy(&Float32, (oFile + iPos), 4);
			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "%f", Float32);
			var.str = tmp;
			EAX.str = tmp;
			temp.push(var);
			iPos += 4;
		}
		else if (*(oFile + iPos) == 0x0F)
		{//pushGlobal(i16 num) : WORD;
			char tmp[256] = { 0 };

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (iPos == it->add && (it->Printed == false))
				{
					it->Printed = true;
					sprintf(tmp, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(tmp));
					break;
				}
			}
			*/

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			iPos++;

			WORD GINT16ADD;
			memcpy(&GINT16ADD, (oFile + iPos), 2);
			
			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "GlobalData[0x%04x]", GINT16ADD);
			EAX.str = tmp;
			EAX.str = tmp;
			temp.push(EAX);
			iPos += 2;
		}
		else if (*(oFile + iPos) == 0x10)
		{ //pushStack(i8 num)
			char tmp[256] = { 0 };

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (iPos == it->add && (it->Printed == false))
				{
					it->Printed = true;
					sprintf(tmp, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(tmp));
					break;
				}
			}
			*/

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			iPos++;
			VarInfo var;
			var.type = STRING;
			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "var%02x", (u8)*(oFile + iPos));
			var.str = tmp;
			EAX.str = tmp;
			temp.push(var);
			iPos++;
		}
		else if (*(oFile + iPos) == 0x11)
		{ //unknown,len = 2
			char tmp[256] = { 0 };

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (iPos == it->add && (it->Printed == false))
				{
					it->Printed = true;
					sprintf(tmp, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(tmp));
					break;
				}
			}
			*/

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			iPos++;
			WORD MMapADD;
			memcpy(&MMapADD, (oFile + iPos), 2);
			VarInfo var = temp.top();
			VarInfo var2;
			var2.type = STRING;
			var2.isParsed = true;
			temp.pop();
			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "Map2[%u][%s]", MMapADD, var.str.c_str());
			var2.str = tmp;
			temp.push(var2);
			EAX.str = tmp;
			iPos += 2;
		}
		else if (*(oFile + iPos) == 0x12)
		{ //unknown,len = 1
			char tmp[256] = { 0 };

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (iPos == it->add && (it->Printed == false))
				{
					it->Printed = true;
					sprintf(tmp, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(tmp));
					break;
				}
			}
			*/

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			VarInfo var = temp.top();
			temp.pop();
			VarInfo var2;
			var2.type = STRING;
			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "Map1[%u][%s]", (u8)*(oFile + iPos + 1), var.str.c_str());
			var2.str = tmp;
			temp.push(var2);
			EAX.str = tmp;
			iPos += 2;
		}
		else if (*(oFile + iPos) == 0x13)
		{ //pushtop(),len = 0; push top; s++
			char tmp[256] = { 0 };

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (iPos == it->add && (it->Printed == false))
				{
					it->Printed = true;
					sprintf(tmp, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(tmp));
					break;
				}
			}
			*/

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			//fprintf(stackinfo,"[%08x]PushTop ();\r\n",iPos);
			fprintf(stderr, "Error! [PushTop] was called!\n");
			iPos++;
		}
		else if (*(oFile + iPos) == 0x14)
		{ //pushtemp(),len = 0; pushscriptObject.temp; scriptObject.temp=0; s++
			char tmp[256] = { 0 };

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (iPos == it->add && (it->Printed == false))
				{
					it->Printed = true;
					sprintf(tmp, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(tmp));
					break;
				}
			}
			*/

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			temp.push(EAX);
			iPos++;
		}
		else if (*(oFile + iPos) == 0x15)
		{//PopGlobal(i16 num), len = 2;//Global[num]=pop
			char tmp[512] = { 0 };

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (iPos == it->add && (it->Printed == false))
				{
					it->Printed = true;
					sprintf(tmp, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(tmp));
					break;
				}
			}
			*/

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			iPos++;
			//printf("Running\n");
			WORD PINT16ADD;
			memcpy(&PINT16ADD, (oFile + iPos), 2);
			iPos += 2;
			//GlobalVarListPointer = GlobalVarList.find(PINT16ADD);
			EAX = temp.top();
			temp.pop();

			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "GlobalData[0x%04x] = %s;\r\n", PINT16ADD, EAX.str.c_str());
			Write(stackinfo, string(tmp));
		}
		else if (*(oFile + iPos) == 0x16)
		{//stackcpy(i8 num),len = 1; ??i10????s--
			char tmp[512] = { 0 };

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (iPos == it->add && (it->Printed == false))
				{
					it->Printed = true;
					sprintf(tmp, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(tmp));
					break;
				}
			}
			*/

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			VarInfo var = temp.top();
			temp.pop();

			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "var%02x = %s;\r\n", (u8)*(oFile + iPos + 1), var.str.c_str());
			Write(stackinfo, string(tmp));
			iPos += 2;
		}
		else if (*(oFile + iPos) == 0x17)
		{//unknown,len = 2
			char tmp[512] = { 0 };

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (iPos == it->add && (it->Printed == false))
				{
					it->Printed = true;
					sprintf(tmp, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(tmp));
					break;
				}
			}
			*/

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			iPos++;
			WORD MMapADD;
			memcpy(&MMapADD, (oFile + iPos), 2);
			VarInfo value_t = temp.top();
			temp.pop();
			VarInfo key_t = temp.top();
			temp.pop();

			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "Map2[%d][%s] = %s;\r\n", MMapADD, key_t.str.c_str(), value_t.str.c_str());
			Write(stackinfo, string(tmp));
			iPos += 2;
		}
		else if (*(oFile + iPos) == 0x18)
		{//unknown,len = 1
			char tmp[512] = { 0 };

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (iPos == it->add && (it->Printed == false))
				{
					it->Printed = true;
					sprintf(tmp, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(tmp));
					break;
				}
			}
			*/

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			iPos++;
			VarInfo value_t = temp.top();
			temp.pop();
			VarInfo key_t = temp.top();
			temp.pop();

			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "Map1[%d][%s] = %s;\r\n", (unsigned char)*(oFile + iPos), key_t.str.c_str(), value_t.str.c_str());
			Write(stackinfo, string(tmp));
			iPos++;
		}
		else if (*(oFile + iPos) == 0x19)
		{//neg(),len = 0
			char tmp[1024] = { 0 };

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (iPos == it->add && (it->Printed == false))
				{
					it->Printed = true;
					sprintf(tmp, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(tmp));
					break;
				}
			}
			*/

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			EAX.str.clear();
			VarInfo var = temp.top();
			VarInfo var2;
			temp.pop();
			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "(- %s)", var.str.c_str());
			var2.str = tmp;
			temp.push(var2);
			
			iPos++;
		}
		else if (*(oFile + iPos) == 0x1A)
		{//add(),len = 0;a=pop;b=pop;push a+b
			char tmp[1024] = { 0 };

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (iPos == it->add && (it->Printed == false))
				{
					it->Printed = true;
					sprintf(tmp, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(tmp));
					break;
				}
			}
			*/

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			iPos++;
			VarInfo t1 = temp.top();
			temp.pop();
			VarInfo t2 = temp.top();
			temp.pop();

			VarInfo kk;
			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "LuaAdd(%s , %s)", t2.str.c_str(), t1.str.c_str());
			kk.str = tmp;
			kk.type = STRING;

			temp.push(kk);
		}
		else if (*(oFile + iPos) == 0x1B)
		{//sub(),len = 0; a=pop;b=pop;push a-b
			char tmp[1024] = { 0 };

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (iPos == it->add && (it->Printed == false))
				{
					it->Printed = true;
					sprintf(tmp, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(tmp));
					break;
				}
			}
			*/

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			iPos++;
			VarInfo t1 = temp.top();
			temp.pop();
			VarInfo t2 = temp.top();
			temp.pop();
			//char *t = new char[1024];
			//memset(t,0,1024);
			VarInfo kk;
			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "(%s - %s)", t2.str.c_str(), t1.str.c_str());
			kk.str = tmp;
			kk.type = STRING;
			temp.push(kk);
		}
		else if (*(oFile + iPos) == 0x1C)
		{//mul(),len = 0; a=pop;b=pop;push a*b
			char tmp[1024] = { 0 };

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (iPos == it->add && (it->Printed == false))
				{
					it->Printed = true;
					sprintf(tmp, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(tmp));
					break;
				}
			}
			*/

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			VarInfo t1 = temp.top();
			temp.pop();
			VarInfo t2 = temp.top();
			temp.pop();
			VarInfo kk;
			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "(%s * %s)", t2.str.c_str(), t1.str.c_str());
			kk.str = tmp;
			kk.type = STRING;
			temp.push(kk);
			iPos++;
		}
		else if (*(oFile + iPos) == 0x1D)
		{//div(),len = 0; a=pop;b=pop;push a/b
			char tmp[1024] = { 0 };

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (iPos == it->add && (it->Printed == false))
				{
					it->Printed = true;
					sprintf(tmp, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(tmp));
					break;
				}
			}
			*/

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			VarInfo t1 = temp.top();
			temp.pop();
			VarInfo t2 = temp.top();
			temp.pop();
			VarInfo kk;

			FormatString(kk.str, "(%s / %s)", t2.str, t1.str);
			kk.type = STRING;
			temp.push(kk);
			iPos++;

		}
		else if (*(oFile + iPos) == 0x1E)
		{//mod(),len = 0; a=pop;b=pop;push a%b
			char tmp[1024] = { 0 };

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (iPos == it->add && (it->Printed == false))
				{
					it->Printed = true;
					sprintf(tmp, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(tmp));
					break;
				}
			}
			*/

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			iPos++;
			VarInfo t1 = temp.top();
			temp.pop();
			VarInfo t2 = temp.top();
			temp.pop();
			//char *t = new char[1024];
			//memset(t,0,1024);
			VarInfo kk;
			kk.str = "(";
			kk.str += t2.str;
			kk.str += " % ";
			kk.str += t1.str;
			kk.str += ")";
			kk.type = STRING;
			temp.push(kk);
			//delete[] t;
			//iPos++;

		}
		else if (*(oFile + iPos) == 0x1F)
		{//bitTest(),len = 0; a=pop;b=pop;pusha&(1<<b)?1:0
			char tmp[1024] = { 0 };

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (iPos == it->add && (it->Printed == false))
				{
					it->Printed = true;
					sprintf(tmp, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(tmp));
					break;
				}
			}
			*/

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			VarInfo t1 = temp.top();
			temp.pop();
			VarInfo t2 = temp.top();
			temp.pop();
			//VarList.push( bit1&(1<<bit2) ? 1:0 ); //CFlag?
			VarInfo var;
			var.type = STRING;

			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "(%s &(1<<%s))", t2.str.c_str(), t1.str.c_str());
			var.str = tmp;
			temp.push(var);
			//delete[] t;
			iPos++;
		}
		else if (*(oFile + iPos) == 0x20)
		{//CondAnd(),len = 0;//a=pop.type;b=pop.type;push x.type(1or0)
			char tmp[1024] = { 0 };

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (iPos == it->add && (it->Printed == false))
				{
					it->Printed = true;
					sprintf(tmp, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(tmp));
					break;
				}
			}
			*/

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			VarInfo t1 = temp.top();
			temp.pop();
			VarInfo t2 = temp.top();
			temp.pop();
			VarInfo ba;

			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "(%s and %s)", t2.str.c_str(), t1.str.c_str());
			ba.str = tmp;
			ba.type = STRING;
			temp.push(ba);
			iPos++;
		}
		else if (*(oFile + iPos) == 0x21)
		{//CondOr(),len = 0, ||
			char tmp[1024] = { 0 };

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (iPos == it->add && (it->Printed == false))
				{
					it->Printed = true;
					sprintf(tmp, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(tmp));
					break;
				}
			}
			*/

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			VarInfo t1 = temp.top();
			temp.pop();
			VarInfo t2 = temp.top();
			temp.pop();
			VarInfo ba;
			
			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "(%s or %s)", t2.str.c_str(), t1.str.c_str());
			ba.str = tmp;
			ba.type = STRING;

			temp.push(ba);
			iPos++;
		}
		else if (*(oFile + iPos) == 0x22)
		{//SetE(),len = 0; //a=pop;b=pop;pushx.type(a==b?1:0)
			char tmp[1024] = { 0 };

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (iPos == it->add && (it->Printed == false))
				{
					it->Printed = true;
					sprintf(tmp, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(tmp));
					break;
				}
			}
			*/

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			VarInfo t1 = temp.top();
			temp.pop();
			VarInfo t2 = temp.top();
			temp.pop();

			VarInfo kk;

			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "(%s == %s)", t2.str.c_str(), t1.str.c_str());
			
			kk.str = tmp;
			kk.type = STRING;
			temp.push(kk);
			iPos++;
		}
		else if (*(oFile + iPos) == 0x23)
		{//SetNE(),len = 0,!=0
			char tmp[1024] = { 0 };

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (iPos == it->add && (it->Printed == false))
				{
					it->Printed = true;
					sprintf(tmp, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(tmp));
					break;
				}
			}
			*/

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			VarInfo t1 = temp.top();
			temp.pop();
			VarInfo t2 = temp.top();
			temp.pop();
			VarInfo kk;

			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "(%s ~= %s)", t2.str.c_str(), t1.str.c_str());
			kk.str = tmp;
			kk.type = STRING;
			temp.push(kk);
			//delete[] t;
			iPos++;
		}
		else if (*(oFile + iPos) == 0x24)
		{//SetG(),len = 0,>
			char tmp[1024] = { 0 };

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (iPos == it->add && (it->Printed == false))
				{
					it->Printed = true;
					sprintf(tmp, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(tmp));
					break;
				}
			}
			*/

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			//>
			VarInfo t1 = temp.top();
			temp.pop();
			VarInfo t2 = temp.top();
			temp.pop();

			VarInfo kk;

			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "(%s > %s)", t2.str.c_str(), t1.str.c_str());
			kk.str = tmp;

			kk.type = STRING;
			temp.push(kk);
			//delete[] t;
			iPos++;
		}
		else if (*(oFile + iPos) == 0x25)
		{//SetLE(),len = 0,<=
			char tmp[1024] = { 0 };

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (iPos == it->add && (it->Printed == false))
				{
					it->Printed = true;
					sprintf(tmp, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(tmp));
					break;
				}
			}
			*/

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			//<=
			VarInfo t1 = temp.top();
			temp.pop();
			VarInfo t2 = temp.top();
			temp.pop();

			VarInfo kk;

			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "(%s <= %s)", t2.str.c_str(), t1.str.c_str());
			kk.str = tmp;
			kk.type = STRING;
			temp.push(kk);
			//delete[] t;
			iPos++;
		}
		else if (*(oFile + iPos) == 0x26)
		{//SetL(),len = 0,<
			char tmp[1024] = { 0 };

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (iPos == it->add && (it->Printed == false))
				{
					it->Printed = true;
					sprintf(tmp, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(tmp));
					break;
				}
			}
			*/

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			//<
			VarInfo t1 = temp.top();
			temp.pop();
			VarInfo t2 = temp.top();
			temp.pop();
			VarInfo kk;

			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "(%s < %s)", t2.str.c_str(), t1.str.c_str());
			kk.str = tmp;
			kk.type = STRING;

			temp.push(kk);
			//delete[] t;
			iPos++;
		}
		else if (*(oFile + iPos) == 0x27)
		{//SetGE(),len = 0,>=
			char tmp[1024] = { 0 };

			/*
			for (it = JMPTable.begin(); it != JMPTable.end(); it++)
			{
				if (iPos == it->add && (it->Printed == false))
				{
					it->Printed = true;
					sprintf(tmp, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(tmp));
					break;
				}
			}
			*/

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "::hcb_%08x::\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			//>=
			VarInfo t1 = temp.top();
			temp.pop();
			VarInfo t2 = temp.top();
			temp.pop();
			VarInfo kk;

			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "(%s >= %s)", t2.str.c_str(), t1.str.c_str());
			kk.str = tmp;
			kk.type = STRING;
			temp.push(kk);
			//delete[] t;
			iPos++;
		}
		else
		{ 
			fprintf(stderr, "[%08x]Parser Error!\n", iPos);
			iPos++;
		}
	}

	///Last Function
	WriteDirect(stackinfo, string("end\r\n"));
	WriteDirect(stackinfo, string("-----------End of Funtion---------\r\n"));

	/*************************************/

	unsigned short game_w = 0, game_h = 0;
	if (game_mode == 1)
	{
		game_w = 800;
		game_h = 600;
	}
	else if (game_mode == 7)
	{
		game_w = 1024;
		game_h = 640;
	}
	else
	{
		game_w = 800;
		game_h = 600;
	}

	int cp = CodePage;
	EntryCode(stackinfo, game_w, game_h, title, entry_proc, cp);
	fclose(stackinfo);
	return 0;
}


int main(int argc, char **argv)
{
	return Main(argc, argv);
}


//一些必要的代码
void InitCode(FILE* fin)
{
	WriteDirect(fin, string("-------Init Global data-------\r\n"));
	WriteDirect(fin, string("--FVP2Lua\r\n"));
	WriteDirect(fin, string("--X'moe Project\r\n"));
	WriteDirect(fin, string("--xmoe.project@gmail.com\r\n"));
	WriteDirect(fin, string("--Using Lua5.3 as target language\r\n"));
	WriteDirect(fin, string("---------------------------\r\n\r\n"));

	///Process
	///Declare all global data

	WriteDirect(fin, string("--Global Data\r\n"));
	WriteDirect(fin, string("GlobalData = {}\r\n"));
	WriteDirect(fin, string("--Table Data[1]\r\n"));
	WriteDirect(fin, string("Map1 = {}\r\n"));
	WriteDirect(fin, string("--Table Data[2]\r\n"));
	WriteDirect(fin, string("Map2 = {}\r\n"));
}


//entry
//需要把原始的entry给包装起来
void EntryCode(FILE* fin, unsigned short w, unsigned short h, string& title, unsigned int entry_proc, int cp)
{
	char tmp[512] = { 0 };
	sprintf(tmp, "GameWidth = %d\r\n", w);
	Write(fin, string(tmp));
	memset(tmp, 0, sizeof(tmp));

	sprintf(tmp, "GameHeight = %d\r\n", h);
	Write(fin, string(tmp));
	memset(tmp, 0, sizeof(tmp));

	if (cp == 932)
	{
		Clear
			SJISConv(title.c_str());
		sprintf(tmp, "GameTitle = \"%s\"\r\n", Buffer);
	}
	else
	{
		Clear
			CHSConv(title.c_str());
		sprintf(tmp, "GameTitle = \"%s\"\r\n", Buffer);
	}
	Write(fin, string(tmp));
	memset(tmp, 0, sizeof(tmp));

	WriteDirect(fin, string("-------Entry Function-------\r\n"));
	WriteDirect(fin, string("function Entry()\r\n"));

	IncTab
		sprintf(tmp, "function_%08x()\r\n", entry_proc);
		Write(fin, string(tmp));
	DecTab
	WriteDirect(fin, string("end\r\n"));

	///End of init proc
	WriteDirect(fin, string("-------------End--------------\r\n\r\n"));
}



void LuaAddCode(FILE* fin)
{
	WriteDirect(fin, string("-------Lua Add Function-------\r\n"));
	WriteDirect(fin, string("function LuaAdd(a, b)\r\n"));
	char tmp[512] = { 0 };
	IncTab
		Write(fin, string("if((type(a) == \"string\") and (type(b) == \"string\")) then \r\n"));
		Write(fin, string("\treturn a..b\r\n"));
		Write(fin, string("else\r\n"));
		Write(fin, string("\treturn a+b\r\n"));
		Write(fin, string("end\r\n"));
	DecTab
	WriteDirect(fin, string("end\r\n"));
	WriteDirect(fin, string("-------------End--------------\r\n\r\n"));
}
