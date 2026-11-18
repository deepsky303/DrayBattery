#include "../src/logical_identity.h"
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
    // Legacy cached records lack an ID; one uniquely matched active device can
    // safely suppress one stale receiver icon in the current snapshot.
    auto oldCache=kbdWifi;
    oldCache.physicalId.clear();
    auto legacy=draybattery::MergeLogicalDevices({oldCache,kbdUsb});
    assert(legacy.size()==1 && legacy[0].percent==32);

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

    // Wolverine's two vendor modes share a physical identity and one icon.
    auto gamePc=Make(L"wolverine:1532:0A4C",L"Razer Wolverine V3 Pro",
                     L"razer:wolverine-v3pro:primary",DeviceKind::Gamepad,97,false);
    auto gameXbox=Make(L"wolverine:1532:0A3F",L"Razer Wolverine V3 Pro",
                       L"razer:wolverine-v3pro:primary",DeviceKind::Gamepad,94,true);
    auto mergedGame=draybattery::MergeLogicalDevices({gameXbox,gamePc});
    assert(mergedGame.size()==1 && mergedGame[0].percent==97);
    assert(mergedGame[0].key==L"physical:razer:wolverine-v3pro:primary");
    std::cout<<"PASS: physical identity, two USB transports, legacy cache, unique units.\n";
}
