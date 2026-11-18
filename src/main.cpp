// Dray Battery - tray app showing the battery level of wireless mice, headsets and Bluetooth devices.
#include <windows.h>
#include <shellapi.h>
#include <dbt.h>
#include <initguid.h>
#include <hidclass.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "devices.h"
#include "widget.h"
#include "palette.h"
#include "poll_schedule.h"
#include "wolverine_xbox.h"
#include "drivers/wolverine_xbox_cache.h"
#include "hid.h"
#include <commctrl.h>

namespace {

constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT WM_RESULTS = WM_APP + 2;
constexpr UINT WM_WIDGET_CLICK = WM_APP + 3;
constexpr UINT WM_XBOX_BATTERY_UPDATED = WM_APP + 4;
constexpr UINT_PTR TIMER_DEBOUNCE = 1;
constexpr UINT_PTR TIMER_WIDGET = 2;
// Poll intervals are provided by poll_schedule.h (startup retries, then 60 s).
constexpr int LOW_BATTERY = 15;
constexpr UINT PLACEHOLDER_ID = 1;

enum MenuId : UINT { ID_REFRESH = 100, ID_AUTOSTART, ID_EXIT, ID_MODE_WIDGET, ID_MODE_TRAY, ID_REFRESH_XBOX, ID_STYLE_COLORED, ID_STYLE_WHITE, ID_STYLE_MONOCHROME, ID_DEVICE_FIRST = 1000 };

const wchar_t* kAppName = L"Dray Battery";
const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* kRunValue = L"DrayBattery";

const wchar_t* kSettingsKey = L"Software\\DrayBattery";

HINSTANCE g_inst;
HWND g_hwnd;
HANDLE g_refreshEvt, g_stopEvt, g_worker;
UINT g_taskbarCreatedMsg;
bool g_lightTaskbar;
bool g_wantWidget = true;  // user's choice: taskbar widget (default) or tray icons
bool g_useWidget = false;  // what is actually shown right now

struct TrayEntry {
    UINT uid = 0;
    DeviceStatus st;
    int lastPct = -1;        // last known percentage, kept while the device sleeps
    ULONGLONG lastSeen = 0;
    bool added = false;
    bool lowWarned = false;
    std::wstring iconSig;    // what the current icon shows, to avoid needless redraws
    HICON icon = nullptr;
};
std::map<std::wstring, TrayEntry> g_entries;
bool g_placeholder = false;
HICON g_placeholderIcon = nullptr;

// ---- helpers ----------------------------------------------------------------------------------

UINT StableId(const std::wstring& key) {
    UINT h = 2166136261u;
    for (wchar_t c : key) { h ^= c; h *= 16777619u; }
    return (h & 0x7FFFFFFF) | 0x100;
}

bool ReadLightTaskbar() {
    DWORD v = 0, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &sz) == ERROR_SUCCESS)
        return v != 0;
    return false;
}

std::wstring ExePath() {
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    return p;
}

// Devices the user chose to hide (by key), stored as a REG_MULTI_SZ.
std::set<std::wstring> g_hidden;

void LoadHidden() {
    DWORD size = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"HiddenDevices", RRF_RT_REG_MULTI_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS || !size)
        return;
    std::vector<wchar_t> buf(size / sizeof(wchar_t) + 1, 0);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"HiddenDevices", RRF_RT_REG_MULTI_SZ, nullptr, buf.data(), &size) != ERROR_SUCCESS)
        return;
    for (const wchar_t* p = buf.data(); *p; p += wcslen(p) + 1) g_hidden.insert(p);
}

void SaveHidden() {
    std::wstring multi;
    for (const auto& k : g_hidden) { multi += k; multi.push_back(L'\0'); }
    multi.push_back(L'\0');
    if (g_hidden.empty()) multi.push_back(L'\0');  // REG_MULTI_SZ requires two nulls when empty
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"HiddenDevices", REG_MULTI_SZ, multi.data(), (DWORD)(multi.size() * sizeof(wchar_t)));
}

bool IsHidden(const std::wstring& key) { return g_hidden.count(key) != 0; }

bool LoadWidgetMode() {
    DWORD v = 1, sz = sizeof(v);
    RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"TaskbarWidget", RRF_RT_REG_DWORD, nullptr, &v, &sz);
    return v != 0;
}

palette::IconStyle LoadIconStyle() {
    DWORD value = 0, bytes = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, L"IconStyle", RRF_RT_REG_DWORD, nullptr, &value, &bytes) != ERROR_SUCCESS || value > 2)
        value = 0;
    return static_cast<palette::IconStyle>(value);
}

void SaveIconStyle(palette::IconStyle style) {
    DWORD value = static_cast<DWORD>(style);
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"IconStyle", REG_DWORD, &value, sizeof(value));
}

void SaveWidgetMode(bool on) {
    DWORD v = on ? 1 : 0;
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, L"TaskbarWidget", REG_DWORD, &v, sizeof(v));
}

bool AutostartEnabled() {
    wchar_t buf[MAX_PATH * 2];
    DWORD sz = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, buf, &sz) != ERROR_SUCCESS) return false;
    return _wcsicmp(buf, (L"\"" + ExePath() + L"\"").c_str()) == 0;
}

void SetAutostart(bool on) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    if (on) {
        std::wstring v = L"\"" + ExePath() + L"\"";
        RegSetValueExW(k, kRunValue, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(k, kRunValue);
    }
    RegCloseKey(k);
}

// ---- icon rendering ---------------------------------------------------------------------------
// Draws the percentage as large digits with a small level bar underneath. GDI text is rendered
// white-on-black into a scratch bitmap and its brightness is used as the alpha channel, which
// gives smooth edges on the transparent icon.

struct Rgb { BYTE r, g, b; };

HICON RenderIcon(const std::wstring& text, int barPct, Rgb textColor, Rgb barColor, bool dim) {
    HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    UINT dpi = taskbar ? GetDpiForWindow(taskbar) : 0;
    const int S = GetSystemMetricsForDpi(SM_CXSMICON, dpi ? dpi : GetDpiForSystem());
    const int barH = std::max(2, S / 8);
    const int textH = S - barH - 1;

    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    // scratch bitmap for the glyphs
    const int W = S * 3, H = S * 2;
    bi.bmiHeader.biWidth = W;
    bi.bmiHeader.biHeight = -H;
    void* sbits = nullptr;
    HBITMAP scratch = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &sbits, nullptr, 0);
    HGDIOBJ oldBmp = SelectObject(dc, scratch);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    auto* sp = static_cast<DWORD*>(sbits);

    int bx0 = 0, by0 = 0, bx1 = -1, by1 = -1;
    for (int fh = textH * 2; fh >= 6; fh--) {
        HFONT f = CreateFontW(-fh, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HGDIOBJ oldF = SelectObject(dc, f);
        SetTextCharacterExtra(dc, text.size() >= 3 ? -1 : 0);
        memset(sbits, 0, (size_t)W * H * 4);
        TextOutW(dc, 2, 0, text.c_str(), (int)text.size());
        GdiFlush();
        SelectObject(dc, oldF);
        DeleteObject(f);
        bx0 = W; by0 = H; bx1 = -1; by1 = -1;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                if (sp[y * W + x] & 0xFF) { bx0 = std::min(bx0, x); bx1 = std::max(bx1, x); by0 = std::min(by0, y); by1 = std::max(by1, y); }
        if (bx1 < 0) break;
        if (bx1 - bx0 + 1 <= S && by1 - by0 + 1 <= textH) break;
    }

    // final icon bitmap
    bi.bmiHeader.biWidth = S;
    bi.bmiHeader.biHeight = -S;
    void* ibits = nullptr;
    HBITMAP color = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &ibits, nullptr, 0);
    auto* ip = static_cast<DWORD*>(ibits);
    memset(ip, 0, (size_t)S * S * 4);
    const int alphaScale = dim ? 140 : 255;
    auto put = [&](int x, int y, Rgb c, int a) {
        if (x < 0 || y < 0 || x >= S || y >= S || a <= 0) return;
        a = a * alphaScale / 255;
        ip[y * S + x] = ((DWORD)a << 24) | ((DWORD)c.r << 16) | ((DWORD)c.g << 8) | c.b;  // straight alpha
    };
    if (bx1 >= 0) {
        int gw = bx1 - bx0 + 1, gh = by1 - by0 + 1;
        int ox = (S - gw) / 2, oy = (textH - gh) / 2;
        for (int y = 0; y < gh; y++)
            for (int x = 0; x < gw; x++)
                put(ox + x, oy + y, textColor, sp[(by0 + y) * W + (bx0 + x)] & 0xFF);
    }
    // level bar: faint track + filled part
    int fill = barPct < 0 ? 0 : std::max(barPct > 0 ? 1 : 0, (barPct * S + 50) / 100);
    for (int y = S - barH; y < S; y++)
        for (int x = 0; x < S; x++) {
            if (x < fill) put(x, y, barColor, 255);
            else put(x, y, palette::GetIconStyle() == palette::IconStyle::White ? Rgb{ 255, 255, 255 } : (g_lightTaskbar ? Rgb{ 0, 0, 0 } : Rgb{ 255, 255, 255 }), 70);
        }

    SelectObject(dc, oldBmp);
    DeleteObject(scratch);
    DeleteDC(dc);

    HBITMAP mask = CreateBitmap(S, S, 1, 1, nullptr);
    {
        HDC mdc = CreateCompatibleDC(nullptr);
        HGDIOBJ o = SelectObject(mdc, mask);
        PatBlt(mdc, 0, 0, S, S, BLACKNESS);
        SelectObject(mdc, o);
        DeleteDC(mdc);
    }
    ICONINFO ii{ TRUE, 0, 0, mask, color };
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(color);
    return icon;
}

Rgb Foreground() { auto c=palette::Foreground(); return Rgb{ (BYTE)c.r, (BYTE)c.g, (BYTE)c.b }; }

HICON IconFor(int pct, bool charging, bool dim, DeviceKind kind, const std::wstring& explicitLabel = L"") {
    auto toRgb = [](palette::Color c) { return Rgb{ (BYTE)c.r, (BYTE)c.g, (BYTE)c.b }; };
    const Rgb shade = toRgb(palette::Device(kind));
    const Rgb low = toRgb(palette::LowBattery());
    const Rgb bar = charging ? toRgb(palette::Charging())
                  : (pct >= 0 && pct <= LOW_BATTERY) ? low : shade;
    const Rgb text = (!charging && pct >= 0 && pct <= LOW_BATTERY) ? low : shade;
    std::wstring t = !explicitLabel.empty() ? explicitLabel : (pct < 0 ? L"?" : std::to_wstring(pct));
    return RenderIcon(t, pct, text, bar, dim);
}

// ---- tray management --------------------------------------------------------------------------

void CopyTip(wchar_t* dst, size_t cap, const std::wstring& s) {
    wcsncpy_s(dst, cap, s.c_str(), _TRUNCATE);
}

std::wstring StatusText(const TrayEntry& e) {
    const auto& s = e.st;
    if (!s.connected) return L"off / not connected";
    std::wstring t;
    if (s.asleep && s.percent >= 0)
        t = (s.levelText.empty() ? std::to_wstring(s.percent) + L"%" : s.levelText)
            + L" (last known)";
    else if (!s.levelText.empty() && s.percent >= 0) t = s.levelText;
    else if (s.percent >= 0) t = std::to_wstring(s.percent) + L"%";
    else if (e.lastPct >= 0) t = std::to_wstring(e.lastPct) + L"% (last known)";
    else t = L"unknown";
    if (s.estimated && s.percent >= 0) t += L" (controller estimate)";
    if (s.charging) t += L", charging";
    if (s.asleep) t += L", asleep";
    return t;
}

void TrayCall(DWORD msg, UINT uid, HICON icon, const std::wstring& tip, const wchar_t* infoTitle = nullptr,
              const std::wstring& info = L"") {
    NOTIFYICONDATAW nid{ sizeof(nid) };
    nid.hWnd = g_hwnd;
    nid.uID = uid;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = icon;
    CopyTip(nid.szTip, ARRAYSIZE(nid.szTip), tip);
    if (infoTitle) {
        nid.uFlags |= NIF_INFO;
        CopyTip(nid.szInfoTitle, ARRAYSIZE(nid.szInfoTitle), infoTitle);
        CopyTip(nid.szInfo, ARRAYSIZE(nid.szInfo), info);
        nid.dwInfoFlags = NIIF_WARNING;
    }
    Shell_NotifyIconW(msg, &nid);
    if (msg == NIM_ADD) {
        nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &nid);
    }
}

void TrayDelete(UINT uid) {
    NOTIFYICONDATAW nid{ sizeof(nid) };
    nid.hWnd = g_hwnd;
    nid.uID = uid;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void UpdatePlaceholder() {
    if (g_useWidget) {
        if (g_placeholder) { TrayDelete(PLACEHOLDER_ID); g_placeholder = false; }
        return;
    }
    bool any = false;
    for (auto& [k, e] : g_entries) any |= e.added;
    if (!any && !g_placeholder) {
        if (!g_placeholderIcon) g_placeholderIcon = RenderIcon(L"-", -1, Foreground(), Foreground(), true);
        TrayCall(NIM_ADD, PLACEHOLDER_ID, g_placeholderIcon, L"Dray Battery\nNo devices with battery info found");
        g_placeholder = true;
    } else if (any && g_placeholder) {
        TrayDelete(PLACEHOLDER_ID);
        g_placeholder = false;
    }
}

void ApplyEntry(TrayEntry& e) {
    const auto& s = e.st;
    bool visible = s.connected && !g_useWidget && !IsHidden(s.key);
    if (!visible) {
        if (e.added) { TrayDelete(e.uid); e.added = false; }
        return;
    }
    int shown = s.percent >= 0 ? s.percent : e.lastPct;
    bool dim = s.asleep || s.percent < 0;
    std::wstring label = s.approximate && !s.levelText.empty() ? s.levelText.substr(0, 1) : L"";
    std::wstring sig = std::to_wstring(shown) + label + (s.charging ? L"c" : L"") + (dim ? L"d" : L"") + (g_lightTaskbar ? L"l" : L"k") + std::to_wstring(static_cast<DWORD>(palette::GetIconStyle()));
    if (sig != e.iconSig || !e.icon) {
        if (e.icon) DestroyIcon(e.icon);
        e.icon = IconFor(shown, s.charging, dim, s.kind, label);
        e.iconSig = sig;
    }
    std::wstring tip = s.name + L"\n" + StatusText(e);

    const wchar_t* infoTitle = nullptr;
    std::wstring info;
    bool coarseLow = s.approximate && (s.levelText == L"Low" || s.levelText == L"Empty");
    if (s.charging || (s.approximate ? !coarseLow : shown > LOW_BATTERY + 10)) e.lowWarned = false;
    if (!s.charging && !s.asleep && s.percent >= 0 && (s.percent <= LOW_BATTERY || coarseLow) && !e.lowWarned) {
        e.lowWarned = true;
        infoTitle = L"Low battery";
        info = s.name + L": " + (s.approximate ? s.levelText : std::to_wstring(s.percent) + L"%");
    }
    TrayCall(e.added ? NIM_MODIFY : NIM_ADD, e.uid, e.icon, tip, infoTitle, info);
    e.added = true;
}

int KindOrder(DeviceKind k) {  // headset first, then mouse, ... (matches the left-to-right layout)
    switch (k) {
        case DeviceKind::Headset:  return 0;
        case DeviceKind::Mouse:    return 1;
        case DeviceKind::Keyboard: return 2;
        case DeviceKind::Gamepad:  return 3;
        default:                   return 4;
    }
}

void UpdateWidget() {
    if (!g_useWidget) return;
    std::vector<const TrayEntry*> list;
    for (auto& [k, e] : g_entries)
        if (e.st.connected && !IsHidden(k)) list.push_back(&e);
    std::sort(list.begin(), list.end(), [](const TrayEntry* a, const TrayEntry* b) {
        int ka = KindOrder(a->st.kind), kb = KindOrder(b->st.kind);
        return ka != kb ? ka < kb : a->st.name < b->st.name;
    });
    std::vector<WidgetItem> items;
    std::wstring tip;
    for (auto* e : list) {
        WidgetItem it;
        it.kind = e->st.kind;
        it.percent = e->st.percent >= 0 ? e->st.percent : e->lastPct;
        if (e->st.percent >= 0) it.text = e->st.levelText;
        it.charging = e->st.charging;
        it.dim = e->st.asleep || e->st.percent < 0;
        it.low = !it.charging && it.percent >= 0 &&
                 (it.percent <= LOW_BATTERY || (e->st.approximate && (e->st.levelText == L"Low" || e->st.levelText == L"Empty")));
        it.bluetooth = e->st.via == L"Bluetooth";
        items.push_back(it);
        if (!tip.empty()) tip += L"\n";
        tip += e->st.name + L": " + StatusText(*e);
    }
    if (tip.empty()) tip = L"Dray Battery: no devices with battery info found";
    widget::SetItems(items, tip);
}

void OnResults(std::vector<DeviceStatus>& results) {
    std::map<std::wstring, bool> seen;
    for (auto& s : results) {
        auto& e = g_entries[s.key];
        if (!e.uid) e.uid = StableId(s.key);
        e.st = s;
        if (s.percent >= 0) e.lastPct = s.percent;
        e.lastSeen = GetTickCount64();
        seen[s.key] = true;
        ApplyEntry(e);
    }
    for (auto it = g_entries.begin(); it != g_entries.end();) {
        auto& e = it->second;
        // Some receivers drop sleeping devices from the link; keep showing the last value for a while.
        bool inGrace = !seen.count(it->first) && e.st.graceSec > 0 && e.st.connected &&
                       GetTickCount64() - e.lastSeen < (ULONGLONG)e.st.graceSec * 1000;
        if (inGrace) {
            e.st.percent = -1;
            e.st.asleep = true;
            ApplyEntry(e);
        }
        if (!seen.count(it->first) && !inGrace) {
            if (it->second.added) TrayDelete(it->second.uid);
            if (it->second.icon) DestroyIcon(it->second.icon);
            it = g_entries.erase(it);
        } else {
            ++it;
        }
    }
    UpdatePlaceholder();
    UpdateWidget();
}

void RedrawAll();

// Makes the actual display match the chosen mode; falls back to tray icons if the taskbar
// widget cannot be created (e.g. Explorer not ready yet). Called once a second.
void Reconcile() {
    const bool paletteChanged = palette::UpdateFromTaskbar();
    bool alive = g_wantWidget && widget::Alive();
    if (g_wantWidget && !alive) alive = widget::Create(g_inst, g_hwnd, WM_WIDGET_CLICK, g_lightTaskbar);
    if (!g_wantWidget) widget::Destroy();
    bool changed = alive != g_useWidget;
    g_useWidget = alive;
    if (changed) {
        for (auto& [k, e] : g_entries) ApplyEntry(e);
        UpdatePlaceholder();
        UpdateWidget();
    } else if (g_useWidget) {
        widget::Reposition();
    }
    if (paletteChanged) {
        RedrawAll();
        widget::SetLightTheme(g_lightTaskbar);
    }
}

void SetMode(bool useWidget) {
    g_wantWidget = useWidget;
    SaveWidgetMode(useWidget);
    Reconcile();
}

void ReAddAll() {
    g_placeholder = false;
    for (auto& [k, e] : g_entries) { e.added = false; ApplyEntry(e); }
    UpdatePlaceholder();
}

void RedrawAll() {
    if (g_placeholderIcon) {
        DestroyIcon(g_placeholderIcon);
        g_placeholderIcon = nullptr;
        if (g_placeholder) { TrayDelete(PLACEHOLDER_ID); g_placeholder = false; }
    }
    for (auto& [k, e] : g_entries) { e.iconSig.clear(); ApplyEntry(e); }
    UpdatePlaceholder();
}

void SetAppearanceChoice(palette::IconStyle style) {
    if (palette::GetIconStyle() == style) return;
    palette::SetIconStyle(style);
    SaveIconStyle(style);
    RedrawAll();
    // The taskbar widget renders from the same palette; repaint it immediately.
    if (g_useWidget) widget::SetLightTheme(g_lightTaskbar);
}

void LaunchXboxBatteryReader() {
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr,path,MAX_PATH)) return;
    std::wstring command = L"\"" + std::wstring(path) + L"\" --xbox-refresh";
    STARTUPINFOW startup{ sizeof(startup) };
    PROCESS_INFORMATION child{};
    if (CreateProcessW(path,command.data(),nullptr,nullptr,FALSE,0,
                       nullptr,nullptr,&startup,&child)) {
        CloseHandle(child.hThread);
        CloseHandle(child.hProcess);
    }
}

void ShowMenu() {
    HMENU m = CreatePopupMenu();
    std::vector<std::wstring> keys;  // menu id ID_DEVICE_FIRST + i -> device key
    if (g_entries.empty()) AppendMenuW(m, MF_STRING | MF_GRAYED, 0, L"No devices with battery info found");
    else AppendMenuW(m, MF_STRING | MF_GRAYED, 0, L"Click a device to show or hide it");
    for (auto& [k, e] : g_entries) {
        std::wstring line = e.st.name + L":  " + StatusText(e) + L"   [" + e.st.via + L"]";
        AppendMenuW(m, MF_STRING | (IsHidden(k) ? 0 : MF_CHECKED), ID_DEVICE_FIRST + (UINT)keys.size(), line.c_str());
        keys.push_back(k);
    }
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (g_wantWidget ? MF_CHECKED : 0), ID_MODE_WIDGET, L"Show on the taskbar (icon + percentage)");
    AppendMenuW(m, MF_STRING | (!g_wantWidget ? MF_CHECKED : 0), ID_MODE_TRAY, L"Show as tray icons");
    CheckMenuRadioItem(m, ID_MODE_WIDGET, ID_MODE_TRAY, g_wantWidget ? ID_MODE_WIDGET : ID_MODE_TRAY, MF_BYCOMMAND);
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    HMENU appearance = CreatePopupMenu();
    auto style = palette::GetIconStyle();
    AppendMenuW(appearance, MF_STRING | (style == palette::IconStyle::Colored ? MF_CHECKED : 0), ID_STYLE_COLORED, L"Colored (adaptive)");
    AppendMenuW(appearance, MF_STRING | (style == palette::IconStyle::White ? MF_CHECKED : 0), ID_STYLE_WHITE, L"White (classic)");
    AppendMenuW(appearance, MF_STRING | (style == palette::IconStyle::Monochrome ? MF_CHECKED : 0), ID_STYLE_MONOCHROME, L"Monochrome (adaptive)");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)appearance, L"Icon appearance");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_REFRESH, L"Refresh now");
    if (IsWolverineXboxPresent())
        AppendMenuW(m, MF_STRING, ID_REFRESH_XBOX,
            L"Read Wolverine battery (Xbox mode)...");
    AppendMenuW(m, MF_STRING | (AutostartEnabled() ? MF_CHECKED : 0), ID_AUTOSTART, L"Start with Windows");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_EXIT, L"Exit");
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);
    UINT cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, nullptr);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(m);
    switch (cmd) {
        case ID_REFRESH: SetEvent(g_refreshEvt); break;
        case ID_REFRESH_XBOX: LaunchXboxBatteryReader(); break;
        case ID_STYLE_COLORED: SetAppearanceChoice(palette::IconStyle::Colored); break;
        case ID_STYLE_WHITE: SetAppearanceChoice(palette::IconStyle::White); break;
        case ID_STYLE_MONOCHROME: SetAppearanceChoice(palette::IconStyle::Monochrome); break;
        case ID_AUTOSTART: SetAutostart(!AutostartEnabled()); break;
        case ID_MODE_WIDGET: SetMode(true); break;
        case ID_MODE_TRAY: SetMode(false); break;
        case ID_EXIT: DestroyWindow(g_hwnd); break;
        default:
            if (cmd >= ID_DEVICE_FIRST && cmd < ID_DEVICE_FIRST + keys.size()) {
                const std::wstring& key = keys[cmd - ID_DEVICE_FIRST];
                if (!g_hidden.erase(key)) g_hidden.insert(key);
                SaveHidden();
                auto it = g_entries.find(key);
                if (it != g_entries.end()) ApplyEntry(it->second);
                UpdatePlaceholder();
                UpdateWidget();
            }
    }
}

// ---- worker thread ----------------------------------------------------------------------------

// An active Logitech HID++ receiver may temporarily withhold its battery report.
// Unlike a disconnected peripheral, the receiver's HID interface is still present
// in Windows. Retry missing receiver reports throughout the session, not just at
// program startup. This avoids needing a manual "Refresh now" after a dropout.
bool MissingLogitechReceiverReport(const std::vector<DeviceStatus>& devices) {
    std::set<USHORT> receiverPids;
    for (const HidColl& c : EnumHid()) {
        if (c.vid != 0x046D || c.page() != 0xFF00) continue;
        if ((c.usage() == 0x0001 && c.outLen() == 7) ||
            (c.usage() == 0x0002 && c.outLen() == 20))
            receiverPids.insert(c.pid);
    }
    for (USHORT pid : receiverPids) {
        const std::wstring prefix = L"logi:" + Hex4(pid) + L":";
        bool returned = false;
        for (const DeviceStatus& device : devices) {
            if (device.key.compare(0, prefix.size(), prefix) == 0) {
                returned = true;
                break;
            }
            for (const auto& source : device.sourceKeys)
                if (source.compare(0, prefix.size(), prefix) == 0) {
                    returned = true;
                    break;
                }
            if (returned) break;
        }
        if (!returned) return true;
    }
    return false;
}

// Optional diagnostics. Writes nothing unless DRAYBATTERY_TRACE=1 is supplied
// to this process, so normal builds have no recurring disk writes.
void TracePoll(const std::vector<DeviceStatus>& statuses, bool missing, unsigned attempt, DWORD interval) {
    wchar_t flag[8]{};
    if (!GetEnvironmentVariableW(L"DRAYBATTERY_TRACE", flag, ARRAYSIZE(flag)) ||
        flag[0] != L'1') return;
    wchar_t path[MAX_PATH]{};
    DWORD n = GetTempPathW(MAX_PATH, path);
    if (!n || n >= MAX_PATH - 50) return;
    wcscat_s(path, L"DrayBattery-poll-trace.txt");

    std::wstring line = std::to_wstring(GetTickCount64()) +
       L" ms; missing=" + (missing ? L"1" : L"0") +
       L"; attempt=" + std::to_wstring(attempt) +
       L"; next_ms=" + std::to_wstring(interval) +
       L"; count=" + std::to_wstring(statuses.size());
    for (const auto& d : statuses) {
        line += L"; " + d.key + L"=" +
            (d.percent >= 0 ? std::to_wstring(d.percent) : L"?");
    }
    line += L"\r\n";
    int bytes = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.size(), nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return;
    std::string utf8((size_t)bytes, 0);
    WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.size(), utf8.data(), bytes, nullptr, nullptr);

    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
        CloseHandle(file);
    }
}

DWORD WINAPI Worker(LPVOID) {
    // Both startup and ongoing Logitech receiver misses get bounded retries.
    unsigned completedStartupWaits = 0;
    unsigned consecutiveMissingPolls = 0;
    for (;;) {
        auto* v = new std::vector<DeviceStatus>(PollAllDevices());
        const bool missing = MissingLogitechReceiverReport(*v);
        if (missing && consecutiveMissingPolls < 1000)
            ++consecutiveMissingPolls;
        else if (!missing)
            consecutiveMissingPolls = 0;

        DWORD interval = draybattery::PollIntervalMs(completedStartupWaits);
        if (missing)
            interval = std::min(interval,
                (DWORD)draybattery::MissingReceiverRetryMs(consecutiveMissingPolls));

        TracePoll(*v, missing, consecutiveMissingPolls, interval);
        if (!PostMessageW(g_hwnd, WM_RESULTS, 0, (LPARAM)v)) delete v;
        HANDLE hs[2] = { g_stopEvt, g_refreshEvt };
        DWORD result = WaitForMultipleObjects(2, hs, FALSE, interval);
        if (result == WAIT_OBJECT_0 || result == WAIT_FAILED) return 0;
        if (result == WAIT_TIMEOUT && completedStartupWaits < 4)
            ++completedStartupWaits;
    }
}

void RefreshSoon(UINT ms) { SetTimer(g_hwnd, TIMER_DEBOUNCE, ms, nullptr); }

LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == g_taskbarCreatedMsg && msg != 0) {  // Explorer restarted
        widget::Destroy();
        g_useWidget = false;
        ReAddAll();
        Reconcile();
        return 0;
    }
    switch (msg) {
        case WM_RESULTS: {
            auto* v = reinterpret_cast<std::vector<DeviceStatus>*>(lp);
            OnResults(*v);
            delete v;
            return 0;
        }
        case WM_NOTIFY: {
            const NMHDR* notification = reinterpret_cast<const NMHDR*>(lp);
            if (notification && notification->code == TTN_SHOW &&
                widget::PositionTooltipOnShow(notification->hwndFrom)) return TRUE;
            break;
        }
        case WM_TRAY:
            switch (LOWORD(lp)) {
                case WM_CONTEXTMENU:
                case NIN_SELECT:
                case NIN_KEYSELECT: ShowMenu(); break;
            }
            return 0;
        case WM_WIDGET_CLICK:
            ShowMenu();
            return 0;
        case WM_XBOX_BATTERY_UPDATED:
            if(g_refreshEvt) SetEvent(g_refreshEvt);
            return 0;
        case WM_ENDSESSION:  // sign-out / shutdown / installer restart manager
            if (wp) DestroyWindow(h);
            return 0;
        case WM_DEVICECHANGE:
            if (wp == DBT_DEVICEARRIVAL || wp == DBT_DEVICEREMOVECOMPLETE) RefreshSoon(2500);
            return TRUE;
        case WM_POWERBROADCAST:
            if (wp == PBT_APMRESUMEAUTOMATIC) RefreshSoon(5000);
            return TRUE;
        case WM_TIMER:
            if (wp == TIMER_DEBOUNCE) { KillTimer(h, TIMER_DEBOUNCE); SetEvent(g_refreshEvt); }
            if (wp == TIMER_WIDGET) Reconcile();
            return 0;
        case WM_SETTINGCHANGE:
            if (lp && !lstrcmpW((LPCWSTR)lp, L"ImmersiveColorSet")) {
                bool light = ReadLightTaskbar();
                if (light != g_lightTaskbar) { g_lightTaskbar = light; palette::SetFallbackLight(light); RedrawAll(); widget::SetLightTheme(light); }
            }
            return 0;
        case WM_DESTROY:
            KillTimer(h, TIMER_WIDGET);
            widget::Destroy();
            SetEvent(g_stopEvt);
            if (g_worker) WaitForSingleObject(g_worker, 5000);
            for (auto& [k, e] : g_entries) {
                if (e.added) TrayDelete(e.uid);
                if (e.icon) DestroyIcon(e.icon);
            }
            if (g_placeholder) TrayDelete(PLACEHOLDER_ID);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// `DrayBattery.exe --list` prints what it finds, `--dump` every HID collection.
int ListToConsole(bool dump) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!out || out == INVALID_HANDLE_VALUE) {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) out = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    }
    auto write = [&](const std::wstring& s) {
        int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
        std::string u(n, 0);
        WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), u.data(), n, nullptr, nullptr);
        DWORD w;
        WriteFile(out, u.data(), (DWORD)u.size(), &w, nullptr);
    };
    if (dump) {
        write(DumpHidCollections());
        return 0;
    }
    auto list = PollAllDevices();
    if (list.empty()) write(L"No devices with battery info found.\n");
    for (auto& s : list) {
        std::wstring line = s.name + L"  [" + s.via + L"]  ";
        if (!s.connected) line += L"off / not connected";
        else if (s.asleep) line += s.percent >= 0 ?
            (s.levelText.empty() ? std::to_wstring(s.percent) + L"%" : s.levelText)
            + L" (last known, asleep)" : L"asleep";
        else if (!s.levelText.empty()) line += s.levelText;
        else if (s.percent >= 0) line += std::to_wstring(s.percent) + L"%";
        else line += L"unknown";
        if (s.charging) line += L", charging";
        if (s.millivolts) line += L", " + std::to_wstring(s.millivolts) + L" mV";
        write(line + L"   (" + s.key + L")\n");
    }
    return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int) {
    if (cmdLine && wcsstr(cmdLine, L"--xbox-refresh")) {
        int result = RunWolverineXboxRefresh();
        if (result == 0)
            if (HWND parent = FindWindowW(L"DrayBatteryWnd", nullptr))
                PostMessageW(parent, WM_APP + 4, 0, 0);
        return result;
    }
    if (cmdLine && wcsstr(cmdLine, L"--list")) return ListToConsole(false);
    if (cmdLine && wcsstr(cmdLine, L"--dump")) return ListToConsole(true);
    if (cmdLine && wcsstr(cmdLine, L"--quit")) {  // used by the installer before upgrading/uninstalling
        if (HWND w = FindWindowW(L"DrayBatteryWnd", nullptr)) {
            PostMessageW(w, WM_CLOSE, 0, 0);
            for (int i = 0; i < 50 && FindWindowW(L"DrayBatteryWnd", nullptr); i++) Sleep(100);
        }
        return 0;
    }
    g_inst = inst;

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\DrayBatteryTray");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    g_lightTaskbar = ReadLightTaskbar();
    palette::SetFallbackLight(g_lightTaskbar);
    palette::SetIconStyle(LoadIconStyle());
    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"DrayBatteryWnd";
    RegisterClassExW(&wc);
    // A hidden top-level window (not message-only) so it receives the TaskbarCreated broadcast.
    g_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, kAppName, WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
    if (!g_hwnd) return 1;

    DEV_BROADCAST_DEVICEINTERFACE_W filter{ sizeof(filter) };
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    filter.dbcc_classguid = GUID_DEVINTERFACE_HID;
    HDEVNOTIFY notify = RegisterDeviceNotificationW(g_hwnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);

    g_refreshEvt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_stopEvt = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_wantWidget = LoadWidgetMode();
    LoadHidden();
    Reconcile();
    UpdatePlaceholder();  // something to click on until the first poll finishes
    SetTimer(g_hwnd, TIMER_WIDGET, 1000, nullptr);
    g_worker = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    // drain results posted after shutdown started
    while (PeekMessageW(&m, nullptr, WM_RESULTS, WM_RESULTS, PM_REMOVE)) delete reinterpret_cast<std::vector<DeviceStatus>*>(m.lParam);
    if (notify) UnregisterDeviceNotification(notify);
    if (g_worker) CloseHandle(g_worker);
    ReleaseMutex(mutex);
    return 0;
}
