#include "lifecycle.h"
#include <windows.h>
#include <cstdio>
#include <string>

// Helps diagnose a missing tray app after Windows logon.
// Not telemetry: local only, no serials/paths/device identifiers.
void LogDrayLifecycle(const wchar_t* event) {
    wchar_t base[32768]{};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA",base,32768);
    if(!n || n>=32768) return;
    std::wstring folder=std::wstring(base)+L"\\DrayBattery";
    if(!CreateDirectoryW(folder.c_str(),nullptr) && GetLastError()!=ERROR_ALREADY_EXISTS)
        return;
    std::wstring file=folder+L"\\lifecycle.log";
    HANDLE log=CreateFileW(file.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,
                           nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(log==INVALID_HANDLE_VALUE) return;
    SYSTEMTIME now{};
    GetLocalTime(&now);
    // Bound file growth: cycle at 128 KiB without blocking startup.
    LARGE_INTEGER size{};
    if(GetFileSizeEx(log,&size) && size.QuadPart>128*1024) {
        CloseHandle(log);
        MoveFileExW(file.c_str(),(folder+L"\\lifecycle.previous.log").c_str(),
                    MOVEFILE_REPLACE_EXISTING);
        log=CreateFileW(file.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,
                         nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(log==INVALID_HANDLE_VALUE) return;
    }
    wchar_t wide[256];
    swprintf_s(wide,L"%04u-%02u-%02u %02u:%02u:%02u PID=%lu %ls\r\n",
              now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,
              GetCurrentProcessId(),event ? event:L"unknown");
    char ascii[512]{};
    int bytes=WideCharToMultiByte(CP_UTF8,0,wide,-1,ascii,sizeof(ascii),nullptr,nullptr);
    if(bytes>1){DWORD written=0;WriteFile(log,ascii,(DWORD)(bytes-1),&written,nullptr);}
    CloseHandle(log);
}
