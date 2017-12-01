#include "stdafx.h"
#include "MojiCHS.h"
#include <map>

using namespace std;

#include "HookApi.h"
#pragma comment(lib,"HookApi.lib")

#define Snow

#ifdef Snow
const DWORD NAME_END = 0x1DFF;
#endif // Snow

#ifdef WA_funta
const DWORD NAME_END = 0x84D;
#endif // WA_funta

#pragma pack(push)
#pragma pack(2)

typedef struct _StackEntry{
    unsigned char type;
    //    unsigned char type2;
    unsigned short stackBase;
    unsigned long value;
}StackEntry,*PStackEntry;

#pragma pack(pop)

typedef struct _ScriptObject
{
    unsigned long unk1;
    LPVOID scriptBuffer;
    StackEntry st[0x100];
    int unk2;
    int unk3;
    bool bEnd;
    StackEntry temp;
    unsigned long vIP;
    unsigned long vSP;
    unsigned long vBP;
}ScriptObject,*PScriptObject;


map<DWORD,string> MojiTable;
map<WORD,WORD> gaijimapTable;
LPVOID offset = 0;


unsigned long __fastcall PushStr(ScriptObject *obj);
typedef decltype(&PushStr) __pfnPushStr;

unsigned long __fastcall HookPushStr(ScriptObject *obj,DWORD edxDummy,DWORD retAddr,__pfnPushStr pfnPushStr)//以后支持多push字串的call处理
{
    auto it = MojiTable.find(obj->vIP-1);
    DWORD strOffset = obj->vIP;
    if (it!=MojiTable.end())
    {
        DWORD retVal = pfnPushStr(obj);
        if (!offset)
        {
            DWORD HdrOffset = *(PDWORD)obj->scriptBuffer;
            char *lpHdr=(char *)obj->scriptBuffer+HdrOffset;
            lpHdr+=10;

            int cbTitle = *lpHdr;
            lpHdr+=cbTitle;

            offset = lpHdr+1;
        }
        if (obj->vIP<NAME_END)
        {
            char *pString = (char *)obj->scriptBuffer+strOffset;
            *pString++=it->second.size();
            memcpy(pString,it->second.c_str(),it->second.size()+1);
            MojiTable.erase(it);
        }
        else
        {
            memcpy(offset,it->second.c_str(),it->second.size()+1);
            obj->st[retVal].value = (DWORD)((DWORD)offset-(DWORD)obj->scriptBuffer);
        }
        return retVal;
    }
    return pfnPushStr(obj);
}

typedef decltype(&lstrcmpiA) __pfnlstrcmpiA;

int WINAPI HooklstrcmpiA(DWORD retAddr,__pfnlstrcmpiA pfnlstrcmpA,LPCSTR lpString1, LPCSTR lpString2)
{
    return CompareStringA(0x411, NORM_IGNORECASE, lpString1, -1, lpString2, -1) - 2;
}

// typedef decltype(&GetGlyphOutlineA) __pfnGetGlyphOutlineA;
//
// BOOL WINAPI My_GetGlyphOutlineA(DWORD RetAddr,
//                                 __pfnGetGlyphOutlineA pfnTextOutA,
//                                 HDC hdc,   // handle of device context
//                                 UINT uChar,   // character to query
//                                 UINT uFormat, // format of data to return
//                                 LPGLYPHMETRICS lpgm, // address of structure for metrics
//                                 DWORD cbBuffer,   // size of buffer for data
//
//                                 LPVOID lpvBuffer, // address of buffer for data
//
//                                 CONST MAT2 *lpmat2   // address of transformation matrix structure
//                                 )
// {
//     auto it = gaijimapTable.find(uChar);
//     if (it!=gaijimapTable.end())
//     {
//         BOOL st = GetGlyphOutlineA(hdc,it->second,uFormat,lpgm,cbBuffer,lpvBuffer,lpmat2);
//         SetLastError(0);
//         return st;
//     }
//     BOOL st = pfnTextOutA(hdc,uChar,uFormat,lpgm,cbBuffer,lpvBuffer,lpmat2);
//     SetLastError(0);
//     return st;
// }

typedef decltype(&DrawTextA) __pfnDrawTextA;
int WINAPI HookDrawTextA(DWORD retAddr,__pfnDrawTextA pfnDrawTextA,HDC hdc,LPCSTR lpchText,int cchText,LPRECT lprc,UINT format)
{
    WORD uChar = *(PWORD)lpchText;
    auto it = gaijimapTable.find(uChar);
    if (it!=gaijimapTable.end())
    {
        *(PWORD)lpchText = it->second;
        int st = pfnDrawTextA(hdc,lpchText,cchText,lprc,format);
        SetLastError(0);
        return st;
    }
    int st = pfnDrawTextA(hdc,lpchText,cchText,lprc,format);
    SetLastError(0);
    return st;
}

void Init()
{
    CMojiCHS moji("text.txt",true);
    size_t cntLine = moji.GetDictSize();
    for (size_t idx = 0;idx<cntLine;++idx)
    {
        wstring && kotoba = moji.GetString(idx);
        if (!kotoba.empty())
        {
            size_t idx2= moji.GetIndexDescr(idx);
            MojiTable[idx2] = UNI2GBK(kotoba.c_str(),kotoba.size());
        }
    }

    gaijimapTable[0x7581] = 0xB8A1;
    gaijimapTable[0x7681] = 0xB9A1;
    gaijimapTable[0x4081] = 0xA1A1;


    //sig :08C144C6
    //mov     byte ptr [ecx+eax*8+0x8], 0x4
    HINSTANCE hInstance=GetModuleHandle(NULL);

    PIMAGE_DOS_HEADER pDH=(PIMAGE_DOS_HEADER)hInstance;
    PIMAGE_NT_HEADERS32 pNtH = (PIMAGE_NT_HEADERS32)((DWORD)pDH+pDH->e_lfanew);

    DWORD dwPushStr = GetStubBySig((unsigned char *)pDH+pNtH->OptionalHeader.BaseOfCode,
        pNtH->OptionalHeader.BaseOfData-pNtH->OptionalHeader.BaseOfCode,
        0x0408C144);

    unsigned char *pHook = (unsigned char *)pDH+pNtH->OptionalHeader.BaseOfCode+dwPushStr;

    InstallHookStub(pHook,HookPushStr);
    InstallHookStub(lstrcmpiA,HooklstrcmpiA);
    InstallHookStub(DrawTextA,HookDrawTextA);
//    InstallHookStub(GetGlyphOutlineA,My_GetGlyphOutlineA);
}
