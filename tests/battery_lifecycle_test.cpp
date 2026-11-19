#include "../src/battery_lifecycle.h"
#include "../src/battery_view.h"
#include "../src/drivers/ghub_status.h"
#include <cassert>
#include <iostream>
using namespace draybattery;
constexpr std::uint64_t t0 = 100000;
DeviceStatus Live(std::wstring link=L"receiver-A", std::wstring unit=L"unit-A", int pct=42,
                  DeviceKind kind=DeviceKind::Mouse, std::uint64_t at=t0) {
    DeviceStatus s;
    s.key=L"logi:"+link+L":1"; s.sourceKeys={s.key};
    s.name=L"Logitech G903 LIGHTSPEED"; s.physicalId=unit; s.connectionId=link;
    s.receiverBacked=true; s.linkKind=LinkKind::UsbReceiver;
    s.batteryFresh=true; s.lastLiveMs=at; s.percent=pct; s.kind=kind;
    return s;
}
DeviceStatus State(DeviceStatus s,const char* state,const char* json=nullptr,std::uint64_t at=t0) {
    ghubjson::Value battery;
    if(json) assert(ghubjson::Parser(json).parse(battery));
    ghub::ApplyBatteryState(s,ghub::ParseDeviceState(state),json?&battery:nullptr,at);
    s.ghub=true;
    return s;
}
DevicePoll Snapshot(std::vector<DeviceStatus> devices={}, std::vector<std::wstring> usb={L"receiver-A"},
                    std::uint64_t at=t0) {
    DevicePoll p; p.devices=std::move(devices); p.presence.usbPresent=std::move(usb);
    p.presence.usbComplete=true; p.presence.bluetoothComplete=true; p.presence.observedMs=at;
    return p;
}
DevicePoll Bt(std::vector<DeviceStatus> devices, Presence state, std::uint64_t at) {
    auto p=Snapshot(std::move(devices),{},at);
    if(state==Presence::Present)p.presence.btConnected={L"container:bt-unit"};
    if(state==Presence::Absent)p.presence.btDisconnected={L"container:bt-unit"};
    if(state==Presence::Unknown)p.presence.btUnknown={L"container:bt-unit"};
    return p;
}
const BatteryEntry& CheckedOnly(const BatteryLifecycle& s,int line){
    if(s.Entries().size()!=1)std::cerr<<"Expected one entry at test line "<<line<<", got "<<s.Entries().size()<<std::endl;
    assert(s.Entries().size()==1);return s.Entries().begin()->second;
}
#define Only(state) CheckedOnly(state,__LINE__)
void AssertDim(const BatteryEntry& e,int pct,std::uint64_t live=t0) {
    assert(e.st.asleep && e.st.percent==pct && e.lastPct==pct && e.lastLiveMs==live);
    assert(!e.st.charging && !e.st.batteryFresh);
    assert(VisibleBattery(e.st,{}) && DimmedBattery(e.st) && !ChargingBattery(e.st) && !LowBattery(e.st));
}
int main(){
    const auto mouse=Live();
    // G903/G915 and every supported peripheral kind use the SAME pure policy.
    for(const auto* name:{L"G903",L"G915 X",L"Razer",L"Corsair",L"CHERRY",L"Rapoo"})
    for(auto kind:{DeviceKind::Mouse,DeviceKind::Keyboard,DeviceKind::Gamepad,DeviceKind::Headset,DeviceKind::Other}) {
        BatteryLifecycle s; auto a=Live(L"receiver-A",L"unit-A",12,kind),b=Live(L"receiver-B",L"unit-B",67,kind);
        a.name=name; b.name=name; a.charging=true;
        s.Apply(Snapshot({a,b},{L"receiver-A",L"receiver-B"}),t0);
        for(auto elapsed:{90000ull,3600000ull,12*3600000ull,3*24*3600000ull}) {
            auto sleeping=State(a,"SLEEPING","{\"percentage\":100,\"charging\":true}");
            s.Apply(Snapshot({sleeping},{L"receiver-A",L"receiver-B"},t0+elapsed),t0+elapsed);
            assert(s.Entries().size()==2);
            for(const auto& pair:s.Entries()) AssertDim(pair.second,pair.second.st.connectionId==L"receiver-A"?12:67);
            assert(!s.Expire(t0+elapsed+1));
        }
        auto wake=State(a,"ACTIVE","{\"percentage\":11}",t0+3*24*3600000ull+1);
        s.Apply(Snapshot({wake},{L"receiver-A",L"receiver-B"},wake.lastLiveMs),wake.lastLiveMs);
        assert(!s.Entries().at(LogicalKey(a)).st.asleep);
    }
    {
        BatteryLifecycle s; s.Apply(Snapshot({mouse}),t0);
        auto off=mouse;off.connected=false;off.percent=100;off.lastLiveMs=t0+1;
        s.Apply(Snapshot({off},{L"receiver-A"},t0+1),t0+1);
        AssertDim(Only(s),42); // vendor false is not USB physical removal
        auto notConnected=State(mouse,"NOT_CONNECTED","{\"percentage\":100,\"charging\":true}");
        for(auto elapsed:{90000ull,1800000ull,12*3600000ull,30*24*3600000ull}) {
            s.Apply(Snapshot({notConnected},{L"receiver-A"},t0+elapsed),t0+elapsed);
            AssertDim(Only(s),42);
        }
        // Continue the removal/generation checks in a fresh deterministic timeline.
        s=BatteryLifecycle{};s.Apply(Snapshot({mouse}),t0);
        s.Apply(Snapshot({}, {},t0+2),t0+2);assert(s.Entries().empty());
        s.Apply(Snapshot({State(mouse,"SLEEPING")},{L"receiver-A"},t0+3),t0+3);assert(s.Entries().empty());
        s.Apply(Snapshot({mouse},{L"receiver-A"},t0+4),t0+4);assert(s.Entries().empty()); // replay predates unplug
        auto fresh=Live(L"receiver-A",L"unit-A",40,DeviceKind::Mouse,t0+5);
        s.Apply(Snapshot({fresh},{L"receiver-A"},t0+5),t0+5);assert(Only(s).lastPct==40);
        s.RemoveConnection(L"receiver-A",t0+6);assert(s.Entries().empty());
        auto queued=Snapshot({fresh},{L"receiver-A"},t0+5);
        assert(!s.Apply(queued,t0+7,1)); // stale hotplug generation
        queued.generation=1; s.Apply(queued,t0+7,1);assert(s.Entries().empty());
    }
    {
        BatteryLifecycle s;auto bt=mouse;bt.key=L"bt-unit";bt.physicalId=L"bt-unit";
        bt.connectionId=L"container:bt-unit";bt.linkKind=LinkKind::Bluetooth;bt.receiverBacked=false;
        // Neither paired-only state nor adapter inventory is a positive connection.
        s.Apply(Bt({bt},Presence::Unknown,t0),t0);assert(s.Entries().empty());
        auto adapter=Snapshot({bt},{L"host-bluetooth-adapter"},t0);s.Apply(adapter,t0);assert(s.Entries().empty());
        s=BatteryLifecycle{}; // independent startup scenario; the previous snapshot proved absence
        auto cached=bt;cached.batteryCached=true;cached.percent=100;
        s.Apply(Bt({cached},Presence::Present,t0),t0);assert(s.Entries().empty());
        s.Apply(Bt({bt},Presence::Present,t0),t0);assert(Only(s).lastPct==42);
        for(auto elapsed:{90000ull,1800000ull,12*3600000ull,3*24*3600000ull}) {
            s.Apply(Bt({},Presence::Present,t0+elapsed),t0+elapsed);AssertDim(Only(s),42);
        }
        auto at=t0+3*24*3600000ull+1;
        s.Apply(Bt({cached},Presence::Absent,at),at);assert(s.Entries().empty());
        s.Apply(Bt({},Presence::Present,at+1),at+1);assert(s.Entries().empty());
        s.Apply(Bt({cached},Presence::Present,at+2),at+2);assert(s.Entries().empty());
        bt.lastLiveMs=at+3;s.Apply(Bt({bt},Presence::Present,at+3),at+3);assert(Only(s).lastLiveMs==at+3);
        auto unpaired=Snapshot({}, {},at+4);s.Apply(unpaired,at+4);assert(s.Entries().empty());
    }
    {
        BatteryLifecycle s;
        for(auto stale:{State(mouse,"SLEEPING","{\"percentage\":100}"),State(mouse,"NOT_CONNECTED","{\"percentage\":100}")}) {
            s.Apply(Snapshot({stale}),t0);assert(s.Entries().empty());
        }
        auto cache=mouse;cache.batteryCached=true;s.Apply(Snapshot({cache}),t0);assert(s.Entries().empty());
        cache.batteryCached=false;cache.batteryFresh=false;s.Apply(Snapshot({cache}),t0);assert(s.Entries().empty());
        cache.batteryFresh=true;cache.lastLiveMs=0;s.Apply(Snapshot({cache}),t0);assert(s.Entries().empty());
        auto invalid=State(mouse,"ACTIVE","{\"percentage\":101}");s.Apply(Snapshot({invalid}),t0);assert(s.Entries().empty());
        auto removed=State(mouse,"ACTIVE","{\"percentage\":100,\"isBatteryRemoved\":true}");s.Apply(Snapshot({removed}),t0);assert(s.Entries().empty());
        auto zero=State(mouse,"ACTIVE","{\"percentage\":0}");s.Apply(Snapshot({zero}),t0);assert(Only(s).lastPct==0);
    }
    {
        // Incomplete enumeration cannot prove absence, even after a long transient.
        BatteryLifecycle s;s.Apply(Snapshot({mouse}),t0);
        auto p=Snapshot({}, {},t0+1000);p.presence.usbComplete=false;
        s.Apply(p,t0+1000);AssertDim(Only(s),42);assert(Only(s).st.presenceUncertain);
        p.presence.observedMs=t0+10000;s.Apply(p,t0+10000);assert(!s.Entries().empty());
        s.Expire(t0+60000);AssertDim(Only(s),42);
        s.Apply(Snapshot({}, {L"receiver-A"},t0+61000),t0+61000);AssertDim(Only(s),42);
        // A positive exact instance inside an incomplete inventory remains sufficient.
        s.Apply(Snapshot({Live(L"receiver-A",L"unit-A",41,DeviceKind::Mouse,t0+62000)},{L"receiver-A"},t0+62000),t0+62000);
        p=Snapshot({}, {L"receiver-A"},t0+3*24*3600000ull);p.presence.usbComplete=false;
        s.Apply(p,p.presence.observedMs);AssertDim(Only(s),41,t0+62000);
    }
    {
        BatteryLifecycle s;auto keyboard=Live(L"receiver-K",L"keyboard",9,DeviceKind::Keyboard);
        s.Apply(Snapshot({mouse,keyboard},{L"receiver-A",L"receiver-K"}),t0);
        auto partial=Snapshot({State(mouse,"SLEEPING")},{},t0+1);
        partial.presence={};partial.complete=false;partial.polledConnections={L"receiver-A"};
        s.Apply(partial,t0+1);assert(s.Entries().size()==2);
        assert(!s.Entries().at(LogicalKey(keyboard)).st.asleep);AssertDim(s.Entries().at(LogicalKey(mouse)),42);
        // A presence-only snapshot must not mark other sources missing.
        auto inventory=Snapshot({}, {L"receiver-A",L"receiver-K"},t0+2000);inventory.complete=false;
        s.Apply(inventory,t0+2000);assert(!s.Entries().at(LogicalKey(keyboard)).st.asleep);
        // A stalled verifier marks uncertainty, without falsely declaring unplug.
        s.Expire(t0+2000+kPresenceCheckMaxAgeMs);assert(s.Entries().size()==2);
        for(const auto& pair:s.Entries()) assert(pair.second.st.presenceUncertain);
        s.Expire(t0+120000);assert(s.Entries().size()==2);
        auto removed=Snapshot({}, {},t0+120001);removed.complete=false;
        s.Apply(removed,t0+120001);assert(s.Entries().empty());
    }
    {
        BatteryLifecycle s;auto hid=mouse;hid.physicalId.clear();s.Apply(Snapshot({hid}),t0);
        const auto key=s.Entries().begin()->first;
        auto hub=State(mouse,"ACTIVE","{\"percentage\":39}",t0+1);
        s.Apply(Snapshot({hub},{L"receiver-A"},t0+1),t0+1);
        assert(s.Entries().size()==1 && s.Entries().begin()->first==key);
        hid.lastLiveMs=t0+2;hid.percent=38;
        s.Apply(Snapshot({hid,hub},{L"receiver-A"},t0+2),t0+2);
        assert(s.Entries().size()==1);assert(Only(s).st.physicalId==mouse.physicalId);
        assert(!VisibleBattery(Only(s).st,{key}));
        auto other=mouse;other.connectionId=L"receiver-B";other.key=L"logi:receiver-B:1";other.physicalId=L"unit-B";
        s.Apply(Snapshot({hid,other},{L"receiver-A",L"receiver-B"},t0+3),t0+3);assert(s.Entries().size()==2);
        auto older=mouse;older.percent=99;s.Apply(Snapshot({older},{L"receiver-A",L"receiver-B"},t0+4),t0+4);
        assert(s.Entries().at(key).lastPct==38);
        assert(!s.Apply(Snapshot(),t0+4,0,true));
    }
    {
        BatteryLifecycle s;auto bt=mouse;bt.key=L"bt-unit";bt.physicalId=L"bt-unit";
        bt.connectionId=L"container:bt-unit";bt.linkKind=LinkKind::Bluetooth;bt.receiverBacked=false;
        s.Apply(Bt({bt},Presence::Present,t0),t0);
        auto incomplete=Bt({},Presence::Unknown,t0+1000);incomplete.presence.bluetoothComplete=false;
        s.Apply(incomplete,t0+1000);AssertDim(Only(s),42);
        incomplete.presence.observedMs=t0+10000;s.Apply(incomplete,t0+10000);
        s.Expire(t0+120000);AssertDim(Only(s),42);
        s.Apply(Bt({},Presence::Present,t0+120001),t0+120001);AssertDim(Only(s),42);
        s.Apply(Bt({},Presence::Absent,t0+120002),t0+120002);assert(s.Entries().empty());
    }
    {
        BatteryLifecycle s;s.Apply(Snapshot({mouse}),t0);
        auto inventoryOnly=Snapshot({}, {},t0+1);inventoryOnly.complete=false;
        s.Apply(inventoryOnly,t0+1);assert(s.Entries().empty());
        auto unknown=mouse;unknown.receiverBacked=false;unknown.linkKind=LinkKind::Unknown;unknown.connectionId.clear();
        s.Apply(Snapshot({unknown}),t0);assert(Only(s).lastPct==42);
        assert(s.Expire(t0+kUnlinkedSampleMaxAgeMs));assert(s.Entries().empty());
    }
    std::cout<<"BATTERY_LIFECYCLE_TESTS_PASSED (all kinds/vendors, days, exact removal/replug, BT proof, cache, uncertainty, partial scope, UI, identity)\n";
}
