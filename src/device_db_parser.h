#pragma once
// Offline parser for the public usb.ids format.
// This data identifies device models; it cannot read battery levels or choose
// a device-specific HID protocol.
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <unordered_map>

namespace draybattery::usbids {

struct Database {
    std::unordered_map<uint16_t, std::string> vendors;
    std::unordered_map<uint32_t, std::string> products;

    const std::string* Vendor(uint16_t vid) const {
        auto it=vendors.find(vid);
        return it == vendors.end() ? nullptr : &it->second;
    }
    const std::string* Product(uint16_t vid, uint16_t pid) const {
        auto it=products.find((uint32_t(vid)<<16)|pid);
        return it == products.end() ? nullptr : &it->second;
    }
};

inline int HexDigit(char ch) {
    if (ch>='0' && ch<='9') return ch-'0';
    if (ch>='a' && ch<='f') return ch-'a'+10;
    if (ch>='A' && ch<='F') return ch-'A'+10;
    return -1;
}
inline int Hex4(std::string_view s) {
    if(s.size()!=4) return -1;
    int n=0;
    for(char ch:s) {
        int digit=HexDigit(ch);
        if(digit<0) return -1;
        n=(n<<4)|digit;
    }
    return n;
}
inline std::string TrimName(std::string_view s) {
    while(!s.empty() && (s.front()==' '||s.front()=='\t')) s.remove_prefix(1);
    while(!s.empty() && (s.back()==' '||s.back()=='\r')) s.remove_suffix(1);
    if(s.empty()||s.size()>200) return {};
    for(unsigned char ch:s)
        if(ch<0x20 && ch!='\t') return {};
    return std::string(s);
}
inline bool Parse(std::string_view raw, Database& output) {
    if(raw.size()>4u*1024*1024 || raw.find('\0')!=std::string_view::npos) return false;
    Database result;
    int currentVendor=-1;
    size_t begin=0;
    while(begin<raw.size()) {
        size_t end=raw.find('\n',begin);
        if(end==std::string_view::npos) end=raw.size();
        std::string_view line=raw.substr(begin,end-begin);
        begin=end+1;
        if(line.empty()||line.front()=='#') continue;
        // Exactly one leading tab marks a product, two mark an interface.
        if(line.front()=='\t') {
            if(currentVendor<0 || line.size()<7 || line[1]=='\t') continue;
            int pid=Hex4(line.substr(1,4));
            if(pid<0 || (line[5]!=' ' && line[5]!='\t')) continue;
            auto name=TrimName(line.substr(6));
            if(!name.empty())
                result.products[(uint32_t(currentVendor)<<16)|uint16_t(pid)]=std::move(name);
            continue;
        }
        currentVendor=-1;
        if(line.size()<7) continue;
        int vid=Hex4(line.substr(0,4));
        if(vid<0 || (line[4]!=' ' && line[4]!='\t')) continue;
        auto name=TrimName(line.substr(5));
        if(name.empty()) continue;
        result.vendors[(uint16_t)vid]=std::move(name);
        currentVendor=vid;
    }
    output=std::move(result);
    return true;
}

} // namespace draybattery::usbids
