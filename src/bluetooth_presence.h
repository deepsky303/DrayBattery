#pragma once
// Deterministic PnP evidence reduction, shared by enumeration and tests.
#include "physical_presence.h"
#include <cwctype>
#include <map>

namespace draybattery {
inline std::wstring LowerId(std::wstring id) {
    for (auto& c : id) c = (wchar_t)std::towlower(c);
    return id;
}
inline bool BluetoothPeripheralRoot(std::wstring id) {
    id = LowerId(std::move(id));
    size_t start = id.rfind(L"bthenum\\dev_", 0) == 0 ? 12 :
                   id.rfind(L"bthle\\dev_", 0) == 0 ? 10 : 0;
    if (!start || id.size() <= start + 13 || id[start + 12] != L'\\') return false;
    for (size_t i = start; i < start + 12; ++i) if (!std::iswxdigit(id[i])) return false;
    return true;
}
struct BluetoothNode {
    std::wstring instance, container, name;
    Presence connection = Presence::Unknown;
    int percent = -1;
    unsigned classOfDevice = 0;
};
inline PhysicalPresence BluetoothPresence(const std::vector<BluetoothNode>& nodes,
                                         bool complete, std::uint64_t at) {
    struct Evidence { bool root = false, yes = false, no = false, unknown = false; };
    std::map<std::wstring, Evidence> groups;
    for (const auto& n : nodes) {
        if (n.container.empty()) continue;
        const auto inst = LowerId(n.instance);
        if (inst.rfind(L"bthenum\\", 0) && inst.rfind(L"bthle\\", 0) && inst.rfind(L"bthledevice\\", 0)) continue;
        auto& e = groups[LowerId(n.container)];
        if (!BluetoothPeripheralRoot(inst)) continue; // service flags never prove connectivity
        e.root = true;
        e.yes |= n.connection == Presence::Present;
        e.no |= n.connection == Presence::Absent;
        e.unknown |= n.connection == Presence::Unknown;
    }
    PhysicalPresence p;
    p.bluetoothComplete = complete;
    p.observedMs = at;
    for (const auto& pair : groups) {
        const auto id = L"container:" + pair.first;
        const auto& e = pair.second;
        if ((e.no && !e.yes && !e.unknown) || (!e.root && complete)) p.btDisconnected.push_back(id);
        else if (e.no) p.btUnknown.push_back(id); // classic/LE disagreement is not a disconnect
        else if (e.root && e.yes && !e.unknown) p.btConnected.push_back(id);
        else p.btUnknown.push_back(id);
    }
    return p;
}
} // namespace draybattery
