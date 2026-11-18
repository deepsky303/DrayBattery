// Wolverine V3 Pro Windows / PC mode (VID:PID 1532:0A4C).
// Uses a *read-only battery command* over HID feature report ID 0x04.
// Protocol documented by the wv3c project (docs/protocol.md).
// No lighting, firmware, profile, polling-rate or button commands are sent.
#include "drivers.h"
#include <cstdio>

namespace {
constexpr USHORT kVid = 0x1532;
constexpr USHORT kPid = 0x0A4C;
constexpr BYTE kReportId = 0x04;
constexpr BYTE kClass = 0x07;
constexpr BYTE kGetBattery = 0x80;
constexpr BYTE kGetCharging = 0x84;
constexpr BYTE kTid = 0x1F;
constexpr int kLen = 91;  // report ID followed by 90 protocol bytes

bool Query(HidDev& device, BYTE command, BYTE& value) {
    std::vector<BYTE> request(kLen, 0);
    request[0] = kReportId;
    BYTE* r = request.data() + 1;
    r[1] = kTid;
    r[5] = 2;
    r[6] = kClass;
    r[7] = command;
    BYTE checksum = 0;
    for (int i = 2; i < 88; ++i) checksum ^= r[i];
    r[88] = checksum;
    // HID SetFeature transmits a request. The command ID's high bit denotes
    // GET/query: it does not change controller configuration.
    if (!device.SetFeature(request)) return false;
    Sleep(165); // Hardware can disconnect after rapid consecutive queries.

    std::vector<BYTE> response;
    if (!device.GetFeature(kReportId, response) ||
        response.size() != kLen || response[0] != kReportId)
        return false;
    const BYTE* a = response.data() + 1;
    if (a[0] != 0x02 || a[1] != kTid ||
        a[6] != kClass || a[7] != command || a[5] < 2)
        return false;
    BYTE actual = 0;
    for (int i = 2; i < 88; ++i) actual ^= a[i];
    if (a[88] != actual) return false;
    value = a[9]; // second argument: actual battery raw 0..255
    return true;
}
} // namespace

void PollWolverinePc(PollContext& ctx) {
    if (ctx.IsClaimed(kVid, kPid)) return;
    for (const HidColl* c : ctx.Find(kVid, kPid, [](const HidColl& h) {
             return h.featLen() == kLen && h.page() == 0x01;
         })) {
        HidDev device;
        if (!device.OpenFeatureOnly(*c)) continue;
        BYTE raw = 0;
        if (!Query(device, kGetBattery, raw)) continue;

        DeviceStatus status;
        status.key = L"wolverine:1532:0A4C";
        // Only one verified Wolverine V3 Pro is attached in this configuration.
        // The two operating modes are the same controller and share a logical ID.
        status.physicalId = L"razer:wolverine-v3pro:primary";
        status.name = L"Razer Wolverine V3 Pro";
        status.via = L"Razer HID (real battery)";
        status.kind = DeviceKind::Gamepad;
        status.percent = Clamp100((raw * 100 + 127) / 255);
        status.estimated = true; // firmware-reported 0..255 value; normalized, not metrological accuracy
        status.graceSec = 900;
        BYTE charging = 0;
        if (Query(device, kGetCharging, charging))
            status.charging = charging != 0;
        ctx.Add(status, kVid, kPid);
        return;
    }
}
