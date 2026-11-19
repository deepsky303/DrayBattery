// Link the production HID++ driver to a simulated transport. No real HID access,
// registry access, G HUB connection, or settings changes are made by this test.
#include "../src/drivers/logitech_transport.h"
#include "../src/drivers/logitech_cache.h"
#include "../src/battery_lifecycle.h"
#include <cassert>
#include <iostream>
#include <map>

namespace {
std::map<std::wstring, std::vector<BYTE>> replies;
std::vector<std::wstring> opened, written;
int battery = 42;
bool zeroFullyCharged = false;
HidColl Collection(const wchar_t* unit, const wchar_t* path) {
    HidColl c;
    c.vid = 0x046D; c.pid = 0xC539;
    c.path = path; c.connectionId = unit; c.transport = LinkKind::UsbCable;
    c.instanceIds = {unit};
    c.caps.UsagePage = 0xFF00; c.caps.Usage = 2;
    c.caps.OutputReportByteLength = 20;
    c.caps.InputReportByteLength = 20;
    return c;
}
}
bool HidDev::Open(const HidColl& c) {
    c_ = c;
    h_ = reinterpret_cast<HANDLE>(1);
    opened.push_back(c.connectionId);
    return true;
}
void HidDev::Close() { h_ = INVALID_HANDLE_VALUE; }
bool HidDev::Write(const std::vector<BYTE>& request, int) {
    written.push_back(c_.connectionId);
    std::vector<BYTE> r(20, 0);
    std::copy_n(request.begin(), 4, r.begin());
    if (request[1] != 1) {
        r[2] = 0xFF; r[3] = request[2]; r[4] = request[3]; r[5] = 8;
    } else if (request[2] == 0 && request[3] == 0x1D) {
        r[4] = 2; // HID++ 2.0 ping
    } else if (request[2] == 0) {
        const int feature = (request[4] << 8) | request[5];
        r[4] = feature == 5 ? 2 : feature == (zeroFullyCharged ? 0x1000 : 0x1004) ? 3 : 0;
    } else if (request[2] == 2) {
        const std::string model = "G903 LIGHTSPEED";
        if ((request[3] >> 4) == 0) r[4] = (BYTE)model.size();
        else if ((request[3] >> 4) == 1) {
            const size_t offset = request[4];
            for (size_t i = offset; i < model.size() && i - offset < 16; ++i)
                r[4 + i - offset] = (BYTE)model[i];
        } else r[4] = 3; // mouse
    } else if (request[2] == 3) {
        r[4] = zeroFullyCharged ? 0 : (BYTE)battery;
        r[6] = zeroFullyCharged ? 3 : 0;
    }
    replies[c_.path] = std::move(r);
    return true;
}
int HidDev::Read(std::vector<BYTE>& out, int) {
    auto& r = replies[c_.path];
    if (r.empty()) return -1;
    out = std::move(r);
    return (int)out.size();
}
std::wstring HidProduct(const HidColl&) { return L"USB Receiver"; }
int Interpolate(int, const int*, const int*, int) { assert(false); return -1; }
DeviceKind GuessKind(const std::wstring&) { return DeviceKind::Mouse; }

int main() {
    const auto a = Collection(L"usb\\vid_046d&pid_c539\\unit-a",
        L"\\\\?\\hid#vid_046d&pid_c539&mi_01#unit-a#{guid}");
    const auto b = Collection(L"usb\\vid_046d&pid_c539\\unit-b",
        L"\\\\?\\hid#vid_046d&pid_c539&mi_01#unit-b#{guid}");
    PollContext ctx;
    ctx.colls = {a, b};
    PollLogitech(ctx);
    assert(ctx.out.size() == 2);
    assert(ctx.out[0].key != ctx.out[1].key);
    assert(ctx.out[0].connectionId != ctx.out[1].connectionId);
    assert(opened.size() == 2);
    for (const auto& d : ctx.out) {
        assert(d.receiverBacked && d.percent == 42 && d.lastLiveMs > 0);
    }
    assert(draybattery::MergeLogicalDevices(ctx.out).size() == 2);

    // Claiming one physical receiver for G HUB must not suppress its same-PID sibling.
    ctx.out.clear(); opened.clear(); written.clear();
    ctx.logitechClaimed.insert(a.connectionId);
    PollLogitech(ctx);
    assert(ctx.out.size() == 1 && ctx.out[0].connectionId == b.connectionId);
    assert(opened.size() == 1 && opened[0] == b.connectionId);
    for (const auto& link : written) assert(link == b.connectionId);

    using draybattery::logitech::MatchLink;
    assert(MatchLink(ctx.colls, a.path, a.pid) == a.connectionId);
    assert(MatchLink(ctx.colls, L"USB\\VID_046D&PID_C539\\UNIT-A", a.pid) == a.connectionId);
    assert(MatchLink(ctx.colls, L"USB\\VID_046D&PID_C539\\removed", a.pid).empty());
    assert(MatchLink({b}, a.path, a.pid).empty()); // no PID-only receiver substitution

    // Real G HUB paths append a numeric suffix to the USB MI node.
    auto aa = a, bb = b;
    aa.path = L"\\\\?\\hid#vid_046d&pid_c539&mi_02#unit-a#{guid}";
    bb.path = L"\\\\?\\hid#vid_046d&pid_c539&mi_02#unit-b#{guid}";
    aa.iface = bb.iface = 2;
    const std::wstring aUsb = L"usb\\vid_046d&pid_c539&mi_02\\7&unit-a&0&0002";
    const std::wstring bUsb = L"usb\\vid_046d&pid_c539&mi_02\\7&unit-b&0&0002";
    aa.instanceIds.push_back(aUsb); bb.instanceIds.push_back(bUsb);
    assert(MatchLink({aa, bb}, aUsb + L":0", a.pid) == a.connectionId);
    assert(MatchLink({aa, bb}, bUsb + L":1", a.pid) == b.connectionId);
    assert(MatchLink({bb}, aUsb + L":0", a.pid).empty());
    assert(MatchLink({aa}, aUsb + L":", a.pid).empty());
    assert(MatchLink({aa}, aUsb + L":0bad", a.pid).empty());

    // G HUB's MI_00 differs from HID++ MI_02. The keyboard collection proves
    // their shared physical USB parent even with TWO same-PID receivers present.
    auto keyboard = aa;
    const std::wstring aKeyboard = L"usb\\vid_046d&pid_c539&mi_00\\7&unit-a&0&0000";
    keyboard.path = L"keyboard-interface-a";
    keyboard.caps.UsagePage = 1; keyboard.caps.Usage = 6;
    keyboard.instanceIds = {aKeyboard, a.connectionId};
    assert(MatchLink({aa, bb, keyboard}, aKeyboard + L":0", a.pid) == a.connectionId);
    assert(MatchLink({bb}, aKeyboard + L":0", a.pid).empty());
    // No enumerated HID collection on G HUB's MI: use only a verified present
    // PnP ancestor supplied by the caller. No PID-unique/name-based guessing.
    assert(MatchLink({aa, bb}, aKeyboard + L":0", a.pid, a.connectionId) == a.connectionId);
    assert(MatchLink({aa, bb}, aKeyboard + L":0", a.pid).empty());
    assert(MatchLink({bb}, aKeyboard + L":0", a.pid, a.connectionId).empty());
    assert(MatchLink({aa, bb}, aUsb + L":0", a.pid, b.connectionId).empty());
    assert(MatchLink({aa, bb}, aUsb + L":0", 0xC547, a.connectionId).empty());
    auto ambiguous = keyboard;
    ambiguous.connectionId = b.connectionId;
    assert(MatchLink({aa, bb, keyboard, ambiguous}, aKeyboard, a.pid).empty());
    assert(draybattery::logitech::Connections({a, a, b}).size() == 2);
    auto razer = a;
    razer.vid = 0x1532; razer.connectionId = L"razer-controller";
    const auto selected = draybattery::logitech::SelectLinks({a, b, razer}, {a.connectionId});
    assert(selected.size() == 1 && selected[0].connectionId == a.connectionId);

    // The old PID-keyed registry cache must not manufacture a record at restart.
    ctx.out.clear(); ctx.claimed.clear();
    PollLogitechCache(ctx);
    assert(ctx.out.empty());
    auto stale = DeviceStatus{};
    stale.receiverBacked = true; stale.asleep = true; stale.percent = 100;
    ctx.out.push_back(stale);
    PollLogitechCache(ctx);
    assert(ctx.out[0].percent == -1 && !ctx.out[0].lastLiveMs);

    // BATTERY_STATUS state 3 must not turn the returned 0% into fictitious 100%.
    zeroFullyCharged = true;
    ctx = {}; ctx.colls = {a};
    PollLogitech(ctx);
    assert(ctx.out.size() == 1 && ctx.out[0].percent == 0);
    std::cout << "LOGITECH_DRIVER_TESTS_PASSED (same-PID receivers, G HUB suffix/MI/ancestor/ambiguity, G HUB claim, no registry ghost, no invented 100%)\n";
}
