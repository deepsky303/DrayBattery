// Optional, strictly opt-in USB name catalogue from upstream hwdata/usb.ids.
// DEVICE NAMES ARE NOT BATTERY PROTOCOLS. Never choose a vendor HID writer
// based solely on a public VID:PID label.
#include "device_db.h"
#include "device_db_parser.h"
#include "hid.h"
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace draybattery::device_db {
namespace {
constexpr size_t MaxBytes=4u*1024*1024;
constexpr wchar_t Host[]=L"raw.githubusercontent.com";
constexpr wchar_t Path[]=L"/vcrhonek/hwdata/master/usb.ids";
struct Http {
    HINTERNET handle=nullptr;
    explicit Http(HINTERNET h=nullptr):handle(h){}
    ~Http(){if(handle)WinHttpCloseHandle(handle);}
    Http(const Http&)=delete;
    Http& operator=(const Http&)=delete;
    explicit operator bool() const {return handle!=nullptr;}
};

std::wstring CachePath() {
    wchar_t folder[32768]{};
    DWORD size=GetEnvironmentVariableW(L"LOCALAPPDATA",folder,32768);
    if(!size || size>=32768) return {};
    return std::wstring(folder)+L"\\DrayBattery\\usb.ids";
}
bool ReadFile(std::string& data) {
    auto name=CachePath();
    if(name.empty()) return false;
    std::error_code ec;
    auto size=std::filesystem::file_size(std::filesystem::path(name),ec);
    if(ec || size<64 || size>MaxBytes) return false;
    std::ifstream in(std::filesystem::path(name),std::ios::binary);
    if(!in) return false;
    data.assign(std::istreambuf_iterator<char>(in),{});
    return in.good() || in.eof();
}
bool ReadCatalogue(usbids::Database& data) {
    std::string contents;
    return ReadFile(contents) && usbids::Parse(contents,data);
}
std::wstring Widen(const std::string& utf8) {
    if(utf8.empty()) return {};
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,
                              utf8.c_str(),(int)utf8.size(),nullptr,0);
    if(n<=0) return {};
    std::wstring wide(n,L'\0');
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,
                            utf8.c_str(),(int)utf8.size(),wide.data(),n))
        return {};
    return wide;
}
bool Fetch(std::string& bytes, std::wstring& error) {
    Http session(WinHttpOpen(L"DrayBattery/0.1 optional-usb-ids",
                            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                            WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    if(!session){error=L"WinHTTP session unavailable";return false;}
    WinHttpSetTimeouts(session.handle,5000,5000,10000,15000);
    Http conn(WinHttpConnect(session.handle,Host,INTERNET_DEFAULT_HTTPS_PORT,0));
    if(!conn){error=L"Unable to connect to the database host";return false;}
    Http req(WinHttpOpenRequest(conn.handle,L"GET",Path,nullptr,
                               WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,
                               WINHTTP_FLAG_SECURE));
    if(!req){error=L"Cannot create HTTPS request";return false;}
    DWORD noRedirects=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(req.handle,WINHTTP_OPTION_REDIRECT_POLICY,
                     &noRedirects,sizeof(noRedirects));
    if(!WinHttpSendRequest(req.handle,WINHTTP_NO_ADDITIONAL_HEADERS,0,
                           WINHTTP_NO_REQUEST_DATA,0,0,0) ||
       !WinHttpReceiveResponse(req.handle,nullptr)){
        error=L"HTTPS request failed. Local cache was not changed.";
        return false;
    }
    DWORD status=0,n=sizeof(status);
    if(!WinHttpQueryHeaders(req.handle,WINHTTP_QUERY_STATUS_CODE |
                           WINHTTP_QUERY_FLAG_NUMBER,nullptr,
                           &status,&n,nullptr) || status!=200){
        error=L"Database server returned an unexpected HTTP status";
        return false;
    }
    bytes.clear();
    for(;;){
        char chunk[8192];
        DWORD count=0;
        if(!WinHttpReadData(req.handle,chunk,sizeof(chunk),&count)){
            error=L"Failed to read the HTTPS response";
            return false;
        }
        if(!count) break;
        if(bytes.size()+count>MaxBytes){
            error=L"Catalogue exceeds the safe size limit";
            return false;
        }
        bytes.append(chunk,count);
    }
    return true;
}
bool Save(const std::string& data) {
    auto name=CachePath();
    if(name.empty())return false;
    auto pos=name.find_last_of(L'\\');
    if(pos==std::wstring::npos)return false;
    std::wstring folder=name.substr(0,pos);
    if(!CreateDirectoryW(folder.c_str(),nullptr) &&
       GetLastError()!=ERROR_ALREADY_EXISTS)return false;
    auto temp=name+L".tmp";
    // Restrict writes to the current user's LocalAppData, never program files.
    FILE* file=nullptr;
    if(_wfopen_s(&file,temp.c_str(),L"wb")!=0 || !file)return false;
    bool ok=fwrite(data.data(),1,data.size(),file)==data.size();
    if(fclose(file)!=0)ok=false;
    if(!ok){DeleteFileW(temp.c_str());return false;}
    if(!MoveFileExW(temp.c_str(),name.c_str(),
                    MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
        DeleteFileW(temp.c_str());
        return false;
    }
    return true;
}
} // namespace

std::wstring CacheInfo() {
    usbids::Database db;
    if(!ReadCatalogue(db))
        return L"USB names database: not installed (offline mode).\n"
               L"Use --update-device-db to download it manually.\n";
    return L"USB names database (offline cache): "+std::to_wstring(db.vendors.size())+
           L" vendors, "+std::to_wstring(db.products.size())+
           L" product IDs.\nSource: hwdata/usb.ids (name lookup only).\n";
}

std::wstring IdentifyConnectedHid() {
    usbids::Database db;
    bool cached=ReadCatalogue(db);
    std::wstring result=L"USB/HID names (identification only; not proof of battery support):\n";
    std::set<uint32_t> seen;
    for(const HidColl& h: EnumHid()){
        uint32_t key=(uint32_t(h.vid)<<16)|h.pid;
        if(!seen.insert(key).second)continue;
        result+=Hex4(h.vid)+L":"+Hex4(h.pid)+L"  ";
        if(cached) {
            const std::string* vendor=db.Vendor(h.vid);
            const std::string* product=db.Product(h.vid,h.pid);
            if(vendor)result+=Widen(*vendor);
            if(product){
                if(vendor)result+=L" / ";
                result+=Widen(*product);
            } else {
                // Prefer the actual HID product descriptor to a bare vendor name.
                auto hidName=HidProduct(h);
                if(!hidName.empty()){
                    if(vendor)result+=L" / ";
                    result+=hidName;
                } else if(!vendor){
                    result+=L"(name unknown)";
                }
            }
            result+=L"\n";
            continue;
        }
        auto label=HidProduct(h);
        result+=(label.empty()?L"(name unknown)":label)+L"\n";
    }
    if(!cached)result+=L"\nUSB names cache not installed; run --update-device-db.\n";
    return result;
}

bool UpdateNames(std::wstring& message) {
    std::string bytes;
    if(!Fetch(bytes,message))return false;
    if(bytes.size()<100000 || bytes.rfind("#",0)!=0 ||
       bytes.find("List of USB ID")==std::string::npos){
        message=L"Response was not a recognized usb.ids catalogue. Cache preserved.";
        return false;
    }
    usbids::Database parsed;
    if(!usbids::Parse(bytes,parsed) || parsed.vendors.size()<1000 ||
       parsed.products.size()<3000){
        message=L"Downloaded catalogue failed validation. Cache preserved.";
        return false;
    }
    if(!Save(bytes)){
        message=L"Could not update the LocalAppData cache. Previous file preserved.";
        return false;
    }
    message=L"USB device names updated: "+std::to_wstring(parsed.vendors.size())+
            L" vendors, "+std::to_wstring(parsed.products.size())+
            L" products.\nThis updates names, NOT battery drivers.\n"
            L"No device IDs or serials were sent.";
    return true;
}

} // namespace draybattery::device_db
