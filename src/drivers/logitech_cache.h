#pragma once
#include "drivers.h"

// Legacy registry cache is deliberately ignored, without deleting user data.
// Last-known display state now lives only in BatteryLifecycle for this session.
// This final policy pass cannot synthesize a device or refresh a live timestamp.
void PollLogitechCache(PollContext& ctx);
