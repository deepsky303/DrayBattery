#pragma once
// Faster, bounded device-discovery retries after application start.
// Wireless receiver battery reports can be briefly unavailable even when
// the keyboard or mouse itself is working normally.
// After one minute, return to the original low-impact 60-second polling.
#include <cstdint>

namespace draybattery {
constexpr std::uint32_t PollIntervalMs(unsigned successfulTimedWaits) {
    return successfulTimedWaits == 0 ? 3000u
         : successfulTimedWaits == 1 ? 8000u
         : successfulTimedWaits == 2 ? 16000u
         : successfulTimedWaits == 3 ? 30000u
         : 60000u;
}

// In addition to startup retries, retry a currently present HID++ Logitech
// receiver that failed to produce a battery report. Persistent misses back
// off to 30 seconds (not an endless busy loop).
constexpr std::uint32_t MissingReceiverRetryMs(unsigned consecutiveMissingPolls) {
    return consecutiveMissingPolls <= 1 ? 3000u
         : consecutiveMissingPolls == 2 ? 7000u
         : consecutiveMissingPolls == 3 ? 15000u
         : 30000u;
}
}
