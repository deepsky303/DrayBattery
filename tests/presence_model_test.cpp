#include "../src/drivers/drivers.h"
#include "../src/bluetooth_presence.h"
#include "../src/battery_lifecycle.h"
#include "../src/battery_view.h"
#include <cassert>
#include <iostream>
using namespace draybattery;
HidColl Usb(std::wstring unit) {
    HidColl c;c.path=L"collection:"+unit;c.connectionId=L"usb:"+unit;c.transport=LinkKind::UsbCable;
    c.vid=0x1532;c.pid=0x9999;return c;
}
DeviceStatus Sample(std::wstring name=L"same-name") {
    DeviceStatus s;s.key=L"vendor:9999:slot1";s.name=name;s.percent=24;s.via=L"2.4 GHz receiver";s.linkKind=LinkKind::UsbReceiver;s.kind=DeviceKind::Mouse;return s;
}
int main(){
    assert(UsbRemovalConnectionId(L"\\\\?\\USB#VID_1234&PID_5678#unit-a#{guid}")==L"usb\\vid_1234&pid_5678\\unit-a");
    assert(UsbRemovalConnectionId(L"\\\\?\\HID#VID_1234&PID_5678#unit-a#{guid}").empty());
    assert(UsbRemovalConnectionId(L"\\\\?\\USB#VID_1234&PID_5678&MI_01#unit-a#{guid}").empty());
    auto a=Usb(L"a"),b=Usb(L"b");auto s=Sample();
    PollContext ctx;ctx.colls={a,b};
    ctx.Add(s,a.vid,a.pid,&a);ctx.Add(s,b.vid,b.pid,&b);
    assert(ctx.out.size()==2 && ctx.out[0].key!=ctx.out[1].key);
    assert(!SamePhysicalDevice(ctx.out[0],ctx.out[1]));
    for(const auto& d:ctx.out)assert(d.receiverBacked&&d.batteryFresh&&d.lastLiveMs>0);
    // Several collections on one receiver do not create duplicate slots.
    auto another=a;another.path=L"other-collection:a";ctx.Add(s,a.vid,a.pid,&another);assert(ctx.out.size()==2);
    // A source-less/multi-receiver PID never obtains physical presence from the inventory.
    PollContext ambiguous;ambiguous.colls={a,b};ambiguous.Add(s,a.vid,a.pid);
    assert(ambiguous.out.empty());
    ambiguous.colls={a};ambiguous.Add(s,a.vid,a.pid);
    assert(ambiguous.out[0].connectionId.empty()&&!PresenceBacked(ambiguous.out[0]));
    auto cable=s;cable.via=L"USB cable";cable.linkKind=LinkKind::UsbCable;ctx={};ctx.Add(cable,a.vid,a.pid,&a);
    assert(!ctx.out[0].receiverBacked&&ctx.out[0].linkKind==LinkKind::UsbCable);
    auto mixed=s;mixed.via=L"USB / Bluetooth";mixed.linkKind=LinkKind::Unknown;ctx={};ctx.Add(mixed,a.vid,a.pid,&a);assert(!PresenceBacked(ctx.out[0]));
    // Receiver labels do not declare a role, and label variants cannot break an explicit role.
    for (auto label : {L"2.4 GHz receiver / USB", L"UWB base station", L"Razer HID (real battery)", L"anything"}) {
        auto radio = s; radio.via = label; ctx = {}; ctx.Add(radio,a.vid,a.pid,&a);
        assert(PresenceBacked(ctx.out[0]));
        radio.linkKind=LinkKind::Unknown;ctx={};ctx.Add(radio,a.vid,a.pid,&a);
        assert(!PresenceBacked(ctx.out[0]));
        auto exact=a;exact.receiverEvidence=true;ctx={};ctx.Add(radio,a.vid,a.pid,&exact);
        assert(PresenceBacked(ctx.out[0]));
        ctx={};ctx.Add(cable,a.vid,a.pid,&exact);assert(!PresenceBacked(ctx.out[0]));
    }
    auto unknownRoot=a;unknownRoot.transport=LinkKind::Unknown;ctx={};ctx.Add(s,a.vid,a.pid,&unknownRoot);
    assert(!PresenceBacked(ctx.out[0]));
    // A vendor string never turns actual BT ancestry into a USB receiver.
    auto bt=a;bt.transport=LinkKind::Bluetooth;bt.connectionId=L"container:unit";
    ctx={};ctx.Add(s,bt.vid,bt.pid,&bt);
    assert(ctx.out[0].linkKind==LinkKind::Bluetooth&&!ctx.out[0].receiverBacked);
    assert(ctx.out[0].physicalId==L"bt:container:unit");
    // A reused cache carries the original timestamp and cannot be a fresh acquisition.
    auto cached=s;cached.batteryCached=true;cached.lastLiveMs=123;cached.charging=true;
    ctx={};ctx.Add(cached,a.vid,a.pid,&a);assert(!ctx.out[0].batteryFresh&&!ctx.out[0].charging&&ctx.out[0].lastLiveMs==123);
    auto offline=s;offline.connected=false;ctx={};ctx.Add(offline,a.vid,a.pid,&a);assert(!ctx.out[0].batteryFresh&&!ctx.out[0].lastLiveMs);

    const std::wstring root=L"BTHENUM\\DEV_112233445566\\instance";
    BluetoothNode main{root,L"unit",L"keyboard",Presence::Present};
    BluetoothNode service{L"BTHENUM\\{service}\\instance",L"unit",L"hands-free",Presence::Absent};
    assert(BluetoothPeripheralRoot(root));
    assert(BluetoothPeripheralRoot(L"BTHLE\\DEV_AABBCCDDEEFF\\instance"));
    assert(!BluetoothPeripheralRoot(L"BTHLEDEVICE\\{service}\\instance"));
    assert(!BluetoothPeripheralRoot(L"BTHENUM\\DEV_112233445566extra\\instance"));
    assert(!BluetoothPeripheralRoot(L"USB\\VID_8087&PID_0000\\host-radio"));
    auto p=BluetoothPresence({main,service},true,1000);
    assert(p.btConnected==std::vector<std::wstring>{L"container:unit"});
    service.connection=Presence::Present;main.connection=Presence::Absent;
    p=BluetoothPresence({main,service},true,1001);assert(p.btConnected.empty()&&p.btDisconnected.size()==1);
    main.connection=Presence::Unknown;
    p=BluetoothPresence({main,service},true,1002);assert(p.btConnected.empty()&&p.btUnknown.size()==1);
    p=BluetoothPresence({service},true,1003);assert(p.btConnected.empty()&&p.btDisconnected.size()==1);
    auto adapter=main;adapter.instance=L"USB\\VID_8087&PID_0000\\host-radio";adapter.connection=Presence::Present;
    p=BluetoothPresence({adapter},true,1004);assert(p.btConnected.empty());
    auto conflict=main;main.connection=Presence::Present;conflict.connection=Presence::Absent;
    p=BluetoothPresence({main,conflict},false,1005);assert(p.btConnected.empty()&&p.btUnknown.size()==1);
    DeviceStatus d;d.connectionId=L"container:unit";d.linkKind=LinkKind::Bluetooth;
    p=BluetoothPresence({},false,1006);assert(ConnectionPresence(p,d)==Presence::Unknown);
    p=BluetoothPresence({},true,1007);assert(ConnectionPresence(p,d)==Presence::Absent);
    main.container=L"other-unit";main.connection=Presence::Present;
    p=BluetoothPresence({main,service},true,1008);assert(ConnectionPresence(p,d)==Presence::Absent); // same name/service isn't root identity
    std::cout<<"PRESENCE_MODEL_TESTS_PASSED (exact HID sources, same PID/name isolation, no PID guesses, cable/BT roles, cached samples, PnP roots/service/adapter/conflicts)\n";
}
