// Wolverine V3 Pro Windows / PC mode (VID:PID 1532:0A4C).
// Uses a *read-only battery command* over HID feature report ID 0x04.
// Protocol documented by the wv3c project (docs/protocol.md).
// No lighting, firmware, profile, polling-rate or button commands are sent.
#include "drivers.h"
#include <cstdio>
#include <map>
#include "wolverine_protocol.h"

namespace {
constexpr USHORT kVid = 0x1532;
constexpr USHORT kPid = 0x0A4C;
constexpr BYTE kReportId = 0x04;
constexpr int kLen = 91;  // report ID followed by 90 protocol bytes

std::map<std::wstring, ULONGLONG> lastQuery;
bool Query(HidDev& device, BYTE& value, PollContext& ctx) {
    // Refresh clicks cannot produce back-to-back controller requests. A single
    // battery GET is enough; 0x07/0x84 has no verified charging-state contract.
    auto& previous = lastQuery[device.coll().path];
    const auto now = GetTickCount64();
    if (previous && now - previous < 1000) {
        const auto delay = (DWORD)(1000 - (now - previous));
        if (ctx.stopEvent) { if (WaitForSingleObject(ctx.stopEvent, delay) != WAIT_TIMEOUT) return false; }
        else Sleep(delay);
    }
    if (ctx.Stopping()) return false;
    const auto request = draybattery::wolverine::BatteryRequest();
    previous = GetTickCount64();
    // HID SetFeature transmits a request. The command ID's high bit denotes
    // GET/query: it does not change controller configuration.
    if (!device.SetFeature(request)) {
        ctx.Note(L"Wolverine SetFeature failed; Win32=" + std::to_wstring(GetLastError()));
        return false;
    }
    Sleep(165); // Hardware can disconnect after rapid consecutive queries.

    std::vector<BYTE> response;
    if (!device.GetFeature(kReportId, response) ||
        response.size() != kLen || response[0] != kReportId)
        { ctx.Note(L"Wolverine GetFeature/report length failed; Win32=" + std::to_wstring(GetLastError())); return false; }
    const BYTE* a = response.data() + 1;
    ctx.Note(L"Wolverine response status=" + Hex4(a[0]) + L" tid=" + Hex4(a[1]) +
        L" class=" + Hex4(a[6]) + L" command=" + Hex4(a[7]) + L" size=" + std::to_wstring(a[5]));
    const bool valid = draybattery::wolverine::BatteryReply(response, value);
    if (!valid) ctx.Note(L"Wolverine response rejected (header or checksum)");
    return valid;
}
} // namespace

void PollWolverinePc(PollContext& ctx) {
    if (ctx.IsClaimed(kVid, kPid)) return;
    for (const HidColl* c : ctx.Find(kVid, kPid, [](const HidColl& h) {
             return h.featLen() == kLen && h.page() == 0x01;
         })) {
        if (ctx.SourceHandled(*c)) continue;
        HidDev device;
        if (!device.OpenFeatureOnly(*c)) {
            ctx.Note(L"Wolverine open failed; Win32=" + std::to_wstring(GetLastError())); continue;
        }
        BYTE raw = 0;
        if (!Query(device, raw, ctx)) continue;

        DeviceStatus status;
        status.key = L"wolverine:1532:0A4C";
        status.name = L"Razer Wolverine V3 Pro";
        status.via = L"Razer HID (real battery)";
        // This command does not identify cable vs dongle. Only exact PnP receiver
        // evidence can promote this USB endpoint to presence-backed retention.
        status.linkKind = LinkKind::Unknown;
        status.kind = DeviceKind::Gamepad;
        status.percent = Clamp100((raw * 100 + 127) / 255);
        status.estimated = true; // firmware-reported 0..255 value; normalized, not metrological accuracy
        ctx.Add(status, kVid, kPid, c);
    }
}
