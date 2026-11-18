#include "logitech_cache.h"
#include <algorithm>
#include <cstring>
#include <set>

namespace {
const wchar_t* kCacheKey = L"Software\\DrayBattery\\LogitechCache";
constexpr DWORD kCacheVersion = 1;
constexpr ULONGLONG kTicksPerSecond = 10000000ULL;
constexpr ULONGLONG kLifetime = 3ULL * 24 * 60 * 60 * kTicksPerSecond;
constexpr ULONGLONG kWriteInterval = 10ULL * 60 * kTicksPerSecond;

struct CachedBattery {
    DWORD version = 0;
    DWORD percent = 0;
    DWORD kind = 0;
    DWORD reserved = 0;
    ULONGLONG updatedAt = 0;
    wchar_t name[128]{};
};

ULONGLONG UtcNow() {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER n{};
    n.LowPart = ft.dwLowDateTime;
    n.HighPart = ft.dwHighDateTime;
    return n.QuadPart;
}
bool Valid(const CachedBattery& c, ULONGLONG now) {
    if (c.version != kCacheVersion || c.percent > 100 ||
        c.kind > (DWORD)DeviceKind::Gamepad ||
        c.name[0] == 0 || c.name[127] != 0 ||
        c.updatedAt == 0 || c.updatedAt > now)
        return false;
    return now - c.updatedAt <= kLifetime;
}
bool Read(USHORT pid, CachedBattery& c) {
    DWORD type = 0, bytes = sizeof(c);
    const std::wstring key = Hex4(pid);
    if (RegGetValueW(HKEY_CURRENT_USER, kCacheKey, key.c_str(), RRF_RT_REG_BINARY,
                     &type, &c, &bytes) != ERROR_SUCCESS ||
        type != REG_BINARY || bytes != sizeof(c))
        return false;
    return Valid(c, UtcNow());
}
void Write(USHORT pid, const CachedBattery& c) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kCacheKey, 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return;
    const std::wstring name = Hex4(pid);
    RegSetValueExW(key, name.c_str(), 0, REG_BINARY, (const BYTE*)&c, sizeof(c));
    RegCloseKey(key);
}
bool ReceiverPresent(const PollContext& ctx, USHORT pid) {
    for (const HidColl& h : ctx.colls)
        if (h.vid == 0x046D && h.pid == pid &&
            h.page() == 0xFF00 &&
            ((h.usage() == 0x0001 && h.outLen() == 7) ||
             (h.usage() == 0x0002 && h.outLen() == 20)))
            return true;
    return false;
}
bool ReceiverFromKey(const std::wstring& key, USHORT& pid) {
    if (key.size() < 10 || key.compare(0, 5, L"logi:") != 0 || key[9] != L':')
        return false;
    unsigned v = 0;
    for (int i = 5; i < 9; ++i) {
        wchar_t c = key[i];
        unsigned digit = (c >= L'0' && c <= L'9') ? c - L'0' :
                         (c >= L'A' && c <= L'F') ? c - L'A' + 10 :
                         (c >= L'a' && c <= L'f') ? c - L'a' + 10 : 16;
        if (digit >= 16) return false;
        v = (v << 4) | digit;
    }
    pid = (USHORT)v;
    return true;
}
} // namespace

void SaveLogitechBattery(USHORT pid, const DeviceStatus& status) {
    if (status.percent < 0 || status.percent > 100 ||
        status.asleep || !status.connected || status.name.empty())
        return;
    ULONGLONG now = UtcNow();
    CachedBattery previous{};
    if (Read(pid, previous) &&
        previous.percent == (DWORD)status.percent &&
        previous.kind == (DWORD)status.kind &&
        std::wstring(previous.name) == status.name &&
        now - previous.updatedAt < kWriteInterval)
        return; // Avoid writing the registry on every polling cycle.

    CachedBattery next{};
    next.version = kCacheVersion;
    next.percent = (DWORD)status.percent;
    next.kind = (DWORD)status.kind;
    next.updatedAt = now;
    wcsncpy_s(next.name, status.name.c_str(), _TRUNCATE);
    Write(pid, next);
}

void PollLogitechCache(PollContext& ctx) {
    // First update the cache from live direct HID++ reports. G HUB readings
    // are persisted as soon as they are accepted in PollGHub.
    for (const DeviceStatus& status : ctx.out) {
        USHORT pid = 0;
        if (ReceiverFromKey(status.key, pid) && ReceiverPresent(ctx, pid))
            SaveLogitechBattery(pid, status);
    }

    // Keep a powered-down/sleeping receiver-backed device represented as
    // last known, even after the app itself has been restarted.
    std::set<USHORT> receivers;
    for (const HidColl& h : ctx.colls)
        if (h.vid == 0x046D &&
            h.page() == 0xFF00 &&
            ((h.usage() == 0x0001 && h.outLen() == 7) ||
             (h.usage() == 0x0002 && h.outLen() == 20)))
            receivers.insert(h.pid);
    for (USHORT pid : receivers) {
        if (ctx.IsClaimed(0x046D, pid)) continue;
        CachedBattery c{};
        if (!Read(pid, c)) continue;
        DeviceStatus stale;
        stale.key = L"logi:" + Hex4(pid) + L":1";
        stale.name = c.name;
        stale.via = L"Logitech battery (last known)";
        stale.kind = (DeviceKind)c.kind;
        stale.percent = (int)c.percent;
        stale.asleep = true;
        stale.connected = true; // The dongle is physically present.
        stale.graceSec = 1800;
        stale.charging = false; // Never claim old charging state is current.
        ctx.Add(stale, 0x046D, pid);
    }
}
