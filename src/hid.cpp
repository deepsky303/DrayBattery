#include "hid.h"

#include <setupapi.h>
#include <cfgmgr32.h>
#include <devpkey.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwctype>

std::vector<HidColl> EnumHid(bool* complete) {
    if (complete) *complete = false;
    std::vector<HidColl> out;
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO di = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (di == INVALID_HANDLE_VALUE) return out;
    SP_DEVICE_INTERFACE_DATA ifd{ sizeof(ifd) };
    bool all = true;
    for (DWORD i = 0;; i++) {
        if (!SetupDiEnumDeviceInterfaces(di, nullptr, &hidGuid, i, &ifd)) {
            all = all && GetLastError() == ERROR_NO_MORE_ITEMS;
            break;
        }
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(di, &ifd, nullptr, 0, &need, nullptr);
        if (!need) { all = false; continue; }
        std::vector<BYTE> buf(need);
        auto det = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buf.data());
        det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        SP_DEVINFO_DATA dd{ sizeof(dd) };
        if (!SetupDiGetDeviceInterfaceDetailW(di, &ifd, det, need, nullptr, &dd)) { all = false; continue; }
        HANDLE h = CreateFileW(det->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) { all = false; continue; }
        HidColl c;
        c.path = det->DevicePath;
        GUID container{};
        DEVPROPTYPE type = 0;
        if (SetupDiGetDevicePropertyW(di, &dd, &DEVPKEY_Device_ContainerId, &type,
                (PBYTE)&container, sizeof(container), nullptr, 0) && type == DEVPROP_TYPE_GUID) {
            wchar_t text[64]{};
            if (container != GUID_NULL && StringFromGUID2(container, text, 64))
                c.containerId = text;
        }
        DEVINST node = dd.DevInst;
        for (int depth = 0; depth < 12; ++depth) {
            wchar_t id[MAX_DEVICE_ID_LEN]{};
            if (CM_Get_Device_IDW(node, id, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) break;
            std::wstring normalized = id;
            for (auto& ch : normalized) ch = (wchar_t)towlower(ch);
            c.instanceIds.push_back(normalized);
            if (normalized.rfind(L"bth", 0) == 0) {
                c.connectionId = c.containerId.empty() ? normalized : L"container:" + c.containerId;
                break; // a Bluetooth adapter is not this peripheral's receiver
            }
            if (normalized.rfind(L"usb\\vid_", 0) == 0 &&
                normalized.substr(0, normalized.find(L'\\', 4)).find(L"&mi_") == std::wstring::npos) {
                c.connectionId = normalized;
                break;
            }
            DEVINST parent = 0;
            if (CM_Get_Parent(&parent, node, 0) != CR_SUCCESS) break;
            node = parent;
        }
        if (c.connectionId.empty() && !c.containerId.empty())
            c.connectionId = L"container:" + c.containerId;
        if (c.connectionId.empty()) c.connectionId = c.path;
        HIDD_ATTRIBUTES a{ sizeof(a) };
        if (HidD_GetAttributes(h, &a)) { c.vid = a.VendorID; c.pid = a.ProductID; }
        else all = false;
        PHIDP_PREPARSED_DATA pp;
        if (HidD_GetPreparsedData(h, &pp)) {
            if (HidP_GetCaps(pp, &c.caps) != HIDP_STATUS_SUCCESS) all = false;
            HidD_FreePreparsedData(pp);
        } else all = false;
        CloseHandle(h);
        std::wstring lower = c.path;
        for (auto& ch : lower) ch = towlower(ch);
        size_t mi = lower.find(L"&mi_");
        if (mi != std::wstring::npos) c.iface = (int)wcstol(lower.c_str() + mi + 4, nullptr, 16);
        out.push_back(std::move(c));
    }
    SetupDiDestroyDeviceInfoList(di);
    if (complete) *complete = all;
    return out;
}

// ---- HidDev -----------------------------------------------------------------------------------

bool HidDev::Open(const HidColl& c) {
    Close();
    c_ = c;
    h_ = CreateFileW(c.path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                     FILE_FLAG_OVERLAPPED, nullptr);
    overlapped_ = true;
    return ok();
}

bool HidDev::OpenFeatureOnly(const HidColl& c) {
    Close();
    c_ = c;
    h_ = CreateFileW(c.path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    overlapped_ = false;
    return ok();
}

void HidDev::Close() {
    if (h_ != INVALID_HANDLE_VALUE) CloseHandle(h_);
    h_ = INVALID_HANDLE_VALUE;
}

bool HidDev::Write(const std::vector<BYTE>& data, int timeoutMs) {
    if (!ok() || !overlapped_ || !c_.outLen() || data.empty()) return false;
    std::vector<BYTE> buf(c_.outLen(), 0);
    memcpy(buf.data(), data.data(), std::min(buf.size(), data.size()));
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD n = 0;
    bool done = false;
    if (WriteFile(h_, buf.data(), (DWORD)buf.size(), nullptr, &ov) || GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ov.hEvent, (DWORD)timeoutMs) == WAIT_OBJECT_0) {
            done = GetOverlappedResult(h_, &ov, &n, FALSE) != FALSE;
        } else {
            CancelIo(h_);
            done = GetOverlappedResult(h_, &ov, &n, TRUE) != FALSE;
        }
    }
    CloseHandle(ov.hEvent);
    return done;
}

int HidDev::Read(std::vector<BYTE>& buf, int timeoutMs) {
    if (!ok() || !overlapped_ || !c_.inLen()) return -1;
    std::vector<BYTE> tmp(c_.inLen(), 0);
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD got = 0;
    bool done = false;
    if (ReadFile(h_, tmp.data(), (DWORD)tmp.size(), nullptr, &ov) || GetLastError() == ERROR_IO_PENDING) {
        if (timeoutMs > 0 && WaitForSingleObject(ov.hEvent, (DWORD)timeoutMs) == WAIT_OBJECT_0) {
            done = GetOverlappedResult(h_, &ov, &got, FALSE) != FALSE;
        } else {
            CancelIo(h_);
            done = GetOverlappedResult(h_, &ov, &got, TRUE) != FALSE;  // it may have completed meanwhile
        }
    }
    CloseHandle(ov.hEvent);
    if (!done || got == 0) return -1;
    size_t off = tmp[0] == 0 ? 1 : 0;  // hidapi semantics: drop the "no report ID" byte
    buf.assign(tmp.begin() + off, tmp.begin() + got);
    return (int)buf.size();
}

void HidDev::Flush() {
    if (ok()) HidD_FlushQueue(h_);
}

bool HidDev::SetFeature(const std::vector<BYTE>& data) {
    if (!ok() || !c_.featLen()) return false;
    std::vector<BYTE> buf(c_.featLen(), 0);
    memcpy(buf.data(), data.data(), std::min(buf.size(), data.size()));
    return HidD_SetFeature(h_, buf.data(), (ULONG)buf.size()) != FALSE;
}

bool HidDev::GetFeature(BYTE reportId, std::vector<BYTE>& out) {
    if (!ok() || !c_.featLen()) return false;
    out.assign(c_.featLen(), 0);
    out[0] = reportId;
    return HidD_GetFeature(h_, out.data(), (ULONG)out.size()) != FALSE;
}

// ---- helpers ----------------------------------------------------------------------------------

std::wstring HidProduct(const HidColl& c) {
    HANDLE h = CreateFileW(c.path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return L"";
    wchar_t name[128] = {};
    if (!HidD_GetProductString(h, name, sizeof(name) - sizeof(wchar_t))) name[0] = 0;
    CloseHandle(h);
    std::wstring s = name;
    while (!s.empty() && iswspace(s.back())) s.pop_back();
    return s;
}

std::wstring Hex4(USHORT v) {
    wchar_t b[8];
    swprintf(b, 8, L"%04X", v);
    return b;
}

int Clamp100(int v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }

int MapTo100(int v, int lo, int hi) {
    if (hi <= lo) return Clamp100(v);
    return Clamp100((v - lo) * 100 / (hi - lo));
}

int Interpolate(int mv, const int* mvTable, const int* pctTable, int n) {
    if (mv >= mvTable[0]) return pctTable[0];
    for (int i = 1; i < n; i++) {
        if (mv >= mvTable[i]) {
            int span = mvTable[i - 1] - mvTable[i];
            return pctTable[i] + (pctTable[i - 1] - pctTable[i]) * (mv - mvTable[i]) / (span ? span : 1);
        }
    }
    return pctTable[n - 1];
}

DeviceKind GuessKind(const std::wstring& name) {
    std::wstring n = name;
    for (auto& ch : n) ch = towlower(ch);
    auto has = [&](const wchar_t* s) { return n.find(s) != std::wstring::npos; };
    if (has(L"mouse") || has(L"mice") || has(L"trackball")) return DeviceKind::Mouse;
    if (has(L"keyboard") || has(L"keys")) return DeviceKind::Keyboard;
    if (has(L"controller") || has(L"gamepad") || has(L"xbox") || has(L"dualsense") || has(L"dualshock") || has(L"joy-con"))
        return DeviceKind::Gamepad;
    if (has(L"head") || has(L"buds") || has(L"pods") || has(L"ear") || has(L"wh-") || has(L"wf-") || has(L"audio") ||
        has(L"speaker") || has(L"arctis") || has(L"cloud") || has(L"void") || has(L"virtuoso"))
        return DeviceKind::Headset;
    return DeviceKind::Other;
}

DeviceKind KindFromCollections(const std::vector<HidColl>& all, USHORT vid, USHORT pid) {
    bool mouse = false, pad = false, kbd = false;
    for (const auto& c : all) {
        if (c.vid != vid || c.pid != pid || c.page() != 0x01) continue;
        mouse |= c.usage() == 0x02;
        pad |= c.usage() == 0x04 || c.usage() == 0x05;
        kbd |= c.usage() == 0x06;
    }
    if (mouse) return DeviceKind::Mouse;
    if (pad) return DeviceKind::Gamepad;
    if (kbd) return DeviceKind::Keyboard;
    return DeviceKind::Other;
}
