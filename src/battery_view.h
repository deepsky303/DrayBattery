#pragma once
#include "devices.h"
#include <set>

namespace draybattery {
inline bool HiddenBattery(const DeviceStatus& s, const std::set<std::wstring>& hidden) {
    if (hidden.count(s.key)) return true;
    for (const auto& alias : s.sourceKeys) if (hidden.count(alias)) return true;
    return false;
}
inline bool VisibleBattery(const DeviceStatus& s, const std::set<std::wstring>& hidden) {
    return s.connected && s.percent >= 0 && s.percent <= 100 && !HiddenBattery(s, hidden);
}
inline bool DimmedBattery(const DeviceStatus& s) { return s.asleep || s.percent < 0; }
inline bool ChargingBattery(const DeviceStatus& s) { return !DimmedBattery(s) && !s.batteryCached && s.charging; }
inline bool LowBattery(const DeviceStatus& s) {
    return !DimmedBattery(s) && !s.batteryCached && !s.charging && s.percent >= 0 &&
        (s.percent <= 15 || (s.approximate && (s.levelText == L"Low" || s.levelText == L"Empty")));
}
// Shared by tray, widget and diagnostic listing. Dim is a display state, not a
// hardware sleep diagnosis. Windows is labelled as the source without claiming a live acquisition.
inline std::wstring BatteryStatusText(const DeviceStatus& s, int fallback = -1) {
    if (!s.connected) return L"off / not connected";
    const auto pct = s.percent >= 0 ? s.percent : fallback;
    std::wstring text = pct >= 0 ?
        (s.levelText.empty() ? std::to_wstring(pct) + L"%" : s.levelText) : L"unknown";
    if (s.windowsBatteryProperty) text += DimmedBattery(s) ? L" (last Windows reading)" : L" (Windows)";
    else if (DimmedBattery(s) && pct >= 0) text += L" (last real sample, not live)";
    if (s.estimated && pct >= 0) text += L" (controller estimate)";
    if (ChargingBattery(s)) text += L", charging";
    if (s.presenceUncertain) text += L", connection unverified";
    return text;
}
} // namespace draybattery
