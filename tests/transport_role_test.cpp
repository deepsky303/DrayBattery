// Production transport-role declarations with mock HID. No hardware is opened.
#include "../src/drivers/drivers.h"
#include "../src/battery_lifecycle.h"
#include <cassert>
#include <cstdio>
#include <map>
namespace {
std::map<std::wstring, std::vector<BYTE>> pending;
BYTE coolerLink = 1;
int keychronMode = 0; // classic shared, explicit 8K radio, explicit 8K cable
HidColl Source(USHORT vid, USHORT pid, USHORT feature = 65) {
    HidColl c; c.vid=vid;c.pid=pid;c.path=L"mock:"+std::to_wstring(vid)+L":"+std::to_wstring(pid);
    c.connectionId=L"usb:"+c.path;c.transport=LinkKind::UsbCable;
    c.iface=2;c.caps.UsagePage=0xFF00;c.caps.FeatureReportByteLength=feature;
    c.caps.OutputReportByteLength=65;c.caps.InputReportByteLength=65;
    return c;
}
DeviceStatus Run(void (*driver)(PollContext&), const HidColl& source) {
    PollContext ctx;ctx.colls={source};driver(ctx);assert(ctx.out.size()==1);return ctx.out.front();
}
void Retained(const DeviceStatus& status, bool receiver) {
    assert(status.receiverBacked==receiver);
    assert(draybattery::PresenceBacked(status)==receiver);
    DevicePoll p;p.devices={status};p.presence.usbComplete=true;
    p.presence.usbPresent={status.connectionId};p.presence.observedMs=GetTickCount64();
    draybattery::BatteryLifecycle state;state.Apply(p,p.presence.observedMs);assert(state.Entries().size()==1);
    p.devices.clear();p.presence.observedMs+=30*24*3600000ull;
    state.Apply(p,p.presence.observedMs);assert(state.Entries().size()==(receiver?1u:0u));
}
}
bool HidDev::Open(const HidColl& c){c_=c;h_=reinterpret_cast<HANDLE>(1);return true;}
bool HidDev::OpenFeatureOnly(const HidColl& c){return Open(c);}
void HidDev::Close(){h_=INVALID_HANDLE_VALUE;}
void HidDev::Flush(){}
bool HidDev::Write(const std::vector<BYTE>& req,int) {
    std::vector<BYTE> r(65,0);
    if(c_.vid==0x1038){assert(req[1]==0x92);r[0]=0x92;r[2]=41;}
    else if(c_.vid==0x2516){assert(req[1]==0x46);r[0]=0x47;r[1]=0x81;r[10]=41;r[12]=coolerLink;}
    else if(c_.vid==0x3554){
        assert(req.size()==17&&req[0]==8);r.resize(17);r[0]=8;r[1]=req[1];r[6]=req[1]==3?1:41;
        unsigned sum=0;for(int i=0;i<16;++i)sum+=r[i];r[16]=(BYTE)(0x55-sum);
    }else if(c_.vid==0x3434){
        const BYTE cmd=req[1];r[0]=cmd;r[3]=1;r[11]=41;
        if(keychronMode==0){r[6]=0x34;r[7]=0x34;}
        else if(keychronMode==1 && cmd==1)r[0]=0xFE;
        else if(keychronMode==2){if(cmd==0x41)r[0]=0xFE;else r[5]=8;}
    }else{assert(false);return false;}
    pending[c_.path]=std::move(r);return true;
}
int HidDev::Read(std::vector<BYTE>& r,int){r=std::move(pending[c_.path]);return r.empty()?-1:(int)r.size();}
bool HidDev::SetFeature(const std::vector<BYTE>& req){
    auto r=req;
    if(c_.vid==0x1532){
        assert(req.size()==91&&req[7]==7);r[1]=2;r[10]=req[8]==0x80?104:0;
        BYTE sum=0;for(int i=3;i<89;++i)sum^=r[i];r[89]=sum;
    }else{assert(req.size()==65&&req[6]==0x83);r[1]=0xA1;r[4]=2;r[6]=0x83;r[8]=41;}
    pending[c_.path]=std::move(r);return true;
}
bool HidDev::GetFeature(BYTE,std::vector<BYTE>& r){return Read(r,0)>0;}
std::wstring HidProduct(const HidColl&){return L"Unverified wireless model";}
std::wstring Hex4(USHORT v){wchar_t b[8];swprintf(b,8,L"%04X",v);return b;}
int Clamp100(int v){return v<0?0:v>100?100:v;}
int MapTo100(int v,int lo,int hi){return Clamp100((v-lo)*100/(hi-lo));}
DeviceKind GuessKind(const std::wstring&){return DeviceKind::Mouse;}
DeviceKind KindFromCollections(const std::vector<HidColl>&,USHORT,USHORT){return DeviceKind::Mouse;}
int main(){
    auto uwb=Source(0x1038,0x1880);
    auto status=Run(PollSteelSeries,uwb);assert(status.via==L"UWB base station");Retained(status,true);
    auto cooler=Source(0x2516,0x01B7);cooler.iface=3;cooler.caps.UsagePage=0xFF0A;
    Retained(Run(PollCoolerMaster,cooler),true);
    coolerLink=0;Retained(Run(PollCoolerMaster,cooler),false);
    coolerLink=7;Retained(Run(PollCoolerMaster,cooler),false); // unknown protocol mode
    auto keychron=Source(0x3434,0x9999);
    Retained(Run(PollKeychron,keychron),false); // classic reply has no transport bit
    keychron.receiverEvidence=true;Retained(Run(PollKeychron,keychron),true);keychron.receiverEvidence=false;
    keychronMode=1;Retained(Run(PollKeychron,keychron),true);
    keychronMode=2;Retained(Run(PollKeychron,keychron),false);
    auto kysona=Source(0x3554,0x9999);kysona.caps.OutputReportByteLength=17;kysona.caps.InputReportByteLength=17;
    auto glorious=Source(0x258A,0x9999),lamzu=Source(0x37B0,0x9999),wolverine=Source(0x1532,0x0A4C,91);
    wolverine.caps.UsagePage=1;
    for(auto pair:{std::make_pair(PollKysona,kysona),std::make_pair(PollGlorious,glorious),
                   std::make_pair(PollLamzu,lamzu),std::make_pair(PollWolverinePc,wolverine)}){
        Retained(Run(pair.first,pair.second),false); // neither PID nor model declares radio
        pair.second.receiverEvidence=true;
        Retained(Run(pair.first,pair.second),true); // exact PnP ancestor can declare it
    }
    auto ambiguous=Run(PollWolverinePc,wolverine);
    assert(ambiguous.via==L"Razer HID (real battery)"&&ambiguous.linkKind==LinkKind::Unknown);
    puts("TRANSPORT_ROLE_TESTS_PASSED (production UWB, Cooler Master radio/cable/unknown, mixed Kysona/Glorious/LAMZU, ambiguous Wolverine, exact PnP proof, days)");
}
