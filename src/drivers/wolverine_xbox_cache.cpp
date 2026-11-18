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
constexpr unsigned long long kDay100ns = 24ull*60*60*10000000ull;
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
bool Load(Cache& result) {
    DWORD type=0,size=sizeof(result);
    if(RegGetValueW(HKEY_CURRENT_USER,kRegistry,L"Battery",
         RRF_RT_REG_BINARY,&type,&result,&size)!=ERROR_SUCCESS ||
         type!=REG_BINARY || size!=sizeof(result)) return false;
    ULONGLONG now=Now();
    return result.version==kVersion && result.raw<=255 &&
           result.charging<=2 && result.updatedFiletime>0 &&
           result.updatedFiletime<=now &&
           now-result.updatedFiletime<=2*kDay100ns;
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
void PollWolverineXbox(PollContext& ctx) {
    if(!IsWolverineXboxPresent())return;
    Cache cache{};
    const bool cached = Load(cache);
    ULONGLONG now=Now();
    ULONGLONG age=cached ? (now-cache.updatedFiletime)/10000000ull : 0;
    DeviceStatus s;
    s.key=L"wolverine:1532:0A3F";
    s.physicalId=L"razer:wolverine-v3pro:primary";
    s.name=L"Razer Wolverine V3 Pro";
    s.via=L"Xbox GIP (controller battery estimate)";
    s.kind=DeviceKind::Gamepad;
    s.percent=cached ? Clamp100((int)((cache.raw*100+127)/255)) : -1;
    s.estimated = cached; // normalized controller-reported value; never show a leading ~ glyph
    // Windows suspends the GIP sink when our reader window loses focus.
    // Never imply that a stale battery reading is live.
    s.asleep=cached && age>300;
    s.connected=true;
    s.charging=cached && cache.charging!=0 && age<=300;
    if (!cached) {
        s.via=L"Xbox GIP (refresh via menu)";
        s.graceSec=0;
    }
    s.graceSec=1800;
    ctx.Add(s,0x1532,0x0A3F);
}
