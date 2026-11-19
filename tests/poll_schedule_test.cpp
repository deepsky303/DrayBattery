#include "../src/poll_schedule.h"
#include <cassert>
#include <iostream>

int main() {
    using draybattery::PollIntervalMs;
    static_assert(PollIntervalMs(0) == 3000u, "first retry");
    static_assert(PollIntervalMs(1) == 8000u, "second retry");
    static_assert(PollIntervalMs(2) == 16000u, "third retry");
    static_assert(PollIntervalMs(3) == 30000u, "fourth retry");
    static_assert(PollIntervalMs(4) == 60000u, "normal interval");
    static_assert(PollIntervalMs(1000) == 60000u, "normal interval after long uptime");
    static_assert(draybattery::MissingReceiverRetryMs(1) == 3000u, "missing retry 1");
    static_assert(draybattery::MissingReceiverRetryMs(2) == 7000u, "missing retry 2");
    static_assert(draybattery::MissingReceiverRetryMs(3) == 15000u, "missing retry 3");
    static_assert(draybattery::MissingReceiverRetryMs(4) == 30000u, "ongoing recovery");
    static_assert(draybattery::MissingReceiverRetryMs(1000) == 30000u, "long uptime recovery");
    static_assert(draybattery::kGHubRefreshMs == 2000, "G HUB wake detection");
    for (unsigned i = 0; i < 1000; i++) {
        const auto ms = PollIntervalMs(i);
        assert(ms >= 3000u && ms <= 60000u);
    }
    // Fast G HUB refresh and missing-link retry do not move the full-device deadline.
    assert(draybattery::WorkerWaitMs(1000, 61000, 3000, true, 0, false) == 2000);
    assert(draybattery::WorkerWaitMs(1000, 61000, 3000, false, 4000, true) == 3000);
    assert(draybattery::WorkerWaitMs(1000, 61000, 3000, false, 4000, false) == 60000);
    assert(draybattery::WorkerWaitMs(61000, 61000, 0, false, 0, false) == 1);
    draybattery::LogitechPollTargets targets;
    DeviceStatus sleeping;
    sleeping.connectionId = L"receiver-A";
    sleeping.ghub = true; sleeping.asleep = true;
    targets.Observe({sleeping}, {L"receiver-A", L"receiver-B"});
    assert(targets.ghub.size() == 1 && targets.ghub[0] == L"receiver-A");
    assert(targets.retry.size() == 1 && targets.retry[0] == L"receiver-B");
    targets.Observe({}, {L"receiver-A"}); // transient G HUB timeout keeps localhost retry
    assert(targets.ghub.size() == 1);
    assert(!draybattery::LogitechPollTargets::Has(targets.retry, L"receiver-A"));
    auto awake = sleeping;
    awake.connectionId = L"receiver-B"; awake.ghub = false;
    awake.asleep = false; awake.percent = 39; awake.lastLiveMs = 1000;
    targets.Observe({awake}, {L"receiver-B"});
    assert(!draybattery::LogitechPollTargets::Has(targets.retry, L"receiver-B"));
    {
        const std::wstring root = L"usb\\vid_1532&pid_0a4c\\9&17fe9c48&0&3";
        const std::wstring xinput = L"usb\\vid_1532&pid_0a4c&ig_00\\b&44ed2f0&0&00";
        const std::wstring iface = L"usb\\vid_1532&pid_0a4c&mi_01\\a&13ddc2de&0&0001";
        const auto links = draybattery::WolverineDiscoveryLinks({root, xinput, iface}, {root, xinput});
        assert(links == std::vector<std::wstring>{root});
        draybattery::DeviceDiscoveryRetries realDiscovery;
        realDiscovery.Observe(links, {}, 1000);
        realDiscovery.Complete({root}, {root}, 4000);
        assert(realDiscovery.Next() == 0); // successful physical root leaves no virtual IG retry
    }
    {
        draybattery::DeviceDiscoveryRetries discovery;
        discovery.Observe({L"W-A", L"W-B"}, {L"W-B"}, 1000);
        assert(discovery.Next() == 4000 && discovery.Due(3999).empty());
        assert(discovery.Due(4000) == std::vector<std::wstring>{L"W-A"});
        discovery.Complete({L"W-A"}, {}, 4000);
        assert(discovery.Next() == 12000);
        discovery.Complete({L"W-A"}, {}, 12000); assert(discovery.Next() == 28000);
        discovery.Complete({L"W-A"}, {}, 28000); assert(discovery.Next() == 58000);
        discovery.Complete({L"W-A"}, {}, 58000); assert(discovery.Next() == 0);
        discovery.Observe({L"W-A", L"W-B"}, {}, 60000); assert(discovery.Next() == 0); // bounded, no periodic restart
        discovery.Observe({L"W-B"}, {}, 61000);
        discovery.Observe({L"W-A", L"W-B"}, {}, 62000); assert(discovery.Next() == 65000); // real reappearance
        discovery.Complete({L"W-A"}, {L"W-A"}, 65000); assert(discovery.Next() == 0);
        discovery.Observe({L"W-A"}, {}, 66000, true); assert(discovery.Next() == 69000);
        discovery.Observe({}, {}, 67000); assert(discovery.Next() == 0); // unplug cancels retries
        discovery.Observe({L"W-A"}, {}, 68000); assert(discovery.Next() == 71000);
        discovery.Cancel(); assert(discovery.Next() == 0 && discovery.Due(72000).empty()); // exception backoff
        assert(draybattery::WorkerWaitMs(1000, 61000, 0, false, 0, false, 4000) == 3000);
    }
    std::cout << "Poll schedule passed: 3s, 8s, 16s, 30s, then 60s.\n";
    return 0;
}
