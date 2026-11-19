#pragma once
// G HUB state is not a battery reading. NOT_CONNECTED cannot create a sample;
// the lifecycle may retain a prior real sample while the exact receiver is present.
#include <string_view>
#include "../devices.h"
#include "ghub_json.h"
namespace draybattery::ghub {
struct DeviceState {
    bool known=false;
    bool connected=false;
    bool asleep=false;
};
inline DeviceState ParseDeviceState(std::string_view name) {
    if (name=="ACTIVE" || name=="CONNECTED") return {true,true,false};
    if (name=="SLEEPING") return {true,true,true};
    if (name=="NOT_CONNECTED") return {true,false,false};
    return {};
}
// G HUB's connectivity state describes the radio link, not receiver presence.
// Only ACTIVE/CONNECTED plus a successful battery GET is a live observation.
inline void ApplyBatteryState(DeviceStatus& s, DeviceState state,
                             const ghubjson::Value* battery, std::uint64_t now) {
    s.connected = true; // caller already matched the actual present receiver
    s.asleep = true;
    s.percent = -1;
    s.charging = false;
    s.lastLiveMs = 0;
    s.batteryFresh = false;
    if (!state.connected || state.asleep || !battery) return;
    const auto* removed = battery->get("isBatteryRemoved");
    if (removed && removed->type == ghubjson::Value::Boolean && removed->boolean) return;
    const auto* pct = battery->get("percentage");
    const int value = pct ? pct->integer() : -1;
    if (value < 0 || value > 100) return;
    s.percent = value;
    s.asleep = false;
    s.lastLiveMs = now;
    s.batteryFresh = true;
    const auto* charging = battery->get("charging");
    s.charging = charging && charging->type == ghubjson::Value::Boolean && charging->boolean;
}
} // namespace draybattery::ghub
