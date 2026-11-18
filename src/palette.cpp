#include "palette.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace palette {
namespace {
Color g_background{ 20, 23, 27 };
bool g_fallbackLight = false;
bool g_sampleValid = false;
IconStyle g_iconStyle = IconStyle::Colored;
ULONGLONG g_lastSample = 0;

double Channel(int v) {
    const double c = std::max(0, std::min(255, v)) / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}
double Luminance(Color c) {
    return 0.2126 * Channel(c.r) + 0.7152 * Channel(c.g) + 0.0722 * Channel(c.b);
}
Color Blend(Color a, Color b, double x) {
    return { (int)std::lround(a.r + (b.r - a.r) * x),
             (int)std::lround(a.g + (b.g - a.g) * x),
             (int)std::lround(a.b + (b.b - a.b) * x) };
}
Color PickContrast(Color bright, Color deep, Color bg) {
    const bool preferBright = ContrastRatio(bright, bg) >= ContrastRatio(deep, bg);
    Color result = preferBright ? bright : deep;
    if (ContrastRatio(result, bg) >= 4.5) return result;
    Color endpoint = preferBright ? Color{ 255, 255, 255 } : Color{ 0, 0, 0 };
    for (int i = 1; i <= 20; ++i) {
        result = Blend(result, endpoint, 0.1);
        if (ContrastRatio(result, bg) >= 4.5) return result;
    }
    return endpoint;
}
Color Middle(std::array<Color, 12> values, int count) {
    std::array<int, 12> r{}, g{}, b{};
    for (int i = 0; i < count; ++i) {
        r[i] = values[i].r;
        g[i] = values[i].g;
        b[i] = values[i].b;
    }
    std::sort(r.begin(), r.begin() + count);
    std::sort(g.begin(), g.begin() + count);
    std::sort(b.begin(), b.begin() + count);
    return { r[count / 2], g[count / 2], b[count / 2] };
}
} // anonymous namespace

void SetIconStyle(IconStyle style) { g_iconStyle = style; }
IconStyle GetIconStyle() { return g_iconStyle; }

double ContrastRatio(Color a, Color b) {
    const double la = Luminance(a), lb = Luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}
void SetFallbackLight(bool light) {
    g_fallbackLight = light;
    if (!g_sampleValid) g_background = light ? Color{ 239, 239, 239 } : Color{ 20, 23, 27 };
}
Color Background() { return g_background; }
bool IsLightBackground() { return Luminance(g_background) > 0.179; }
Color Foreground() {
    if (g_iconStyle == IconStyle::White) return { 255, 255, 255 };
    const Color white{ 255, 255, 255 }, black{ 0, 0, 0 };
    return ContrastRatio(white, g_background) > ContrastRatio(black, g_background) ? white : black;
}
Color Device(DeviceKind kind) {
    if (g_iconStyle != IconStyle::Colored) return Foreground();
    Color bright{}, deep{};
    switch (kind) {
        case DeviceKind::Headset:  bright = { 235, 173, 255 }; deep = { 106, 38, 155 }; break; // lavender
        case DeviceKind::Mouse:    bright = {  95, 227, 255 }; deep = {   0, 79, 126 }; break; // cyan
        case DeviceKind::Keyboard: bright = { 255, 202, 103 }; deep = { 131, 62,   0 }; break; // amber
        case DeviceKind::Gamepad:  bright = { 142, 252, 149 }; deep = {  10,101,  54 }; break; // green
        default:                   bright = { 255, 169, 199 }; deep = { 133, 29,  73 }; break; // pink
    }
    return PickContrast(bright, deep, g_background);
}
Color LowBattery() {
    if (g_iconStyle != IconStyle::Colored) return Foreground();
    return PickContrast({ 255, 143, 137 }, { 145, 21, 35 }, g_background);
}
Color Charging() {
    if (g_iconStyle != IconStyle::Colored) return Foreground();
    return PickContrast({ 104, 226, 255 }, { 0, 82, 144 }, g_background);
}

bool UpdateFromTaskbar() {
    const ULONGLONG now = GetTickCount64();
    if (now - g_lastSample < 1900) return false;
    g_lastSample = now;

    HWND tb = FindWindowW(L"Shell_TrayWnd", nullptr);
    RECT bar{};
    if (!tb || !GetWindowRect(tb, &bar)) return false;
    const int width = bar.right - bar.left, height = bar.bottom - bar.top;
    if (width < 300 || height < 20 || height > 200) return false;
    MONITORINFO mi{ sizeof(mi) };
    HMONITOR monitor = MonitorFromWindow(tb, MONITOR_DEFAULTTONEAREST);
    if (!GetMonitorInfoW(monitor, &mi)) return false;
    // Windows can slide auto-hidden taskbars beyond the monitor edge.
    // Sampling off-screen pixels would incorrectly treat the background as black.
    if (bar.top >= mi.rcMonitor.bottom - 6 || bar.bottom > mi.rcMonitor.bottom + 3) return false;

    HWND own = FindWindowExW(tb, nullptr, L"DrayBatteryWidget", nullptr);
    RECT wr{};
    if (!own || !GetWindowRect(own, &wr)) {
        wr.left = bar.right - 250;
        wr.right = bar.right - 160;
    }

    // Sample near but outside our own translucent widget, where the taskbar's
    // wallpaper/accent is visible. Spread points and use median to ignore icons.
    const std::array<int, 4> xs{ wr.left - 13, wr.left - 29, wr.left - 47, wr.left - 72 };
    const std::array<int, 3> ys{ bar.top + height / 4, bar.top + height / 2, bar.bottom - height / 4 };
    HDC dc = GetDC(nullptr);
    if (!dc) return false;
    std::array<Color, 12> samples{};
    int n = 0;
    for (int x : xs) for (int y : ys) {
        if (x < bar.left + 4 || x >= bar.right - 4) continue;
        COLORREF raw = GetPixel(dc, x, y);
        if (raw == CLR_INVALID) continue;
        samples[n++] = { (int)GetRValue(raw), (int)GetGValue(raw), (int)GetBValue(raw) };
    }
    ReleaseDC(nullptr, dc);
    if (n < 5) return false;
    const Color next = Middle(samples, n);
    if (!g_sampleValid) {
        g_background = next;
        g_sampleValid = true;
        return true;
    }

    const int delta = std::max({std::abs(next.r - g_background.r),
                                std::abs(next.g - g_background.g),
                                std::abs(next.b - g_background.b)});
    // Avoid redrawing on subtle anti-aliasing/noise while catching visible changes.
    if (delta < 14) return false;
    g_background = next;
    return true;
}
} // namespace palette
