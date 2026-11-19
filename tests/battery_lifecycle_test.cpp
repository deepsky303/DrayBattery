#include "../src/battery_lifecycle.h"
#include "../src/drivers/ghub_status.h"
#include <cassert>
#include <iostream>

using draybattery::BatteryLifecycle;
constexpr std::uint64_t t0 = 100000;
constexpr auto ttl = draybattery::kReceiverDisplayTtlMs;

DeviceStatus Live(const wchar_t* link = L"receiver-A", const wchar_t* id = L"unit-A",
                  int percent = 42, std::uint64_t time = t0,
                  DeviceKind kind = DeviceKind::Mouse) {
    DeviceStatus s;
    s.connectionId = link;
    s.key = L"logi:" + s.connectionId + L":1";
    s.sourceKeys = {s.key};
    s.name = kind == DeviceKind::Keyboard ? L"Logitech G915 X LS" : L"Logitech G903 LS";
    s.physicalId = id;
    s.kind = kind;
    s.receiverBacked = true;
    s.percent = percent;
    s.lastLiveMs = time;
    s.graceSec = 1800; // cannot override the central 90-second policy
    return s;
}
DevicePoll Snapshot(std::vector<DeviceStatus> devices,
                    std::vector<std::wstring> present = {L"receiver-A"}) {
    DevicePoll p;
    p.devices = std::move(devices);
    p.presentConnections = std::move(present);
    return p;
}
DeviceStatus State(DeviceStatus s, const char* state, const char* json = nullptr,
                   std::uint64_t now = t0) {
    ghubjson::Value battery;
    if (json) assert(ghubjson::Parser(json).parse(battery));
    draybattery::ghub::ApplyBatteryState(s, draybattery::ghub::ParseDeviceState(state),
                                       json ? &battery : nullptr, now);
    s.ghub = true;
    return s;
}
const draybattery::BatteryEntry& Only(const BatteryLifecycle& state) {
    assert(state.Entries().size() == 1);
    return state.Entries().begin()->second;
}

int main() {
    static_assert(ttl == 90000, "exact default TTL");
    assert(draybattery::WithinReceiverDisplayTtl(t0, t0 + ttl - 1));
    assert(!draybattery::WithinReceiverDisplayTtl(t0, t0 + ttl));
    assert(!draybattery::WithinReceiverDisplayTtl(0, t0));
    assert(!draybattery::WithinReceiverDisplayTtl(t0 + 1, t0));
    const auto mouse = Live();
    {
        BatteryLifecycle state;
        state.Apply(Snapshot({mouse}), t0);
        assert(!Only(state).st.asleep && Only(state).st.percent == 42);
        auto sleeping = State(mouse, "SLEEPING", "{\"percentage\":100,\"charging\":true}");
        state.Apply(Snapshot({sleeping}), t0 + 10000);
        assert(Only(state).st.asleep && Only(state).st.percent == 42);
        assert(!Only(state).st.charging && Only(state).lastLiveMs == t0);
        assert(state.NextExpiryMs(t0 + 10000) == 80000);
        state.Apply(Snapshot({sleeping}), t0 + ttl - 1);
        assert(Only(state).lastLiveMs == t0 && Only(state).lastPct == 42);
        assert(state.NextExpiryMs(t0 + ttl - 1) == 1);
        // Expiration works without another worker result, including a stalled poll.
        assert(state.Expire(t0 + ttl));
        assert(state.Entries().empty());
        state.Apply(Snapshot({sleeping}), t0 + ttl + 10000);
        assert(state.Entries().empty());
    }
    {
        BatteryLifecycle state;
        state.Apply(Snapshot({mouse}), t0);
        auto transient = State(mouse, "NOT_CONNECTED", "{\"percentage\":100}");
        for (auto elapsed : {1000u, 5000u, 40000u, 89999u}) {
            state.Apply(Snapshot({transient}), t0 + elapsed);
            assert(Only(state).st.asleep && Only(state).st.percent == 42);
            assert(Only(state).lastLiveMs == t0);
        }
        state.Apply(Snapshot({transient}), t0 + ttl);
        assert(state.Entries().empty());
    }
    {
        BatteryLifecycle state;
        auto hid = mouse;
        hid.physicalId.clear();
        state.Apply(Snapshot({hid}), t0);
        state.Apply(Snapshot({State(mouse, "NOT_CONNECTED")}), t0 + 1000);
        const auto key = state.Entries().begin()->first;
        auto hub = State(mouse, "ACTIVE", "{\"percentage\":39}", t0 + 2000);
        hub.name = L"Logitech G903 LIGHTSPEED";
        state.Apply(Snapshot({hub}), t0 + 2000);
        assert(!Only(state).st.asleep && Only(state).st.percent == 39);
        assert(Only(state).lastLiveMs == t0 + 2000);
        assert(state.Entries().begin()->first == key);
        hid.percent = 38;
        hid.lastLiveMs = t0 + 3000;
        state.Apply(Snapshot({hid}), t0 + 3000);
        assert(!Only(state).st.asleep && Only(state).st.percent == 38);
        assert(Only(state).st.physicalId == mouse.physicalId);
        // Both sources in the same snapshot must still produce just one device.
        hub.lastLiveMs = t0 + 3000;
        state.Apply(Snapshot({hid, hub}), t0 + 3000);
        assert(state.Entries().size() == 1);
    }
    {
        BatteryLifecycle state;
        state.Apply(Snapshot({mouse}), t0);
        state.Apply(Snapshot({}, {}), t0 + 1);
        assert(state.Entries().empty()); // confirmed receiver removal: no grace
        state.Apply(Snapshot({mouse}, {}), t0 + 2);
        assert(state.Entries().empty()); // stale G HUB list cannot override inventory
        state.Apply(Snapshot({mouse}), t0 + 3);
        assert(state.RemoveConnection(L"receiver-A")); // immediate PnP removal path
        assert(state.Entries().empty());
    }
    {
        BatteryLifecycle state;
        state.Apply(Snapshot({mouse}), t0);
        auto uncertain = Snapshot({}, {});
        uncertain.inventoryComplete = false;
        state.Apply(uncertain, t0 + 1000);
        assert(Only(state).st.asleep && Only(state).lastLiveMs == t0);
        assert(state.Expire(t0 + ttl)); // uncertainty never extends TTL
    }
    {
        BatteryLifecycle restarted;
        auto stale = mouse;
        stale.asleep = true;
        stale.percent = 100; // legacy 3-day record / G HUB cached percentage
        stale.lastLiveMs = 0;
        stale.via = L"Logitech battery (last known)";
        restarted.Apply(Snapshot({stale}), t0);
        assert(restarted.Entries().empty());
        stale.asleep = false; // even a cache incorrectly labelled awake needs a timestamp
        restarted.Apply(Snapshot({stale}), t0);
        assert(restarted.Entries().empty());
        auto tooOld = Live(L"receiver-A", L"unit-A", 99, 1);
        restarted.Apply(Snapshot({tooOld}), t0);
        assert(restarted.Entries().empty());
    }
    {
        BatteryLifecycle state;
        auto other = Live(L"receiver-B", L"unit-B", 67);
        auto keyboard = Live(L"receiver-K", L"keyboard", 12, t0, DeviceKind::Keyboard);
        const std::vector<std::wstring> present{L"receiver-A", L"receiver-B", L"receiver-K"};
        state.Apply(Snapshot({mouse, other, keyboard}, present), t0);
        assert(state.Entries().size() == 3);
        state.Apply(Snapshot({State(mouse, "NOT_CONNECTED"), other, keyboard}, present), t0 + 1000);
        assert(state.Entries().size() == 3);
        size_t dim = 0;
        for (const auto& pair : state.Entries()) dim += pair.second.st.asleep;
        assert(dim == 1);
        // Same-name anonymous units on distinct hardware links also remain distinct.
        BatteryLifecycle anonymous;
        auto a = mouse, b = other;
        a.physicalId.clear(); b.physicalId.clear();
        anonymous.Apply(Snapshot({a, b}, present), t0);
        assert(anonymous.Entries().size() == 2);
    }
    {
        BatteryLifecycle state;
        auto keyboard = Live(L"receiver-K", L"keyboard", 12, t0, DeviceKind::Keyboard);
        state.Apply(Snapshot({mouse, keyboard}, {L"receiver-A", L"receiver-K"}), t0);
        DevicePoll partial = Snapshot({State(mouse, "SLEEPING")}, {L"receiver-A", L"receiver-K"});
        partial.complete = false;
        partial.polledConnections = {L"receiver-A"};
        state.Apply(partial, t0 + 1000);
        assert(state.Entries().size() == 2);
        for (const auto& pair : state.Entries()) {
            if (pair.second.st.kind == DeviceKind::Keyboard) assert(!pair.second.st.asleep);
        }
    }
    {
        BatteryLifecycle state;
        state.Apply(Snapshot({mouse}), t0);
        state.RemoveConnection(L"receiver-A");
        // An already queued WM_RESULTS payload must not resurrect a removed receiver.
        assert(!state.Apply(Snapshot({mouse}), t0 + 1, 1));
        assert(state.Entries().empty());
        auto fresh = Snapshot({Live(L"receiver-A", L"unit-A", 40, t0 + 2)});
        fresh.generation = 1;
        assert(state.Apply(fresh, t0 + 2, 1));
        assert(Only(state).lastPct == 40);
        assert(!state.Apply(fresh, t0 + 3, 1, true)); // producer/consumer shutdown gate
        auto older = Snapshot({Live(L"receiver-A", L"unit-A", 99, t0)});
        older.generation = 1;
        assert(state.Apply(older, t0 + 4, 1));
        assert(Only(state).lastPct == 40 && Only(state).st.percent == 40);
    }
    {
        BatteryLifecycle state;
        auto bt = mouse;
        bt.receiverBacked = false; bt.graceSec = 0;
        bt.key = L"bt:container"; bt.connectionId = L"container:bt";
        state.Apply(Snapshot({bt}, {}), t0);
        state.Apply(Snapshot({}, {}), t0 + 1);
        assert(state.Entries().empty()); // BT disconnect has no receiver grace
        auto invalid = State(mouse, "ACTIVE", "{\"percentage\":101}");
        state.Apply(Snapshot({invalid}), t0);
        assert(state.Entries().empty());
        auto removed = State(mouse, "ACTIVE", "{\"percentage\":100,\"isBatteryRemoved\":true}");
        state.Apply(Snapshot({removed}), t0);
        assert(state.Entries().empty());
        auto zero = State(mouse, "ACTIVE", "{\"percentage\":0}", t0);
        state.Apply(Snapshot({zero}), t0);
        assert(Only(state).st.percent == 0); // real zero is never invented 100%
    }
    std::cout << "BATTERY_LIFECYCLE_TESTS_PASSED (sleep, NOT_CONNECTED, wake, source switch, removal, TTL, restart, identity, keyboard, BT)\n";
}
