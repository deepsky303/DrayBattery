// Fallbacks that work for any vendor:
//  * HID devices exposing a standard battery usage (Generic Device Controls "Battery Strength",
//    Battery System "Absolute State Of Charge") in a feature or input report.
//  * Bluetooth devices: Windows keeps the level it reads over HFP (classic) or the GATT Battery
//    Service (LE) in a device property, the same value Settings > Bluetooth & devices shows.
#include "drivers.h"
#include "../windows_bluetooth_battery.h"

#include <setupapi.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <devpkey.h>

#include <cstring>
#include <map>

namespace {

bool IsBatteryUsage(USHORT page, USAGE usage) {
    return (page == 0x06 && usage == 0x20) || (page == 0x85 && usage == 0x65);
}

const DEVPROPKEY kBtBattery = { { 0x104EA319, 0x6EE2, 0x4701, { 0xBD, 0x47, 0x8D, 0xDB, 0xF4, 0x25, 0xBB, 0xE5 } }, 2 };
const DEVPROPKEY kBtBatteryUpdated = { kBtBattery.fmtid, 7 };
const DEVPROPKEY kIsConnected = { { 0x83DA6326, 0x97A6, 0x4088, { 0x94, 0x53, 0xA1, 0x92, 0x3F, 0x57, 0x3B, 0x29 } }, 15 };
const DEVPROPKEY kClassOfDevice = { { 0x2BD67D8B, 0x8BEB, 0x48D5, { 0x87, 0xE0, 0x6C, 0xDA, 0x34, 0x28, 0x04, 0x0A } }, 10 };

std::wstring StrProp(HDEVINFO di, SP_DEVINFO_DATA* dd, const DEVPROPKEY& key) {
    DEVPROPTYPE t;
    wchar_t buf[256] = {};
    if (SetupDiGetDevicePropertyW(di, dd, &key, &t, (PBYTE)buf, sizeof(buf) - sizeof(wchar_t), nullptr, 0) && t == DEVPROP_TYPE_STRING)
        return buf;
    return L"";
}

std::wstring StripServiceSuffix(std::wstring n) {
    for (const wchar_t* suf : { L" Hands-Free AG", L" Hands-Free", L" Avrcp Transport", L" Stereo", L" Audio Gateway" }) {
        size_t l = wcslen(suf);
        if (n.size() > l && n.compare(n.size() - l, l, suf) == 0) return n.substr(0, n.size() - l);
    }
    return n;
}

}  // namespace

void PollGenericHid(PollContext& ctx) {
    const auto claimedBefore = ctx.claimed;
    for (const auto& c : ctx.colls) {
        if (claimedBefore.count({c.vid, c.pid})) continue;
        if (c.page() == 0x84 || c.page() == 0x85) continue;  // UPS / power supplies
        if (!c.caps.NumberFeatureValueCaps && !c.caps.NumberInputValueCaps) continue;
        HANDLE h = CreateFileW(c.path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        PHIDP_PREPARSED_DATA pp;
        if (!HidD_GetPreparsedData(h, &pp)) { CloseHandle(h); continue; }

        int pct = -1;
        for (HIDP_REPORT_TYPE type : { HidP_Feature, HidP_Input }) {
            USHORT n = type == HidP_Feature ? c.caps.NumberFeatureValueCaps : c.caps.NumberInputValueCaps;
            USHORT len = type == HidP_Feature ? c.featLen() : c.inLen();
            if (!n || !len) continue;
            std::vector<HIDP_VALUE_CAPS> vc(n);
            if (HidP_GetValueCaps(type, vc.data(), &n, pp) != HIDP_STATUS_SUCCESS) continue;
            for (USHORT i = 0; i < n && pct < 0; i++) {
                const auto& v = vc[i];
                USAGE u = v.IsRange ? v.Range.UsageMin : v.NotRange.Usage;
                if (!IsBatteryUsage(v.UsagePage, u)) continue;
                std::vector<BYTE> buf(len, 0);
                buf[0] = v.ReportID;
                BOOL got = type == HidP_Feature ? HidD_GetFeature(h, buf.data(), len) : HidD_GetInputReport(h, buf.data(), len);
                if (!got) continue;
                ULONG val = 0;
                if (HidP_GetUsageValue(type, v.UsagePage, v.LinkCollection, u, &val, pp, (PCHAR)buf.data(), len) != HIDP_STATUS_SUCCESS)
                    continue;
                // Apple devices (Magic Trackpad / Mouse / Keyboard) report a plain percentage while
                // declaring a 0-255 range; Linux handles them with HID_BATTERY_QUIRK_PERCENT.
                bool percentQuirk = c.vid == 0x05AC && val <= 100;
                if (percentQuirk) pct = (int)val;
                else if (v.LogicalMax > v.LogicalMin) pct = Clamp100((int)(((LONG)val - v.LogicalMin) * 100 / (v.LogicalMax - v.LogicalMin)));
                else if (val <= 100) pct = (int)val;
            }
            if (pct >= 0) break;
        }
        HidD_FreePreparsedData(pp);
        CloseHandle(h);
        if (pct < 0) continue;
        DeviceStatus s;
        s.name = HidProduct(c);
        if (s.name.empty()) s.name = L"Wireless device";
        s.key = L"hid:" + Hex4(c.vid) + L":" + Hex4(c.pid);
        s.via = L"HID";
        s.kind = KindFromCollections(ctx.colls, c.vid, c.pid);
        if (s.kind == DeviceKind::Other) s.kind = GuessKind(s.name);
        s.percent = pct;
        // A standard Bluetooth HID getter may be served by the OS cache; no freshness token.
        s.batteryCached = c.transport == LinkKind::Bluetooth;
        s.asleep = s.batteryCached;
        ctx.Add(s, c.vid, c.pid, &c);
    }
}

namespace {
PhysicalPresence ReadPhysicalPresence(PollContext* ctx, std::wstring* diagnostic = nullptr) {
    HDEVINFO di = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (di == INVALID_HANDLE_VALUE) return {};
    std::vector<draybattery::BluetoothNode> nodes;
    std::set<std::wstring> usb;
    bool complete = true, bluetoothComplete = true;
    SP_DEVINFO_DATA dd{ sizeof(dd) };
    for (DWORD i = 0;; ++i) {
        if (!SetupDiEnumDeviceInfo(di, i, &dd)) { complete &= GetLastError() == ERROR_NO_MORE_ITEMS; break; }
        wchar_t inst[512]{};
        if (!SetupDiGetDeviceInstanceIdW(di, &dd, inst, 512, nullptr)) { complete = false; continue; }
        const auto id = draybattery::LowerId(inst);
        if (id.rfind(L"usb\\vid_", 0) == 0 &&
            id.substr(0, id.find(L'\\', 4)).find(L"&mi_") == std::wstring::npos) usb.insert(id);
        if (id.rfind(L"bthenum\\", 0) && id.rfind(L"bthle\\", 0) && id.rfind(L"bthledevice\\", 0)) continue;
        GUID container{};
        DEVPROPTYPE t = 0;
        if (!SetupDiGetDevicePropertyW(di, &dd, &DEVPKEY_Device_ContainerId, &t,
                (PBYTE)&container, sizeof(container), nullptr, 0) || t != DEVPROP_TYPE_GUID || container == GUID_NULL) {
            bluetoothComplete = false; continue;
        }
        wchar_t text[64]{};
        if (!StringFromGUID2(container, text, 64)) { bluetoothComplete = false; continue; }
        draybattery::BluetoothNode n;
        n.instance = id;
        n.container = draybattery::LowerId(text);
        n.name = StrProp(di, &dd, DEVPKEY_Device_FriendlyName);
        if (n.name.empty()) n.name = StrProp(di, &dd, DEVPKEY_Device_DeviceDesc);
        if (draybattery::BluetoothPeripheralRoot(id)) {
            DEVPROP_BOOLEAN conn = DEVPROP_FALSE;
            if (SetupDiGetDevicePropertyW(di, &dd, &kIsConnected, &t, (PBYTE)&conn, sizeof(conn), nullptr, 0) &&
                t == DEVPROP_TYPE_BOOLEAN) n.connection = conn == DEVPROP_FALSE ? Presence::Absent : Presence::Present;
            ULONG status = 0, problem = 0;
            if (n.connection != Presence::Unknown &&
                (CM_Get_DevNode_Status(&status, &problem, dd.DevInst, 0) != CR_SUCCESS || (status & DN_HAS_PROBLEM)))
                n.connection = Presence::Unknown;
            UINT32 cod = 0;
            if (SetupDiGetDevicePropertyW(di, &dd, &kClassOfDevice, &t, (PBYTE)&cod, sizeof(cod), nullptr, 0) && t == DEVPROP_TYPE_UINT32)
                n.classOfDevice = cod;
        }
        BYTE pct = 0;
        if (ctx && SetupDiGetDevicePropertyW(di, &dd, &kBtBattery, &t, &pct, sizeof(pct), nullptr, 0) &&
            t == DEVPROP_TYPE_BYTE && pct <= 100) n.percent = pct;
        FILETIME updated{};
        if (n.percent >= 0 && SetupDiGetDevicePropertyW(di, &dd, &kBtBatteryUpdated, &t,
                (PBYTE)&updated, sizeof(updated), nullptr, 0) && t == DEVPROP_TYPE_FILETIME)
            n.batteryReportedFiletime = (std::uint64_t(updated.dwHighDateTime) << 32) | updated.dwLowDateTime;
        nodes.push_back(std::move(n));
    }
    SetupDiDestroyDeviceInfoList(di);
    auto p = draybattery::BluetoothPresence(nodes, complete && bluetoothComplete, GetTickCount64());
    p.usbPresent.assign(usb.begin(), usb.end());
    p.usbComplete = complete;
    if (diagnostic) for (const auto& root : nodes) {
        if (!draybattery::BluetoothPeripheralRoot(root.instance)) continue;
        *diagnostic += root.name + L": root=" + (root.connection == Presence::Present ? L"connected" :
            root.connection == Presence::Absent ? L"disconnected" : L"unknown");
        bool found = false;
        for (const auto& n : nodes) if (n.container == root.container && n.percent >= 0) {
            found = true;
            *diagnostic += L"; Windows cached battery=" + std::to_wstring(n.percent) +
                L"%; timestamp=" + std::to_wstring(n.batteryReportedFiletime);
        }
        if (!found) *diagnostic += L"; Windows battery unavailable";
        *diagnostic += L"\n";
    }
    if (ctx) for (auto s : draybattery::WindowsBluetoothBatteries(nodes, p)) {
        s.name = StripServiceSuffix(std::move(s.name));
        if (s.kind == DeviceKind::Other) s.kind = GuessKind(s.name);
        ctx->Add(s, 0, 0);
    }
    return p;
}
} // namespace

PhysicalPresence EnumBluetoothPresence() { return ReadPhysicalPresence(nullptr); }
void PollBluetooth(PollContext& ctx) { ctx.presence = ReadPhysicalPresence(&ctx); }
std::wstring DiagnoseBluetooth() {
    PollContext ctx;
    std::wstring text;
    ctx.presence = ReadPhysicalPresence(&ctx, &text);
    return text;
}
