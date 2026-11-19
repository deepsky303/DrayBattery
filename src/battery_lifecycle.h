#pragma once
// Pure session policy. Battery age NEVER expires a positively present link.
#include "logical_identity.h"
#include "physical_presence.h"
#include <cstdint>
#include <map>
#include <set>

namespace draybattery {
// A delayed verifier marks presence uncertain; ONLY confirmed absence removes a linked entry.
constexpr std::uint64_t kPresenceCheckMaxAgeMs = 10000;
constexpr std::uint64_t kUnlinkedSampleMaxAgeMs = 75000; // live-only fallback; never receiver/BT sleep retention
struct BatteryEntry {
    DeviceStatus st;
    int lastPct = -1;
    std::uint64_t lastLiveMs = 0, presenceMs = 0, uncertainSinceMs = 0;
};
class BatteryLifecycle {
    std::map<std::wstring, BatteryEntry> entries_;
    std::map<std::wstring, std::uint64_t> invalidAfter_;
    PhysicalPresence presence_;
    // A reconnect cannot replay the same undated Windows property. Keep the last
    // property signature even if it vanishes before disconnection. Session-only.
    std::map<std::wstring, std::pair<std::wstring, int>> windowsSeen_;
    std::map<std::wstring, std::pair<std::wstring, int>> windowsInvalid_;
    std::map<std::wstring, std::uint64_t> windowsObserved_;
    void Invalidate(const std::wstring& id, std::uint64_t at) {
        invalidAfter_[id] = std::max(invalidAfter_[id], at);
        const auto seen = windowsSeen_.find(id);
        if (seen != windowsSeen_.end()) windowsInvalid_[id] = seen->second;
    }
    static void Dim(BatteryEntry& e) {
        e.st.percent = e.lastPct;
        e.st.asleep = true; // display state, not a measured radio sleep state
        e.st.connected = true;
        e.st.charging = false;
        e.st.batteryFresh = false;
        e.st.lastLiveMs = e.lastLiveMs;
    }
    static std::uint64_t Deadline(const BatteryEntry& e) {
        return PresenceBacked(e.st) ? 0 : e.lastLiveMs + kUnlinkedSampleMaxAgeMs;
    }
public:
    const std::map<std::wstring, BatteryEntry>& Entries() const { return entries_; }
    bool Expire(std::uint64_t now) {
        bool changed = false;
        for (auto it = entries_.begin(); it != entries_.end();) {
            const auto deadline = Deadline(it->second);
            if (deadline && now >= deadline) {
                Invalidate(it->second.st.connectionId, now);
                it = entries_.erase(it); changed = true;
            } else {
                auto& e = it->second;
                if (PresenceBacked(e.st) && now >= e.presenceMs + kPresenceCheckMaxAgeMs && !e.st.presenceUncertain) {
                    e.st.presenceUncertain = true;
                    Dim(e);
                    changed = true;
                }
                ++it;
            }
        }
        return changed;
    }
    std::uint64_t NextExpiryMs(std::uint64_t now) const {
        std::uint64_t delay = 0;
        for (const auto& pair : entries_) {
            const auto deadline = Deadline(pair.second);
            const auto remaining = deadline > now ? deadline - now : 1;
            const auto check = pair.second.presenceMs + kPresenceCheckMaxAgeMs;
            const auto next = PresenceBacked(pair.second.st) ?
                (pair.second.st.presenceUncertain ? 0 : (check > now ? check - now : 1)) : remaining;
            if (!next) continue;
            if (!delay || next < delay) delay = next;
        }
        return delay;
    }
    bool RemoveConnection(const std::wstring& id, std::uint64_t now) {
        Invalidate(id, now);
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
        if (poll.presence.observedMs <= now && poll.presence.observedMs >= presence_.observedMs) presence_ = poll.presence;
        const auto& p = presence_;
        for (const auto& id : p.btDisconnected)
            if (!HasId(p.btConnected, id) && !HasId(p.btUnknown, id)) RemoveConnection(id, p.observedMs);
        for (auto it = entries_.begin(); it != entries_.end();) {
            auto& e = it->second;
            if (!PresenceBacked(e.st) || !p.observedMs || p.observedMs < e.presenceMs) { ++it; continue; }
            const auto presence = ConnectionPresence(p, e.st);
            if (presence == Presence::Absent) {
                Invalidate(e.st.connectionId, p.observedMs);
                it = entries_.erase(it);
                continue;
            }
            if (presence == Presence::Present) {
                e.presenceMs = p.observedMs;
                e.uncertainSinceMs = 0;
                e.st.presenceUncertain = false;
            } else if (!e.uncertainSinceMs) {
                e.uncertainSinceMs = std::min(now, e.presenceMs + kPresenceCheckMaxAgeMs);
                e.st.presenceUncertain = true;
                Dim(e);
            }
            ++it;
        }
        Expire(now);
        std::set<std::wstring> seen;
        for (auto s : MergeLogicalDevices(poll.devices)) {
            if (!poll.complete && !HasId(poll.polledConnections, s.connectionId)) continue;
            const bool backed = PresenceBacked(s);
            const auto presence = ConnectionPresence(p, s);
            if (backed && presence == Presence::Absent) {
                Invalidate(s.connectionId, p.observedMs);
                continue;
            }
            auto it = entries_.find(s.key);
            if (it != entries_.end() && !SamePhysicalDevice(it->second.st, s)) it = entries_.end();
            if (it == entries_.end()) {
                for (auto candidate = entries_.begin(); candidate != entries_.end(); ++candidate)
                    if (SamePhysicalDevice(candidate->second.st, s)) { it = candidate; break; }
            }
            const bool live = s.connected && !s.asleep && !s.batteryCached && s.batteryFresh &&
                s.percent >= 0 && s.percent <= 100 && s.lastLiveMs && s.lastLiveMs <= now &&
                (!backed || ((!invalidAfter_.count(s.connectionId) || s.lastLiveMs > invalidAfter_.at(s.connectionId)) &&
                 presence == Presence::Present && p.observedMs <= now && now - p.observedMs <= kPresenceCheckMaxAgeMs));
            const auto signature = std::make_pair(s.batteryPropertyId, s.percent);
            const auto blocked = windowsInvalid_.find(s.connectionId);
            const bool windows = s.linkKind == LinkKind::Bluetooth && s.windowsBatteryProperty &&
                s.batteryCached && s.connected && s.percent >= 0 && s.percent <= 100 &&
                !s.batteryPropertyId.empty() && s.batteryObservedMs && s.batteryObservedMs <= now &&
                (!windowsObserved_.count(s.connectionId) || s.batteryObservedMs >= windowsObserved_.at(s.connectionId)) &&
                presence == Presence::Present && p.observedMs <= now && now - p.observedMs <= kPresenceCheckMaxAgeMs &&
                (!invalidAfter_.count(s.connectionId) ||
                    (s.batteryObservedMs > invalidAfter_.at(s.connectionId) &&
                     blocked != windowsInvalid_.end() && blocked->second.first == signature.first &&
                     blocked->second.second != signature.second));
            if (windows) { windowsSeen_[s.connectionId] = signature; windowsObserved_[s.connectionId] = s.batteryObservedMs; }
            if (it == entries_.end() && !live && !windows) continue;
            if (!backed && !live) { if (it != entries_.end()) entries_.erase(it); continue; }
            if (it != entries_.end() && it->second.st.connectionId != s.connectionId && !live && !windows) continue;
            if (it == entries_.end()) it = entries_.emplace(s.key, BatteryEntry{}).first;
            auto& e = it->second;
            seen.insert(it->first);
            if (live && s.lastLiveMs < e.lastLiveMs) continue;
            if (s.physicalId.empty()) s.physicalId = e.st.physicalId;
            for (const auto& alias : e.st.sourceKeys)
                if (!HasId(s.sourceKeys, alias)) s.sourceKeys.push_back(alias);
            s.key = it->first; // preserve icon and hidden preference across source changes
            if (live) {
                e.lastPct = s.percent;
                e.lastLiveMs = s.lastLiveMs;
                e.st = std::move(s);
                e.st.windowsBatteryProperty = false;
                e.st.batteryCached = false;
            } else if (windows && !e.lastLiveMs) {
                e.lastPct = s.percent;
                s.lastLiveMs = 0; // reading a property is not a new measurement
                e.st = std::move(s);
                Dim(e);
            } else {
                if (backed) { e.st.physicalId = s.physicalId; e.st.sourceKeys = std::move(s.sourceKeys); }
                Dim(e); // never copy cached percent, charging or level over the real sample
            }
            if (backed && presence == Presence::Present) { e.presenceMs = p.observedMs; e.uncertainSinceMs = 0; }
        }
        for (auto it = entries_.begin(); it != entries_.end();) {
            auto& e = it->second;
            const bool touched = poll.complete || HasId(poll.polledConnections, e.st.connectionId);
            const bool missing = touched && !seen.count(it->first);
            if (missing && !PresenceBacked(e.st)) it = entries_.erase(it);
            else { if (missing) Dim(e); ++it; }
        }
        Expire(now);
        return true;
    }
};
} // namespace draybattery
