// Exact production PnP reduction + Windows battery conversion + Add/merge/lifecycle/UI.
// All nodes/reports are simulated. No Bluetooth/device API calls.
#include "../src/drivers/drivers.h"
#include "../src/windows_bluetooth_battery.h"
#include "../src/battery_lifecycle.h"
#include "../src/battery_view.h"
#include <cassert>
#include <cstdio>
using namespace draybattery;
constexpr std::uint64_t start = 100000;
BluetoothNode Root(const wchar_t* container = L"unit-a", int pct = 9) {
    return {L"BTHENUM\\DEV_112233445566\\root", container, L"Same keyboard", Presence::Present, pct, 0x540};
}
DevicePoll Poll(std::vector<BluetoothNode> nodes, std::uint64_t at = start, bool complete = true,
                std::vector<DeviceStatus> vendor = {}) {
    DevicePoll p;
    p.presence = BluetoothPresence(nodes, complete, at);
    PollContext ctx;
    ctx.out = std::move(vendor);
    for (auto s : WindowsBluetoothBatteries(nodes, p.presence)) ctx.Add(s, 0, 0);
    p.devices = MergeLogicalDevices(ctx.out);
    return p;
}
const BatteryEntry& One(const BatteryLifecycle& state) {
    assert(state.Entries().size() == 1);
    return state.Entries().begin()->second;
}
void WindowsDim(const BatteryEntry& e, int pct) {
    assert(e.lastPct == pct && e.st.percent == pct && e.lastLiveMs == 0 && e.st.lastLiveMs == 0);
    assert(e.st.linkKind == LinkKind::Bluetooth && e.st.windowsBatteryProperty && e.st.batteryCached);
    assert(!e.st.batteryFresh && !e.st.charging && e.st.asleep);
    assert(VisibleBattery(e.st, {}) && DimmedBattery(e.st) && !ChargingBattery(e.st) && !LowBattery(e.st));
    assert(BatteryStatusText(e.st).find(L"last reported by Windows, not live") != std::wstring::npos);
    assert(BatteryStatusText(e.st).find(L"asleep") == std::wstring::npos);
}
int main() {
    {
        // Real Mi Earphones scenario: app starts while paired headphones are off.
        // Presence-only polls see the disconnected root before any battery property.
        BatteryLifecycle late;
        auto mi = Root(L"late-mi", 100);
        mi.connection = Presence::Absent;
        late.Apply(Poll({mi}, start), start);
        late.Apply(Poll({mi}, start + 2000), start + 2000);
        assert(late.Entries().empty());
        mi.connection = Presence::Present;
        late.Apply(Poll({mi}, start + 3000), start + 3000);
        WindowsDim(One(late), 100);
        // Independent presence worker publishes battery snapshots as partial BT scopes.
        auto partial = Poll({mi}, start + 4000);
        partial.complete = false;
        partial.polledConnections = partial.presence.btConnected;
        late.Apply(partial, start + 4000);
        WindowsDim(One(late), 100);
    }
    {
        // A newer Windows battery timestamp permits the same percentage on reconnect.
        BatteryLifecycle reconnect;
        auto mi = Root(L"timestamp-mi", 100);
        mi.batteryReportedFiletime = 1000000;
        reconnect.Apply(Poll({mi}, start), start);
        mi.connection = Presence::Absent;
        reconnect.Apply(Poll({mi}, start + 1), start + 1);
        mi.connection = Presence::Present;
        reconnect.Apply(Poll({mi}, start + 2), start + 2);
        assert(reconnect.Entries().empty()); // unchanged old report is still blocked
        ++mi.batteryReportedFiletime;
        reconnect.Apply(Poll({mi}, start + 3), start + 3);
        WindowsDim(One(reconnect), 100);
        mi.percent = 50; --mi.batteryReportedFiletime;
        reconnect.Apply(Poll({mi}, start + 4), start + 4);
        WindowsDim(One(reconnect), 100); // delayed older OS report cannot roll back
        auto child = mi; child.instance = L"BTHENUM\\{hfp}\\child";
        child.percent = 90; child.batteryReportedFiletime += 2;
        const auto latest = Poll({mi, child}, start + 5);
        assert(latest.devices.size() == 1 && latest.devices.front().percent == 90);
    }
    auto root = Root();
    auto p = Poll({root});
    assert(p.devices.size() == 1 && !p.devices[0].batteryFresh && p.devices[0].lastLiveMs == 0);
    {
        // Generic BT HID getters are cached too. They must neither mask a usable
        // Windows property during merge nor manufacture an initial live battery.
        HidColl c;c.path=L"generic-hid";c.connectionId=L"container:unit-a";c.transport=LinkKind::Bluetooth;
        DeviceStatus cached;cached.key=L"hid:1234:5678";cached.percent=99;cached.batteryCached=true;
        PollContext ctx;ctx.Add(cached,0x1234,0x5678,&c);
        auto combined=Poll({root},start,true,ctx.out);
        assert(combined.devices.size()==1 && combined.devices[0].windowsBatteryProperty);
        BatteryLifecycle initial;initial.Apply(combined,start);WindowsDim(One(initial),9);
        auto property=WindowsBluetoothBatteries({root},combined.presence).front();
        assert(MergeLogicalDevices({property,ctx.out[0]})[0].windowsBatteryProperty);
        combined.devices=ctx.out;initial=BatteryLifecycle{};initial.Apply(combined,start);
        assert(initial.Entries().empty());
    }
    BatteryLifecycle state;
    state.Apply(p, start);
    WindowsDim(One(state), 9); // regression: valid initial Windows property IS visible, DIM
    const auto key = state.Entries().begin()->first;
    assert(!VisibleBattery(One(state).st, {key}));

    root.percent = -1; // property vanishes while exact root remains connected
    for (auto elapsed : {90000ull, 1800000ull, 12 * 3600000ull, 30 * 24 * 3600000ull}) {
        const auto at = start + elapsed;
        state.Apply(Poll({root}, at), at);
        WindowsDim(One(state), 9);
        assert(!state.Expire(at + 1));
    }
    auto at = start + 30 * 24 * 3600000ull + 10;
    // A stalled or incomplete inventory never turns 25 seconds into disconnection.
    assert(state.Expire(at + 60000)); WindowsDim(One(state), 9);
    assert(One(state).st.presenceUncertain);
    state.Apply(Poll({}, at + 120000, false), at + 120000);
    WindowsDim(One(state), 9);
    root.connection = Presence::Absent;
    at += 180000;
    state.Apply(Poll({root}, at), at); assert(state.Entries().empty());
    root.connection = Presence::Present;
    ++at; state.Apply(Poll({root}, at), at); assert(state.Entries().empty()); // no property
    root.percent = 9;
    ++at; state.Apply(Poll({root}, at), at); assert(state.Entries().empty()); // same undated cache is not new
    auto relocated=root;relocated.instance=L"BTHENUM\\{service}\\new-child";relocated.percent=8;
    root.percent=-1;
    ++at;state.Apply(Poll({root,relocated},at),at);assert(state.Entries().empty()); // cache on another service isn't a new sample
    root.percent = 8;
    ++at; state.Apply(Poll({root}, at), at); WindowsDim(One(state), 8); // changed Windows property, still DIM
    auto delayed=Poll({Root(L"unit-a",99)},at-1);
    state.Apply(delayed,at);WindowsDim(One(state),8); // older queued property cannot roll the value back

    // A true vendor report for this exact container supersedes the Windows cache
    // regardless of order, model spelling, source key, or cached percent.
    HidColl bt;
    bt.path = L"vendor:bt"; bt.connectionId = L"container:unit-a"; bt.transport = LinkKind::Bluetooth;
    DeviceStatus live;
    live.key = L"vendor-bt"; live.name = L"Different protocol name";
    live.kind = DeviceKind::Keyboard; live.percent = 7; live.charging = true;
    live.lastLiveMs = ++at;
    PollContext ctx;
    ctx.Add(live, 0x1234, 0x5678, &bt);
    live = ctx.out.front();
    p = Poll({root}, at, true, {live});
    assert(p.devices.size() == 1 && p.devices[0].batteryFresh && !p.devices[0].batteryCached);
    state.Apply(p, at);
    assert(state.Entries().size() == 1 && state.Entries().begin()->first == key);
    assert(One(state).lastPct == 7 && !DimmedBattery(One(state).st) && ChargingBattery(One(state).st));
    assert(!One(state).st.windowsBatteryProperty && One(state).lastLiveMs == at);
    auto windows = WindowsBluetoothBatteries({root}, p.presence).front();
    assert(MergeLogicalDevices({windows, live}).size() == 1);
    assert(MergeLogicalDevices({windows, live})[0].percent == 7);
    root.percent = 99;
    ++at; state.Apply(Poll({root}, at), at);
    assert(One(state).lastPct == 7 && DimmedBattery(One(state).st) && !ChargingBattery(One(state).st));
    assert(BatteryStatusText(One(state).st).find(L"last real sample, not live") != std::wstring::npos);

    // Initial paired-only/unknown roots, service-only flags, and host adapters NEVER seed.
    for (auto connection : {Presence::Unknown, Presence::Absent}) {
        auto paired = Root(); paired.connection = connection;
        assert(Poll({paired}).devices.empty());
    }
    auto service = Root(); service.instance = L"BTHENUM\\{service}\\child";
    auto adapter = Root(); adapter.instance = L"USB\\VID_8087&PID_0000\\host-radio";
    assert(Poll({service, adapter}).devices.empty());
    auto missingRoot = Root(); missingRoot.instance = L"BTHENUM\\DEV_112233445566extra\\bad";
    assert(Poll({missingRoot}).devices.empty());
    auto noBattery = Root(); noBattery.percent = -1;
    assert(Poll({noBattery, adapter}).devices.empty()); // adapter's % cannot leak into peripheral
    assert(Poll({noBattery, service}).devices.size() == 1); // property on exact-container service is allowed
    auto wrongService = service; wrongService.container = L"other";
    assert(Poll({noBattery, wrongService}).devices.empty());
    for (int invalid : {-1, 101, 255}) { auto n = Root(L"unit-a", invalid); assert(Poll({n}).devices.empty()); }

    // Dual-mode genuine classic+LE roots: mixed evidence = Unknown, no removal.
    auto classic = Root(), le = Root();
    le.instance = L"BTHLE\\DEV_112233445566\\root";
    for (auto connection : {Presence::Present, Presence::Absent, Presence::Unknown}) {
        le.connection = connection;
        auto evidence = BluetoothPresence({classic, le}, true, at);
        assert(ConnectionPresence(evidence, live) == (connection == Presence::Present ? Presence::Present : Presence::Unknown));
        auto before = One(state).lastPct;
        ++at; state.Apply(Poll({classic, le}, at), at);
        assert(One(state).lastPct == before);
    }
    classic.connection = Presence::Absent; le.connection = Presence::Present;
    assert(Poll({classic, le}, at).presence.btUnknown.size() == 1); // reverse conflict
    le.connection = Presence::Absent;
    ++at; state.Apply(Poll({classic, le}, at), at); assert(state.Entries().empty());

    // Multiple same-name/same-model containers cannot share levels or removal state.
    BatteryLifecycle multi;
    auto a = Root(L"unit-a", 0), b = Root(L"unit-b", 87);
    multi.Apply(Poll({a, b}), start); assert(multi.Entries().size() == 2);
    b.connection = Presence::Absent;
    multi.Apply(Poll({a, b}, start + 1), start + 1); WindowsDim(One(multi), 0);
    b.connection = Presence::Present; b.percent = -1;
    multi.Apply(Poll({a, b}, start + 2), start + 2); WindowsDim(One(multi), 0);
    // A delayed old generation cannot recreate either Windows or live entries.
    auto old = Poll({Root(L"unit-b", 87)}, start + 3);
    assert(!multi.Apply(old, start + 3, 1)); WindowsDim(One(multi), 0);
    std::puts("WINDOWS_BLUETOOTH_TESTS_PASSED (initial DIM, per-root proof, vanished property, days, disconnect/reconnect, stale cache, live priority, UI, service/adapter, dual mode, containers, generation)");
}
