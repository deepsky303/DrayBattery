// Production vendor parsers + central enrichment + lifecycle. Transport is simulated.
#include "../src/drivers/drivers.h"
#include "../src/battery_lifecycle.h"
#include <cassert>
#include <cstdio>
#include <map>
namespace {
std::map<std::wstring,std::vector<BYTE>> pending;
bool offline=false;
int Level(const HidColl& c){return c.path.back()==L'a'?24:67;}
HidColl Source(USHORT vid,USHORT pid,const wchar_t* vendor,wchar_t unit) {
    HidColl c;c.vid=vid;c.pid=pid;c.path=std::wstring(vendor)+L":"+unit;
    c.connectionId=L"usb:"+c.path;c.transport=LinkKind::UsbCable;
    c.caps.UsagePage=0xFF00;c.caps.InputReportByteLength=64;c.caps.OutputReportByteLength=64;
    if(vid==0x1532){c.caps.FeatureReportByteLength=91;c.receiverEvidence=true;}
    if(vid==0x046A){c.caps.UsagePage=0xFF1C;c.caps.Usage=0x0092;}
    if(vid==0x3537)c.caps.UsagePage=0xFF7A;
    return c;
}
}
bool HidDev::Open(const HidColl& c){c_=c;h_=reinterpret_cast<HANDLE>(1);return true;}
bool HidDev::OpenFeatureOnly(const HidColl& c){return Open(c);}
void HidDev::Close(){h_=INVALID_HANDLE_VALUE;}
void HidDev::Flush(){}
bool HidDev::Write(const std::vector<BYTE>& command,int){
    std::vector<BYTE> r(64,0);
    if(c_.vid==0x1B1C){assert(command[0]==0xC9);r[2]=(BYTE)Level(c_);r[4]=offline?0:2;}
    else if(c_.vid==0x046A){assert(command[0]==4&&command[3]==0x1A);if(offline)return false;r[0]=4;r[3]=0x1A;r[8]=(BYTE)Level(c_);}
    else if(c_.vid==0x3537){assert(command[1]==1);r[0]=1;r[18]=offline?0xFF:(BYTE)Level(c_);}
    else {assert(false);return false;}
    pending[c_.path]=std::move(r);return true;
}
int HidDev::Read(std::vector<BYTE>& r,int){auto& p=pending[c_.path];if(p.empty())return -1;r=std::move(p);return (int)r.size();}
bool HidDev::SetFeature(const std::vector<BYTE>& request){
    assert(c_.vid==0x1532&&request.size()==91);auto response=request;
    response[1]=offline?0x04:0x02;
    response[10]=request[8]==0x80?(BYTE)(Level(c_)*255/100):0;
    pending[c_.path]=std::move(response);return true;
}
bool HidDev::GetFeature(BYTE,std::vector<BYTE>& r){return Read(r,0)>0;}
std::wstring HidProduct(const HidColl& c){return c.vid==0x1532?L"Razer same-model Receiver":L"same-model device";}
std::wstring Hex4(USHORT v){wchar_t b[8];swprintf(b,8,L"%04X",v);return b;}
int Clamp100(int v){return v<0?0:v>100?100:v;}
DeviceKind GuessKind(const std::wstring&){return DeviceKind::Mouse;}
DeviceKind KindFromCollections(const std::vector<HidColl>&,USHORT,USHORT){return DeviceKind::Mouse;}
int main(){
    std::vector<HidColl> sources;
    for(wchar_t unit:{L'a',L'b'}){
        sources.push_back(Source(0x1532,0x0099,L"razer",unit));
        sources.push_back(Source(0x1B1C,0x0A14,L"corsair",unit));
        sources.push_back(Source(0x046A,0x00EA,L"cherry",unit));
        sources.push_back(Source(0x3537,0x1098,L"gamesir",unit));
    }
    PollContext ctx;ctx.colls=sources;
    PollRazer(ctx);PollCorsair(ctx);PollCherry(ctx);PollGameSir(ctx);
    assert(ctx.out.size()==8);
    DevicePoll p;p.devices=ctx.out;p.presence.usbComplete=true;p.presence.observedMs=GetTickCount64()+1;
    for(const auto& c:sources)p.presence.usbPresent.push_back(c.connectionId);
    for(const auto& d:ctx.out)assert(d.receiverBacked&&d.batteryFresh&&d.percent>=23&&d.percent<=67);
    draybattery::BatteryLifecycle state;state.Apply(p,p.presence.observedMs);assert(state.Entries().size()==8);
    std::map<std::wstring,std::pair<int,std::uint64_t>> original;
    for(const auto& pair:state.Entries())original[pair.first]={pair.second.lastPct,pair.second.lastLiveMs};
    offline=true;ctx={};ctx.colls=sources;PollRazer(ctx);PollCorsair(ctx);PollCherry(ctx);PollGameSir(ctx);
    for(const auto& d:ctx.out)assert(!d.batteryFresh); // explicit radio false, no answer, unknown/charging
    auto base=p.presence.observedMs;p.devices=ctx.out;
    for(auto elapsed:{90000ull,3600000ull,12*3600000ull,3*24*3600000ull}){
        p.presence.observedMs=base+elapsed;state.Apply(p,p.presence.observedMs);assert(state.Entries().size()==8);
        for(const auto& pair:state.Entries()){
            const auto& e=pair.second;assert(e.st.asleep&&!e.st.charging);
            assert(e.lastPct==original[pair.first].first&&e.lastLiveMs==original[pair.first].second);
        }
    }
    p.devices.clear();p.presence.usbPresent.erase(p.presence.usbPresent.begin());p.presence.observedMs++;
    state.Apply(p,p.presence.observedMs);assert(state.Entries().size()==7);
    p.presence.usbPresent.push_back(sources[0].connectionId);p.presence.observedMs++;
    state.Apply(p,p.presence.observedMs);assert(state.Entries().size()==7);
    puts("VENDOR_PRESENCE_TESTS_PASSED (production Razer/Corsair/CHERRY/GameSir, 8 exact receivers, same PID/name, vendor-off, missing battery, days, unplug/replug)");
}
