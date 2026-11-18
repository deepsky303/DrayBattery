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
    if (name.size() > 3 && name.compare(name.size()-3,3,L" LS")==0)
        name.replace(name.size()-3,3,L" LIGHTSPEED");
    // Collapse whitespace.
    std::wstring cleaned;
    for(wchar_t c : name) {
        if(std::iswspace(c)) {
            if(!cleaned.empty() && cleaned.back()!=L' ')cleaned+=L' ';
        } else cleaned+=c;
    }
    if(!cleaned.empty() && cleaned.back()==L' ')cleaned.pop_back();
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

// Carefully migrate old cached Logitech records made before a physical
// identity was persisted. A stale receiver entry may be coalesced with one
// live device of the same normalized model only when unique in this snapshot.
// No two *live* devices are ever merged by their model name alone.
inline void MigrateLegacyLogitechCache(std::vector<DeviceStatus>& input) {
    for (auto& stale : input) {
        if (!stale.asleep || !stale.physicalId.empty() ||
            stale.via.find(L"last known") == std::wstring::npos ||
            stale.key.rfind(L"logi:", 0) != 0)
            continue;
        const std::wstring model = CanonicalModelName(stale.name);
        size_t match = input.size(), count = 0;
        for (size_t j = 0; j < input.size(); ++j) {
            const auto& candidate = input[j];
            if (&candidate == &stale || candidate.asleep ||
                candidate.physicalId.empty() ||
                candidate.kind != stale.kind ||
                candidate.key.rfind(L"logi:", 0) != 0)
                continue;
            if (CanonicalModelName(candidate.name) == model) {
                match = j;
                ++count;
            }
        }
        if (count == 1) {
            // Legacy-only: no persistent hardware ID was saved by old builds.
            // Do not discard data or merge concurrent live devices.
            stale.physicalId = input[match].physicalId;
        }
    }
}

inline std::vector<DeviceStatus> MergeLogicalDevices(
    const std::vector<DeviceStatus>& found) {
    std::vector<DeviceStatus> input = found;
    MigrateLegacyLogitechCache(input);
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
            if (priority > priorities[it->second]) {
                s.sourceKeys = std::move(combined);
                incumbent = std::move(s);
                priorities[it->second] = priority;
            } else {
                incumbent.sourceKeys = std::move(combined);
            }
        }
    }
    return merged;
}
} // namespace draybattery
