/*
Copyright (c) 2002 Robert Rainwater <rrainwater@yahoo.com>

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

*/
#include <windows.h>
#include <commctrl.h>
#include "../exdll/exdll.h"

#ifndef true
#define true TRUE
#endif
#ifndef false
#define false FALSE
#endif
#define TIMEOUT 15000
#define NOLOG_TIMEOUT 600000
#define LOOPTIMEOUT 100

HINSTANCE   g_hInstance;
HWND        g_hwndParent;
HWND        g_hwndList;
HWND        g_hwndDlg;
char *      g_exec;
char *      g_szto;
BOOL        g_foundto;
int         g_to;


void ExecScript(BOOL log);
void LogMessage(const char *pStr);
char *my_strstr(const char *string, const char *strCharSet);
int my_atoi(char *s);

void __declspec(dllexport) Exec(HWND hwndParent, int string_size, char *variables, stack_t **stacktop) {
    g_hwndParent=hwndParent;
    EXDLL_INIT();
    {
        ExecScript(false);
    }
}

void __declspec(dllexport) ExecToLog(HWND hwndParent, int string_size, char *variables, stack_t **stacktop) {
    g_hwndParent=hwndParent;
    EXDLL_INIT();
    {
        ExecScript(true);
    }
}

BOOL WINAPI _DllMainCRTStartup(HANDLE hInst, ULONG ul_reason_for_call, LPVOID lpReserved) {
    g_hInstance=hInst;
    return TRUE;
}

void ExecScript(BOOL log) {
    g_to = INFINITE;
    g_foundto = FALSE;
    g_hwndDlg = FindWindowEx(g_hwndParent,NULL,"#32770",NULL);
    g_hwndList = FindWindowEx(g_hwndDlg,NULL,"SysListView32",NULL);
    g_exec = (char *)GlobalAlloc(GPTR, sizeof(char)*g_stringsize+1);
    g_szto = (char *)GlobalAlloc(GPTR, sizeof(char)*g_stringsize+1);
    if (!popstring(g_szto)) {
        if (my_strstr(g_szto,"/TIMEOUT=")) {
            g_szto += 9;
            g_to = my_atoi(g_szto);
            if (g_to<0) g_to = log?TIMEOUT:NOLOG_TIMEOUT;
            g_foundto = TRUE;
        }
    }
    if (g_foundto) {
        if (popstring(g_exec)) {
            pushstring("error");
            return;
        }
    }
    else {
        lstrcpy(g_exec,g_szto);
    }
    {
        STARTUPINFO si={sizeof(si),};
        SECURITY_ATTRIBUTES sa={sizeof(sa),};
        SECURITY_DESCRIPTOR sd={0,};
        PROCESS_INFORMATION pi={0,};
        OSVERSIONINFO osv={sizeof(osv)};
        HANDLE newstdout=0,read_stdout=0;
        DWORD dwRead = 1;
        DWORD dwExit = !STILL_ACTIVE;
        static char szBuf[1024];
        static char szRet[128];
        HGLOBAL hUnusedBuf;
        static char *szUnusedBuf;

        if (log) {
            hUnusedBuf = GlobalAlloc(GHND, sizeof(szBuf)*4);
            if (!hUnusedBuf) {
                pushstring("error");
                return;
            }
            szUnusedBuf = (char *)GlobalLock(hUnusedBuf);

            GetVersionEx(&osv);
            if (osv.dwPlatformId == VER_PLATFORM_WIN32_NT) {
                InitializeSecurityDescriptor(&sd,SECURITY_DESCRIPTOR_REVISION);
                SetSecurityDescriptorDacl(&sd,true,NULL,false);
                sa.lpSecurityDescriptor = &sd;
            }
            else sa.lpSecurityDescriptor = NULL;
            sa.bInheritHandle = true;
            if (!CreatePipe(&read_stdout,&newstdout,&sa,0)) {
                pushstring("error");
                return;
            }
        }

        GetStartupInfo(&si);
        si.dwFlags = STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        si.hStdOutput = newstdout;
        si.hStdError = newstdout;
        if (!CreateProcess(NULL,g_exec,NULL,NULL,TRUE,CREATE_NEW_CONSOLE,NULL,NULL,&si,&pi)) {
            CloseHandle(newstdout);
            CloseHandle(read_stdout);
            pushstring("error");
            return;
        }

        if (log) {
            while (dwExit == STILL_ACTIVE || dwRead) {
                PeekNamedPipe(read_stdout, 0, 0, 0, &dwRead, NULL);
                if (dwRead) {
                    ReadFile(read_stdout, szBuf, sizeof(szBuf)-1, &dwRead, NULL);
                    szBuf[dwRead] = 0;
                    {
                        char *p, *lineBreak;
                        SIZE_T iReqLen = lstrlen(szBuf) + lstrlen(szUnusedBuf);
                        if (GlobalSize(hUnusedBuf) < iReqLen) {
                            GlobalUnlock(hUnusedBuf);
                            hUnusedBuf = GlobalReAlloc(hUnusedBuf, iReqLen+sizeof(szBuf), GHND);
                            if (!hUnusedBuf) {
                                pushstring("error");
                                CloseHandle(pi.hThread);
                                CloseHandle(pi.hProcess);
                                CloseHandle(newstdout);
                                CloseHandle(read_stdout);
                                return;
                            }
                            szUnusedBuf = (char *)GlobalLock(hUnusedBuf);
                        }
                        p = szUnusedBuf; // get the old left overs
                        lstrcat(p, szBuf);

                        while (lineBreak = my_strstr(p, "\r\n")) {
                            *lineBreak = 0;
                            LogMessage(p);
                            p = lineBreak + 2;
                        }

                        // If data was taken out from the unused buffer, move p contents to the start of szUnusedBuf
                        if (p != szUnusedBuf) {
                            char *p2 = szUnusedBuf;
                            while (*p) *p2++ = *p++;
                            *p2 = 0;
                        }
                    }
                }
                else Sleep(LOOPTIMEOUT);
                GetExitCodeProcess(pi.hProcess, &dwExit);
                if (dwExit != STILL_ACTIVE) {
                    PeekNamedPipe(read_stdout, 0, 0, 0, &dwRead, NULL);
                }
            }
            if (*szUnusedBuf) LogMessage(szUnusedBuf);
        }
        else WaitForSingleObject(pi.hProcess, g_to);
        wsprintf(szRet,"%d",dwExit);
        pushstring(szRet);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        CloseHandle(newstdout);
        CloseHandle(read_stdout);
        if (log) GlobalUnlock(hUnusedBuf);
    }
}

// Tim Kosse's LogMessage
void LogMessage(const char *pStr) {
    LVITEM item={0};
    int nItemCount;
    if (!g_hwndList) return;
    if (!lstrlen(pStr)) return;
    nItemCount=SendMessage(g_hwndList, LVM_GETITEMCOUNT, 0, 0);
    item.mask=LVIF_TEXT;
    item.pszText=(char *)pStr;
    item.cchTextMax=0;
    item.iItem=nItemCount;
    ListView_InsertItem(g_hwndList, &item);
    ListView_EnsureVisible(g_hwndList, item.iItem, 0);
}


char *my_strstr(const char *string, const char *strCharSet) {
    char *s1, *s2;
    size_t chklen;
    size_t i;
    if (lstrlen(string) < lstrlen(strCharSet)) return 0;
    if (!*strCharSet) return (char*)string;
    chklen=lstrlen(string)-lstrlen(strCharSet);
    for (i = 0; i <= chklen; i++) {
        s1=&((char*)string)[i];
        s2=(char*)strCharSet;
        while (*s1++ == *s2++)
        if (!*s2) return &((char*)string)[i];
    }
    return 0;
}

int my_atoi(char *s) {
    unsigned int v=0;
    if (*s == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s+=2;
        for (;;) {
            int c=*s++;
            if (c >= '0' && c <= '9') c-='0';
            else if (c >= 'a' && c <= 'f') c-='a'-10;
            else if (c >= 'A' && c <= 'F') c-='A'-10;
            else break;
            v<<=4;
            v+=c;
        }
    }
    else if (*s == '0' && s[1] <= '7' && s[1] >= '0') {
        s++;
        for (;;) {
            int c=*s++;
            if (c >= '0' && c <= '7') c-='0';
            else break;
            v<<=3;
            v+=c;
        }
    }
    else {
        int sign=0;
        if (*s == '-') { s++; sign++; }
        for (;;) {
            int c=*s++ - '0';
            if (c < 0 || c > 9) break;
            v*=10;
            v+=c;
        }
        if (sign) return -(int) v;
    }
    return (int)v;
}
