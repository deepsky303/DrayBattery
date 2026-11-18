#pragma once
// G HUB state is not a battery reading. NOT_CONNECTED must never be treated as
// SLEEPING just because G HUB remembers the last percentage.
#include <string_view>
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
} // namespace draybattery::ghub
