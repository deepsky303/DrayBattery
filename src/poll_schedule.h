#pragma once
// Faster, bounded device-discovery retries after application start.
// Wireless receiver battery reports can be briefly unavailable even when
// the keyboard or mouse itself is working normally.
// After one minute, return to the original low-impact 60-second polling.
#include <cstdint>
#include "devices.h"
#include <algorithm>

namespace draybattery {
constexpr std::uint32_t kGHubRefreshMs = 2000; // localhost only; no HID/other vendor probes
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

struct LogitechPollTargets {
    std::vector<std::wstring> ghub, retry;
    static bool Has(const std::vector<std::wstring>& ids, const std::wstring& id) {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    }
    void Observe(const std::vector<DeviceStatus>& devices, const std::vector<std::wstring>& scope) {
        for (const auto& id : scope) {
            bool hub = false, live = false;
            for (const auto& d : devices) if (d.connectionId == id) {
                hub |= d.ghub;
                live |= d.connected && !d.asleep && d.percent >= 0 && d.lastLiveMs != 0;
            }
            if (hub && !Has(ghub, id)) ghub.push_back(id);
            if (hub || live) retry.erase(std::remove(retry.begin(), retry.end(), id), retry.end());
            else if (!Has(ghub, id) && !Has(retry, id)) retry.push_back(id);
        }
    }
};

inline std::uint32_t WorkerWaitMs(std::uint64_t now, std::uint64_t full,
                                 std::uint64_t hub, bool hasHub,
                                 std::uint64_t retry, bool hasRetry) {
    auto next = full;
    if (hasHub) next = std::min(next, hub);
    if (hasRetry) next = std::min(next, retry);
    return next > now ? (std::uint32_t)std::min<std::uint64_t>(next - now, 60000) : 1;
}
}
