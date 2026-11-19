#include "../src/logical_identity.h"
#include "../src/drivers/ghub_status.h"
#include <cassert>
#include <iostream>

DeviceStatus Make(const wchar_t* key,const wchar_t* name,
                  const wchar_t* id,DeviceKind kind,int pct,
                  bool asleep=false,bool charge=false) {
    DeviceStatus s;
    s.key=key;
    s.name=name;
    if(id)s.physicalId=id;
    s.kind=kind;
    s.percent=pct;
    s.asleep=asleep;
    s.charging=charge;
    s.via=asleep?L"Logitech battery (last known)":L"Logitech G HUB (local)";
    return s;
}
int main(){
    assert(draybattery::CanonicalModelName(L"  Logitech G903  LS \t") ==
           draybattery::CanonicalModelName(L"Logitech G903 LIGHTSPEED"));
    auto kbdUsb=Make(L"logi:C356:1",L"Logitech G915 X LIGHTSPEED",
                     L"logitech:KEYBOARD.g915_x_ls.0.2398464588",DeviceKind::Keyboard,32,false,true);
    auto kbdWifi=Make(L"logi:C547:1",L"Logitech G915 X LS",
                      L"logitech:KEYBOARD.g915_x_ls.0.2398464588",DeviceKind::Keyboard,4,true,false);
    for(auto input:{std::vector<DeviceStatus>{kbdUsb,kbdWifi},
                    std::vector<DeviceStatus>{kbdWifi,kbdUsb}}){
        auto result=draybattery::MergeLogicalDevices(input);
        assert(result.size()==1);
        assert(result[0].percent==32 && !result[0].asleep && result[0].charging);
        assert(result[0].sourceKeys.size()==2);
        assert(result[0].key==L"physical:logitech:KEYBOARD.g915_x_ls.0.2398464588");
    }
    // Legacy cache cannot prove identity from a model name alone.
    auto oldCache=kbdWifi;
    oldCache.physicalId.clear();
    auto legacy=draybattery::MergeLogicalDevices({oldCache,kbdUsb});
    assert(legacy.size()==2);

    // Two distinct physically identifiable same-model devices never collapse.
    auto otherUnit=kbdUsb;
    otherUnit.key=L"logi:another";
    otherUnit.physicalId=L"logitech:KEYBOARD.g915_x_ls.0.DIFFERENT";
    auto two=draybattery::MergeLogicalDevices({kbdUsb,otherUnit});
    assert(two.size()==2);

    // Without reliable ID, two live identical-name units remain separate.
    auto anonymous=kbdUsb;
    anonymous.physicalId.clear();
    auto anonymousTwo=anonymous;
    anonymousTwo.key=L"logi:receiver2";
    assert(draybattery::MergeLogicalDevices({anonymous,anonymousTwo}).size()==2);

    // Sources sharing an independently verified unit ID have one icon.
    auto gamePc=Make(L"wolverine:1532:0A4C",L"Razer Wolverine V3 Pro",
                     L"razer:verified-unit-A",DeviceKind::Gamepad,97,false);
    auto gameXbox=Make(L"wolverine:1532:0A3F",L"Razer Wolverine V3 Pro",
                       L"razer:verified-unit-A",DeviceKind::Gamepad,94,true);
    auto mergedGame=draybattery::MergeLogicalDevices({gameXbox,gamePc});
    assert(mergedGame.size()==1 && mergedGame[0].percent==97);
    assert(mergedGame[0].key==L"physical:razer:verified-unit-A");
    // Cross-poll source transition: 42% from HID++ must not linger next
    // to the active 41% G HUB reading of the same G903 physical device.
    auto oldHid = Make(L"logi:C539:1",L"Logitech G903 LIGHTSPEED",
        nullptr,DeviceKind::Mouse,42,false);
    oldHid.via=L"Logitech receiver";
    oldHid.sourceKeys={L"logi:C539:1"};
    oldHid.connectionId=L"receiver-A";
    auto newHub=Make(L"physical:logitech:MOUSE.g903_hero.0.1680857627",
        L"Logitech G903 LIGHTSPEED",
        L"logitech:MOUSE.g903_hero.0.1680857627",DeviceKind::Mouse,41);
    newHub.sourceKeys={L"logi:C539:1"};
    newHub.connectionId=L"receiver-A";
    assert(draybattery::SupersededPreviousEntry(oldHid, {newHub}));
    auto asleepHid=oldHid;
    asleepHid.asleep=true;
    assert(!draybattery::SupersededPreviousEntry(newHub, {asleepHid}));
    // No shared hardware alias: identical model alone must never hide a mouse.
    auto otherReceiver=oldHid;
    otherReceiver.key=L"logi:C540:1";
    otherReceiver.sourceKeys={L"logi:C540:1"};
    otherReceiver.connectionId=L"receiver-B";
    assert(!draybattery::SupersededPreviousEntry(otherReceiver, {newHub}));
    // Even a shared receiver alias cannot override distinct verified units.
    auto anotherUnit=oldHid;
    anotherUnit.physicalId=L"logitech:MOUSE.g903_hero.0.other_unit";
    assert(!draybattery::SupersededPreviousEntry(anotherUnit, {newHub}));
    // Transport fallback also removes a ghost with physical ID when a live
    // anonymous HID++ reading has the same receiver slot and device model.
    auto newHid=oldHid;
    newHid.percent=39;
    auto oldHub=newHub;
    assert(draybattery::SupersededPreviousEntry(oldHub, {newHid}));
    // Stale or disconnected replacement is not a valid reason to discard cache.
    newHub.asleep=true;
    assert(!draybattery::SupersededPreviousEntry(oldHid, {newHub}));
    newHub.asleep=false;
    newHub.connected=false;
    assert(!draybattery::SupersededPreviousEntry(oldHid, {newHub}));

    // G HUB reports NOT_CONNECTED for offline hardware, unlike SLEEPING.
    assert(draybattery::ghub::ParseDeviceState("ACTIVE").connected);
    assert(draybattery::ghub::ParseDeviceState("CONNECTED").connected);
    assert(draybattery::ghub::ParseDeviceState("SLEEPING").connected);
    assert(draybattery::ghub::ParseDeviceState("SLEEPING").asleep);
    const auto notConnected=draybattery::ghub::ParseDeviceState("NOT_CONNECTED");
    assert(notConnected.known && !notConnected.connected && !notConnected.asleep);
    assert(!draybattery::ghub::ParseDeviceState("UNKNOWN").known);

    // Explicit disconnected device invalidates its old ghost entry.
    auto gone=newHub;
    gone.connected=false;
    gone.asleep=false;
    gone.percent=-1;
    assert(draybattery::InvalidatedByExplicitDisconnect(newHub,{gone}));
    assert(draybattery::InvalidatedByExplicitDisconnect(oldHid,{gone}));
    // An unrelated receiver and a different physical unit are unaffected.
    assert(!draybattery::InvalidatedByExplicitDisconnect(otherReceiver,{gone}));
    assert(!draybattery::InvalidatedByExplicitDisconnect(anotherUnit,{gone}));
    gone.asleep=true;
    assert(!draybattery::InvalidatedByExplicitDisconnect(newHub,{gone}));
    gone.asleep=false;
    gone.connected=true;
    assert(!draybattery::InvalidatedByExplicitDisconnect(newHub,{gone}));

    std::cout<<"PASS: physical identity, two USB transports, legacy cache, unique units, explicit disconnect.\n";
}
