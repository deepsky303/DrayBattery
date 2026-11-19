#pragma once
#include "devices.h"
#include <algorithm>
#include <cwctype>

namespace draybattery {
inline std::wstring UsbRemovalConnectionId(std::wstring path) {
    for (auto& c : path) c = (wchar_t)std::towlower(c);
    if (path.rfind(L"\\\\?\\", 0) == 0) path.erase(0, 4);
    std::replace(path.begin(), path.end(), L'#', L'\\');
    const auto guid = path.find(L"\\{");
    if (guid != std::wstring::npos) path.resize(guid);
    const auto split = path.find(L'\\', 4);
    if (path.rfind(L"usb\\vid_", 0) || split == std::wstring::npos || split + 1 == path.size() ||
        path.substr(0, split).find(L"&mi_") != std::wstring::npos) return L"";
    return path;
}
inline bool HasId(const std::vector<std::wstring>& ids, const std::wstring& id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}
inline bool PresenceBacked(const DeviceStatus& s) {
    return !s.connectionId.empty() &&
        (s.linkKind == LinkKind::UsbReceiver || s.linkKind == LinkKind::Bluetooth);
}
inline Presence ConnectionPresence(const PhysicalPresence& p, const DeviceStatus& s) {
    if (!PresenceBacked(s) || !p.observedMs) return Presence::Unknown;
    if (s.linkKind == LinkKind::UsbReceiver) {
        if (HasId(p.usbPresent, s.connectionId)) return Presence::Present;
        return p.usbComplete ? Presence::Absent : Presence::Unknown;
    }
    // Contradictory root evidence is uncertainty, never proven absence.
    if (HasId(p.btDisconnected, s.connectionId) && HasId(p.btConnected, s.connectionId)) return Presence::Unknown;
    if (HasId(p.btUnknown, s.connectionId)) return Presence::Unknown;
    if (HasId(p.btDisconnected, s.connectionId)) return Presence::Absent;
    if (HasId(p.btConnected, s.connectionId)) return Presence::Present;
    return p.bluetoothComplete ? Presence::Absent : Presence::Unknown;
}
} // namespace draybattery
