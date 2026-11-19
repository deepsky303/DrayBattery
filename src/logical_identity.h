#pragma once
// Generic identity resolution. Devices with the same verified physical ID
// are merged across transports. Devices with no such ID remain distinct,
// rather than accidentally merging different units of the same model.
#include "devices.h"
#include <algorithm>
#include <cwctype>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace draybattery {

inline std::wstring CanonicalModelName(std::wstring name) {
    for (wchar_t& c : name) c = (wchar_t)std::towupper(c);
    // Common Logitech naming abbreviation in HID++ vs G HUB:
    // "G915 X LS" and "G915 X LIGHTSPEED" are the same model family.
    // Collapse whitespace.
    std::wstring cleaned;
    for(wchar_t c : name) {
        if(std::iswspace(c)) {
            if(!cleaned.empty() && cleaned.back()!=L' ')cleaned+=L' ';
        } else cleaned+=c;
    }
    if(!cleaned.empty() && cleaned.back()==L' ')cleaned.pop_back();
    if (cleaned.size() > 3 && cleaned.compare(cleaned.size()-3, 3, L" LS") == 0)
        cleaned.replace(cleaned.size()-3, 3, L" LIGHTSPEED");
    return cleaned;
}

inline std::wstring LogicalKey(const DeviceStatus& s) {
    if (!s.physicalId.empty()) return L"physical:" + s.physicalId;
    return s.key;
}

inline int SourcePriority(const DeviceStatus& s) {
    int score = s.connected ? 1000 : 0;
    score += s.asleep ? 0 : 400;
    score += s.charging ? 100 : 0;
    score += (s.percent >= 0 && !s.approximate) ? 50 : 0;
    score += (s.via.find(L"G HUB") != std::wstring::npos) ? 20 : 0;
    score += (s.via.find(L"USB") != std::wstring::npos) ? 12 : 0;
    return score;
}

inline bool SamePhysicalDevice(const DeviceStatus& a, const DeviceStatus& b) {
    if (!a.physicalId.empty() && !b.physicalId.empty())
        return a.physicalId == b.physicalId;
    if (a.key == b.key) return true;
    if (a.kind != b.kind || CanonicalModelName(a.name) != CanonicalModelName(b.name)) return false;
    if (a.connectionId.empty() || a.connectionId != b.connectionId) return false;
    auto aliases = [](const DeviceStatus& s) {
        auto ids = s.sourceKeys;
        if (ids.empty()) ids.push_back(s.key);
        return ids;
    };
    const auto left = aliases(a), right = aliases(b);
    for (const auto& id : left)
        if (id.rfind(L"logi:", 0) == 0 &&
            std::find(right.begin(), right.end(), id) != right.end()) return true;
    return false;
}

// Compatibility helpers also use physical transport identity, never PID/model alone.
inline bool SupersededPreviousEntry(const DeviceStatus& previous,
                                    const std::vector<DeviceStatus>& current) {
    for (const auto& fresh : current)
        if (fresh.key != previous.key && fresh.connected && !fresh.asleep &&
            fresh.kind == previous.kind && SamePhysicalDevice(previous, fresh)) return true;
    return false;
}

inline bool InvalidatedByExplicitDisconnect(
    const DeviceStatus& previous, const std::vector<DeviceStatus>& current) {
    for (const auto& offline : current)
        if (!offline.connected && !offline.asleep && !offline.receiverBacked &&
            offline.kind == previous.kind && SamePhysicalDevice(previous, offline)) return true;
    return false;
}
inline std::vector<DeviceStatus> MergeLogicalDevices(
    const std::vector<DeviceStatus>& found) {
    std::vector<DeviceStatus> input = found;
    std::map<std::wstring, size_t> positions;
    std::vector<DeviceStatus> merged;
    std::vector<int> priorities;
    merged.reserve(input.size());
    priorities.reserve(input.size());
    for (const auto& status : input) {
        DeviceStatus s = status;
        std::wstring key = LogicalKey(s);
        const int priority = SourcePriority(s);
        if (s.sourceKeys.empty()) s.sourceKeys.push_back(s.key);
        s.key = key;
        auto it = positions.find(key);
        if (it == positions.end()) {
            for (size_t i = 0; i < merged.size(); ++i) {
                if (SamePhysicalDevice(merged[i], s)) {
                    key = merged[i].key;
                    s.key = key;
                    it = positions.find(key);
                    break;
                }
            }
        }
        if (it == positions.end()) {
            positions.emplace(key, merged.size());
            merged.push_back(std::move(s));
            priorities.push_back(priority);
        } else {
            auto& incumbent = merged[it->second];
            // Retain all original USB/Bluetooth/receiver keys even when one
            // current reading supersedes a sleeping receiver cache.
            std::vector<std::wstring> combined = incumbent.sourceKeys;
            for (const auto& alias : s.sourceKeys)
                if (std::find(combined.begin(), combined.end(), alias) == combined.end())
                    combined.push_back(alias);
            if (priority > priorities[it->second] ||
                (priority == priorities[it->second] && s.lastLiveMs > incumbent.lastLiveMs)) {
                if (s.physicalId.empty()) s.physicalId = incumbent.physicalId;
                s.sourceKeys = std::move(combined);
                incumbent = std::move(s);
                priorities[it->second] = priority;
            } else {
                if (incumbent.physicalId.empty()) incumbent.physicalId = s.physicalId;
                incumbent.sourceKeys = std::move(combined);
            }
        }
    }
    return merged;
}
} // namespace draybattery
