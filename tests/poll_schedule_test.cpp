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
    for (unsigned i = 0; i < 1000; i++) {
        const auto ms = PollIntervalMs(i);
        assert(ms >= 3000u && ms <= 60000u);
    }
    std::cout << "Poll schedule passed: 3s, 8s, 16s, 30s, then 60s.\n";
    return 0;
}
