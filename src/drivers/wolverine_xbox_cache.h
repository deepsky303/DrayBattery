#pragma once
#include "drivers.h"

// Xbox Mode 1532:0A3F is not exposed as HID on Windows. The foreground-only
// WinRT GIP reader stores a validated reading in HKCU. This driver presents
// that reading while the physical Xbox controller is still connected.
bool IsWolverineXboxPresent();
bool SaveWolverineXboxReading(int rawValue, int charging);
void PollWolverineXbox(PollContext& ctx);
