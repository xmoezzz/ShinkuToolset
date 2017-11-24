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

static char Buffer[4096] = { 0 };

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
	~userFunctionInfo() {}
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


static bool First = true;


/**********************************************/
int Main(int argc, char **argv)
{
	if (argc != 2 && argc != 3)
		return -1;
	
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
	stackinfo = fopen((string(argv[1]) + ".txt").c_str(), "wb");
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


	printf("Reading user call(s)...\n");
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
			bool addFlag = true;
								 
			JMPTable.insert(std::make_pair(JUMPADD, false));
			iPos += 4;
		}
		else if (*(oFile + iPos) == 0x07)
		{
			iPos++;
			bool jzAddFlag = true;
			memcpy(&JZADD, (oFile + iPos), 4);

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
	printf("Found [%08x] user function(s).\n", userCallCount);


	static bool NextTempReturn = false;

	/*****************************************************/
	iPos = 4;
	while (iPos < RealStackADD)
	{
		if (*(oFile + iPos) == 0x0E)
		{
			char tmp[256] = { 0 };

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "Label_%08x:\r\n", iPos);
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
		{
			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "Label_%08x:\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}


			char MakeString[128] = { 0 };
			sprintf(MakeString, "goto Label_", iPos);
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
					sprintf(MakeString, "Label_%08x:\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}


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
		{
			char tmp[256] = { 0 };

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "Label_%08x:\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

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
					WriteDirect(stackinfo, string("}\r\n"));
				WriteDirect(stackinfo, string("//-----------End of Funtion---------\r\n"));
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
			sprintf(tmp, "//Decompiled function %08x", iPos - 3);
			WriteDirect(stackinfo, string(tmp));
			WriteDirect(stackinfo, string("--------\r\n"));
			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "function_%08x ( ", iPos - 3);
			Write(stackinfo, string(tmp));
			if (num != 0)
			{
				for (int var = 1; var <= num; var++)
				{
					memset(tmp, 0, sizeof(tmp));
					sprintf(tmp, "var_%02x%s", 0xff - var, var == num ? "" : ",");
					WriteDirect(stackinfo, string(tmp));
				}
				WriteDirect(stackinfo, string(" )\r\n{\r\n"));
			}
			else 
			{
				WriteDirect(stackinfo, string(")\r\n{\r\n"));
			}
			IncTab
				for (unsigned int index = 0; index < local; index++)
				{
					memset(tmp, 0, sizeof(tmp));
					sprintf(tmp, "var var_%02x;\r\n", index);
					Write(stackinfo, string(tmp));
				}
		}
		else if (*(oFile + iPos) == 0x04)
		{ //return(void),len = 0
			char tmp[256] = { 0 };

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "Label_%08x:\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			if (*(u8*)(oFile + iPos))
			{
				it = JMPTable.find(iPos + 1);
				if (it != JMPTable.end())
				{
					if (!it->second)
					{
						it->second = true;
						char MakeString[128] = { 0 };
						sprintf(MakeString, "Label_%08x:\r\n", iPos + 1);
						Write(stackinfo, string(MakeString));
					}
				}
			}

			iPos++;
			//DecTab
			if (NextTempReturn)
			{
				
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


			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "Label_%08x:\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			if (*(u8*)(oFile + iPos + 1) == 0x04)
			{
				it = JMPTable.find(iPos + 1);
				if (it != JMPTable.end())
				{
					if (!it->second)
					{
						it->second = true;
						char MakeString[128] = { 0 };
						sprintf(MakeString, "Label_%08x:\r\n", iPos + 1);
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

			}
			else
			{
				if (NextTempReturn)
				{
					Write(stackinfo, string("else return "));
					WriteDirect(stackinfo, EAX.str);
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
		{
			char tmp[2048] = { 0 };

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "Label_%08x:\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			VarInfo s2 = temp.top();
			temp.pop();

			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "if(false == %s) goto ", s2.str.c_str());
			Write(stackinfo, string(tmp));

			iPos++;
			memcpy(&JZADD, (oFile + iPos), 4);
			iPos += 4;

			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "Label_%08x:\r\n", JZADD);
			WriteDirect(stackinfo, string(tmp));
		}
		else if (*(oFile + iPos) == 0x08)
		{
			char tmp[256] = { 0 };

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "Label_%08x:\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			VarInfo var;
			var.type = STRING;
			var.str = "false";
			EAX.str = "false";
			temp.push(var);
			iPos++;
		}
		else if (*(oFile + iPos) == 0x09)
		{//push1()


			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "Label_%08x:\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			VarInfo var;
			var.type = STRING;
			var.str = "true";
			EAX.str = "true";
			temp.push(var);
			iPos++;
		}
		else if (*(oFile + iPos) == 0x0A)
		{//PushInt32() : DWORD
			char tmp[256] = { 0 };


			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "Label_%08x:\r\n", iPos);
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


			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "Label_%08x:\r\n", iPos);
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

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "Label_%08x:\r\n", iPos);
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

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "Label_%08x:\r\n", iPos);
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


			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "Label_%08x:\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			iPos++;

			WORD GINT16ADD;
			memcpy(&GINT16ADD, (oFile + iPos), 2);

			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "GlobalData_%04x", GINT16ADD);
			EAX.str = tmp;
			EAX.str = tmp;
			temp.push(EAX);
			iPos += 2;
		}
		else if (*(oFile + iPos) == 0x10)
		{
			char tmp[256] = { 0 };


			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "Label_%08x:\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			iPos++;
			VarInfo var;
			var.type = STRING;
			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "var_%02x", (u8)*(oFile + iPos));
			var.str = tmp;
			EAX.str = tmp;
			temp.push(var);
			iPos++;
		}
		else if (*(oFile + iPos) == 0x11)
		{ 
			char tmp[256] = { 0 };


			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "Label_%08x:\r\n", iPos);
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
			sprintf(tmp, "GlobalData_%04x[%s]", MMapADD, var.str.c_str());
			var2.str = tmp;
			temp.push(var2);
			EAX.str = tmp;
			iPos += 2;
		}
		else if (*(oFile + iPos) == 0x12)
		{ 
			char tmp[256] = { 0 };

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "Label_%08x:\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			VarInfo var = temp.top();
			temp.pop();
			VarInfo var2;
			var2.type = STRING;
			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "var_%02x[%s]", (u8)*(oFile + iPos + 1), var.str.c_str());
			var2.str = tmp;
			temp.push(var2);
			EAX.str = tmp;
			iPos += 2;
		}
		else if (*(oFile + iPos) == 0x13)
		{ 
			char tmp[256] = { 0 };


			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "Label_%08x:\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			//fprintf(stackinfo,"[%08x]PushTop ();\r\n",iPos);
			fprintf(stderr, "Error! [PushTop] was called!\n");
			iPos++;
		}
		else if (*(oFile + iPos) == 0x14)
		{ 
			char tmp[256] = { 0 };

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "Label_%08x:\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			temp.push(EAX);
			iPos++;
		}
		else if (*(oFile + iPos) == 0x15)
		{
			char tmp[512] = { 0 };
			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "Label_%08x:\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			iPos++;
			//printf("Running\n");
			WORD PINT16ADD;
			memcpy(&PINT16ADD, (oFile + iPos), 2);
			iPos += 2;

			EAX = temp.top();
			temp.pop();

			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "GlobalData_%04x = %s;\r\n", PINT16ADD, EAX.str.c_str());
			Write(stackinfo, string(tmp));
		}
		else if (*(oFile + iPos) == 0x16)
		{
			char tmp[512] = { 0 };

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "Label_%08x:\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			VarInfo var = temp.top();
			temp.pop();

			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "var_%02x = %s;\r\n", (u8)*(oFile + iPos + 1), var.str.c_str());
			Write(stackinfo, string(tmp));
			iPos += 2;
		}
		else if (*(oFile + iPos) == 0x17)
		{
			char tmp[512] = { 0 };

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "Label_%08x:\r\n", iPos);
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
			sprintf(tmp, "GlobalData_%04x[%s] = %s;\r\n", MMapADD, key_t.str.c_str(), value_t.str.c_str());
			Write(stackinfo, string(tmp));
			iPos += 2;
		}
		else if (*(oFile + iPos) == 0x18)
		{
			char tmp[512] = { 0 };

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "Label_%08x:\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			iPos++;
			VarInfo value_t = temp.top();
			temp.pop();
			VarInfo key_t = temp.top();
			temp.pop();

			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "var_%02x[%s] = %s;\r\n", (unsigned char)*(oFile + iPos), key_t.str.c_str(), value_t.str.c_str());
			Write(stackinfo, string(tmp));
			iPos++;
		}
		else if (*(oFile + iPos) == 0x19)
		{
			char tmp[1024] = { 0 };

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "Label_%08x:\r\n", iPos);
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
		{
			char tmp[1024] = { 0 };


			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "Label_%08x:\r\n", iPos);
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
			sprintf(tmp, "(%s + %s)", t2.str.c_str(), t1.str.c_str());
			kk.str = tmp;
			kk.type = STRING;

			temp.push(kk);
		}
		else if (*(oFile + iPos) == 0x1B)
		{
			char tmp[1024] = { 0 };

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "Label_%08x:\r\n", iPos);
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
		{
			char tmp[1024] = { 0 };

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "Label_%08x:\r\n", iPos);
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
		{
			char tmp[1024] = { 0 };

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "Label_%08x:\r\n", iPos);
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
		{
			char tmp[1024] = { 0 };

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
		{
			char tmp[1024] = { 0 };

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "Label_%08x:\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			VarInfo t1 = temp.top();
			temp.pop();
			VarInfo t2 = temp.top();
			temp.pop();

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
		{
			char tmp[1024] = { 0 };

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "Label_%08x:\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			VarInfo t1 = temp.top();
			temp.pop();
			VarInfo t2 = temp.top();
			temp.pop();
			VarInfo ba;

			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "(%s && %s)", t2.str.c_str(), t1.str.c_str());
			ba.str = tmp;
			ba.type = STRING;
			temp.push(ba);
			iPos++;
		}
		else if (*(oFile + iPos) == 0x21)
		{
			char tmp[1024] = { 0 };

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "Label_%08x:\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			VarInfo t1 = temp.top();
			temp.pop();
			VarInfo t2 = temp.top();
			temp.pop();
			VarInfo ba;

			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "(%s || %s)", t2.str.c_str(), t1.str.c_str());
			ba.str = tmp;
			ba.type = STRING;

			temp.push(ba);
			iPos++;
		}
		else if (*(oFile + iPos) == 0x22)
		{
			char tmp[1024] = { 0 };

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "Label_%08x:\r\n", iPos);
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
		{
			char tmp[1024] = { 0 };


			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "Label_%08x:\r\n", iPos);
					Write(stackinfo, string(MakeString));
				}
			}

			VarInfo t1 = temp.top();
			temp.pop();
			VarInfo t2 = temp.top();
			temp.pop();
			VarInfo kk;

			memset(tmp, 0, sizeof(tmp));
			sprintf(tmp, "(%s != %s)", t2.str.c_str(), t1.str.c_str());
			kk.str = tmp;
			kk.type = STRING;
			temp.push(kk);
			//delete[] t;
			iPos++;
		}
		else if (*(oFile + iPos) == 0x24)
		{
			char tmp[1024] = { 0 };

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "Label_%08x:\r\n", iPos);
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
		{
			char tmp[1024] = { 0 };

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "Label_%08x:\r\n", iPos);
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
		{
			char tmp[1024] = { 0 };

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "Label_%08x:\r\n", iPos);
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
		{
			char tmp[1024] = { 0 };

			it = JMPTable.find(iPos);
			if (it != JMPTable.end())
			{
				if (!it->second)
				{
					it->second = true;
					char MakeString[128] = { 0 };
					sprintf(MakeString, "Label_%08x:\r\n", iPos);
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

	WriteDirect(stackinfo, string("//-----------End of Funtion---------\r\n"));

	fclose(stackinfo);
	return 0;
}


int main(int argc, char **argv)
{
	return Main(argc, argv);
}

