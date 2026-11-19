// Displays the last authenticated battery reading for a physically attached
// Razer Wolverine V3 Pro in Xbox Mode (1532:0A3F).
// The GIP transport only provides responses to a foreground WinRT client.
#include "wolverine_xbox_cache.h"
#include <setupapi.h>
#include <cwchar>
#include <algorithm>

#pragma comment(lib, "setupapi.lib")
namespace {
const wchar_t* const kRegistry = L"Software\\DrayBattery\\WolverineXbox";
constexpr DWORD kVersion = 1;
struct Cache {
    DWORD version = kVersion;
    DWORD raw = 0;
    DWORD charging = 0;
    DWORD reserved = 0;
    ULONGLONG updatedFiletime = 0;
};
ULONGLONG Now() {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER v{};
    v.LowPart = ft.dwLowDateTime;
    v.HighPart = ft.dwHighDateTime;
    return v.QuadPart;
}

}
bool IsWolverineXboxPresent() {
    HDEVINFO devices=SetupDiGetClassDevsW(nullptr,nullptr,nullptr,
                                           DIGCF_PRESENT|DIGCF_ALLCLASSES);
    if(devices==INVALID_HANDLE_VALUE)return false;
    bool present=false;
    for(DWORD i=0;i<10000;i++){
        SP_DEVINFO_DATA info{};
        info.cbSize=sizeof(info);
        if(!SetupDiEnumDeviceInfo(devices,i,&info))break;
        wchar_t id[1024]{};
        if(!SetupDiGetDeviceInstanceIdW(devices,&info,id,1024,nullptr))continue;
        if(_wcsnicmp(id,L"USB\\VID_1532&PID_0A3F",21)==0){
            present=true;
            break;
        }
    }
    SetupDiDestroyDeviceInfoList(devices);
    return present;
}
bool SaveWolverineXboxReading(int rawValue, int charging) {
    if(rawValue<0 || rawValue>255)return false;
    Cache data{};
    data.raw=(DWORD)rawValue;
    data.charging=charging>0?1:0;
    data.updatedFiletime=Now();
    HKEY h=nullptr;
    if(RegCreateKeyExW(HKEY_CURRENT_USER,kRegistry,0,nullptr,0,KEY_SET_VALUE,
                       nullptr,&h,nullptr)!=ERROR_SUCCESS)return false;
    LONG result=RegSetValueExW(h,L"Battery",0,REG_BINARY,
                              reinterpret_cast<const BYTE*>(&data),sizeof(data));
    RegCloseKey(h);
    return result==ERROR_SUCCESS;
}
void PollWolverineXbox(PollContext&) {
    // The legacy registry record is keyed only by model, not by controller instance.
    // It cannot seed a verified current-session battery or prove which receiver owns it.
    // The foreground GIP reader remains available; no cached record is polled here.
}
