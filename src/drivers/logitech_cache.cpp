#include "logitech_cache.h"
#include "../battery_lifecycle.h"

void PollLogitechCache(PollContext& ctx) {
    // Do not read/write the old PID-keyed 3-day registry cache: it has no
    // physical identity and its write-throttled timestamp is not last-live time.
    // Retain the module as a final validation boundary for Logitech observations.
    for (auto& s : ctx.out) {
        if (!s.receiverBacked) continue;
        s.graceSec = (int)(draybattery::kReceiverDisplayTtlMs / 1000);
        if (s.asleep || !s.connected || !s.lastLiveMs || s.percent < 0 || s.percent > 100) {
            s.percent = -1;
            s.lastLiveMs = 0;
            s.charging = false;
            s.asleep = true;
        }
    }
}
