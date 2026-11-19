// Read-only benchmark of the exact production presence query; no HID reports or controls.
#include "../src/hid.h"
#include <setupapi.h>
#include <algorithm>
#include <cstdio>
#include <numeric>

static double CpuMs() {
    FILETIME creation{}, exit{}, kernel{}, user{};
    GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel, &user);
    ULARGE_INTEGER k{}, u{};
    k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime; u.HighPart = user.dwHighDateTime;
    return (k.QuadPart + u.QuadPart) / 10000.0;
}
int main() {
    auto di = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    unsigned count = 0;
    SP_DEVINFO_DATA dd{sizeof(dd)};
    if (di == INVALID_HANDLE_VALUE) return 2;
    while (SetupDiEnumDeviceInfo(di, count, &dd)) ++count;
    SetupDiDestroyDeviceInfoList(di);
    std::printf("present_pnp_nodes=%u\n", count);
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    std::vector<double> times, cpus;
    for (int i = 0; i < 12; ++i) {
        LARGE_INTEGER a{}, b{};
        const auto cpu = CpuMs();
        QueryPerformanceCounter(&a);
        const auto p = EnumBluetoothPresence();
        QueryPerformanceCounter(&b);
        const auto wall = 1000.0 * (b.QuadPart - a.QuadPart) / frequency.QuadPart;
        const auto used = CpuMs() - cpu;
        times.push_back(wall); cpus.push_back(used);
        std::printf("run=%d wall_ms=%.3f cpu_ms=%.3f usb=%zu bt_yes=%zu bt_no=%zu bt_unknown=%zu complete=%d/%d\n",
            i + 1, wall, used, p.usbPresent.size(), p.btConnected.size(), p.btDisconnected.size(),
            p.btUnknown.size(), p.usbComplete, p.bluetoothComplete);
    }
    std::sort(times.begin(), times.end());
    std::printf("summary n=%zu min_ms=%.3f median_ms=%.3f mean_ms=%.3f max_ms=%.3f mean_cpu_ms=%.3f\n",
        times.size(), times.front(), (times[5] + times[6]) / 2,
        std::accumulate(times.begin(), times.end(), 0.0) / times.size(), times.back(),
        std::accumulate(cpus.begin(), cpus.end(), 0.0) / cpus.size());
}
