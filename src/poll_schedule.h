#pragma once
// Faster, bounded device-discovery retries after application start.
// Wireless receiver battery reports can be briefly unavailable even when
// the keyboard or mouse itself is working normally.
// After one minute, return to the original low-impact 60-second polling.
#include <cstdint>
#include "devices.h"
#include <algorithm>
#include <map>
#include <set>

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

// XInput adds an IG_00 virtual USB child with the same VID/PID. It is not a
// separate receiver and has no battery channel; schedule only exact USB roots.
inline std::vector<std::wstring> WolverineDiscoveryLinks(
    const std::vector<std::wstring>& present, const std::vector<std::wstring>& hid) {
    std::set<std::wstring> roots;
    for (const auto* list : {&present, &hid}) for (const auto& id : *list)
        if (id.rfind(L"usb\\vid_1532&pid_0a4c\\", 0) == 0) roots.insert(id);
    return {roots.begin(), roots.end()};
}

// A newly enumerated Wolverine can expose XInput before its battery HID channel
// is ready. Four scoped retries; ordinary periodic polls cannot restart the burst.
class DeviceDiscoveryRetries {
    struct Retry { std::uint64_t due; unsigned attempt = 0; };
    std::set<std::wstring> known_;
    std::map<std::wstring, Retry> pending_;
public:
    void Cancel() { pending_.clear(); }
    void Observe(const std::vector<std::wstring>& present, const std::vector<std::wstring>& reported,
                 std::uint64_t now, bool topologyChanged = false) {
        std::set<std::wstring> current(present.begin(), present.end());
        for (auto it = pending_.begin(); it != pending_.end();)
            if (!current.count(it->first)) it = pending_.erase(it); else ++it;
        for (const auto& id : current) {
            if (std::find(reported.begin(), reported.end(), id) != reported.end()) pending_.erase(id);
            else if (topologyChanged || !known_.count(id)) pending_[id] = {now + PollIntervalMs(0), 0};
        }
        known_ = std::move(current);
    }
    std::vector<std::wstring> Due(std::uint64_t now) const {
        std::vector<std::wstring> ids;
        for (const auto& pair : pending_) if (pair.second.due <= now) ids.push_back(pair.first);
        return ids;
    }
    void Complete(const std::vector<std::wstring>& queried, const std::vector<std::wstring>& reported,
                  std::uint64_t now) {
        for (const auto& id : queried) {
            auto it = pending_.find(id);
            if (it == pending_.end()) continue;
            if (std::find(reported.begin(), reported.end(), id) != reported.end() || ++it->second.attempt >= 4)
                pending_.erase(it);
            else it->second.due = now + PollIntervalMs(it->second.attempt);
        }
    }
    std::uint64_t Next() const {
        std::uint64_t result = 0;
        for (const auto& pair : pending_) if (!result || pair.second.due < result) result = pair.second.due;
        return result;
    }
};

inline std::uint32_t WorkerWaitMs(std::uint64_t now, std::uint64_t full,
                                 std::uint64_t hub, bool hasHub,
                                 std::uint64_t retry, bool hasRetry,
                                 std::uint64_t discovery = 0) {
    auto next = full;
    if (hasHub) next = std::min(next, hub);
    if (hasRetry) next = std::min(next, retry);
    if (discovery) next = std::min(next, discovery);
    return next > now ? (std::uint32_t)std::min<std::uint64_t>(next - now, 60000) : 1;
}
}
