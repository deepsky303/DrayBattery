#pragma once
// Pure display policy shared by the UI and deterministic regression tests.
// No clock, registry, HID, windows, or external side effects.
#include "logical_identity.h"
#include <cstdint>
#include <map>
#include <set>

namespace draybattery {
constexpr std::uint64_t kReceiverDisplayTtlMs = 90000;
inline bool WithinReceiverDisplayTtl(std::uint64_t lastLive, std::uint64_t now) {
    return lastLive && now >= lastLive && now - lastLive < kReceiverDisplayTtlMs;
}

struct BatteryEntry {
    DeviceStatus st;
    int lastPct = -1;
    std::uint64_t lastLiveMs = 0;
    std::uint64_t lastSeenMs = 0; // non-Logitech legacy grace only
};

class BatteryLifecycle {
    std::map<std::wstring, BatteryEntry> entries_;
    static bool Contains(const std::vector<std::wstring>& ids, const std::wstring& id) {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    }
    static bool Expired(const BatteryEntry& e, std::uint64_t now) {
        return e.st.receiverBacked && !WithinReceiverDisplayTtl(e.lastLiveMs, now);
    }
    static void Dim(BatteryEntry& e) {
        e.st.percent = e.lastPct;
        e.st.asleep = true;
        e.st.charging = false;
        e.st.lastLiveMs = e.lastLiveMs;
    }
public:
    const std::map<std::wstring, BatteryEntry>& Entries() const { return entries_; }

    bool Expire(std::uint64_t now) {
        bool changed = false;
        for (auto it = entries_.begin(); it != entries_.end();) {
            if (Expired(it->second, now)) { it = entries_.erase(it); changed = true; }
            else ++it;
        }
        return changed;
    }
    std::uint64_t NextExpiryMs(std::uint64_t now) const {
        std::uint64_t delay = kReceiverDisplayTtlMs;
        bool any = false;
        for (const auto& pair : entries_) {
            const auto& e = pair.second;
            if (!e.st.receiverBacked) continue;
            any = true;
            const auto age = now >= e.lastLiveMs ? now - e.lastLiveMs : kReceiverDisplayTtlMs;
            delay = std::min(delay, age < kReceiverDisplayTtlMs ? kReceiverDisplayTtlMs - age : 1);
        }
        return any ? delay : 0;
    }
    bool RemoveConnection(const std::wstring& id) {
        bool changed = false;
        for (auto it = entries_.begin(); it != entries_.end();) {
            if (it->second.st.connectionId == id) { it = entries_.erase(it); changed = true; }
            else ++it;
        }
        return changed;
    }
    bool Apply(const DevicePoll& poll, std::uint64_t now,
               unsigned currentGeneration = 0, bool stopping = false) {
        if (stopping || poll.generation != currentGeneration) return false;
        // An incomplete OS enumeration cannot prove removal. A complete one can.
        if (poll.complete && poll.inventoryComplete) {
            for (auto it = entries_.begin(); it != entries_.end();) {
                const auto& s = it->second.st;
                if (s.receiverBacked && !Contains(poll.presentConnections, s.connectionId))
                    it = entries_.erase(it);
                else ++it;
            }
        }
        std::set<std::wstring> seen;
        for (auto s : MergeLogicalDevices(poll.devices)) {
            if (!s.connected) continue;
            if (s.receiverBacked && poll.inventoryComplete &&
                !Contains(poll.presentConnections, s.connectionId)) continue;
            auto it = entries_.find(s.key);
            if (it != entries_.end() && !SamePhysicalDevice(it->second.st, s))
                it = entries_.end();
            if (it == entries_.end()) {
                for (auto candidate = entries_.begin(); candidate != entries_.end(); ++candidate)
                    if (SamePhysicalDevice(candidate->second.st, s)) { it = candidate; break; }
            }
            const bool live = !s.asleep && s.percent >= 0 && s.percent <= 100 &&
                (!s.receiverBacked || (s.lastLiveMs && s.lastLiveMs <= now));
            // Cold-start SLEEPING/NOT_CONNECTED/legacy registry data cannot create an icon.
            if (it == entries_.end() && s.receiverBacked && !live) continue;
            if (it == entries_.end()) it = entries_.emplace(s.key, BatteryEntry{}).first;
            auto& e = it->second;
            if (s.receiverBacked && live && s.lastLiveMs < e.lastLiveMs) {
                seen.insert(it->first);
                continue; // an older in-flight source cannot overwrite a newer sample
            }
            // Enrich an anonymous HID++ slot with G HUB's verified device signature.
            if (s.physicalId.empty()) s.physicalId = e.st.physicalId;
            for (const auto& alias : e.st.sourceKeys)
                if (!Contains(s.sourceKeys, alias)) s.sourceKeys.push_back(alias);
            s.key = it->first; // preserve the UI identity/hidden choice across source changes
            if (live && (!s.receiverBacked || s.lastLiveMs >= e.lastLiveMs)) {
                e.lastPct = s.percent;
                e.lastLiveMs = s.receiverBacked ? s.lastLiveMs : now;
            }
            e.st = std::move(s);
            e.lastSeenMs = now;
            if (e.st.receiverBacked && !live) Dim(e);
            seen.insert(it->first);
        }
        for (auto it = entries_.begin(); it != entries_.end();) {
            auto& e = it->second;
            const bool touched = poll.complete || Contains(poll.polledConnections, e.st.connectionId);
            const bool missing = touched && !seen.count(it->first);
            const bool disconnected = missing && InvalidatedByExplicitDisconnect(e.st, poll.devices);
            const bool grace = e.st.receiverBacked ? !Expired(e, now) :
                e.st.graceSec > 0 && now >= e.lastSeenMs &&
                now - e.lastSeenMs < (std::uint64_t)e.st.graceSec * 1000;
            if (Expired(e, now) || disconnected || (missing && !grace)) {
                it = entries_.erase(it);
            } else {
                if (missing) Dim(e);
                ++it;
            }
        }
        return true;
    }
};
} // namespace draybattery
