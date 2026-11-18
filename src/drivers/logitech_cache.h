#pragma once
#include "drivers.h"

// Store only last confirmed Logitech battery values under HKCU.
// When the receiver remains present but Logitech G HUB reports an asleep
// device without a battery endpoint, the cached level stays visible dimmed.
// No external files, drivers or network access are required.
void SaveLogitechBattery(USHORT receiverPid, const DeviceStatus& reading);
void PollLogitechCache(PollContext& ctx);
