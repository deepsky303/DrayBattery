#include "diagnostics.h"
#include "hid.h"

std::wstring DiagnoseRunningApp() {
    HWND window = FindWindowW(L"DrayBatteryWnd", nullptr);
    if (!window) return L"DrayBattery UI is not running\n";
    DWORD_PTR version = 0;
    if (!SendMessageTimeoutW(window, WM_DRAY_READ_STATUS, 4, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &version) ||
        version != 2) return L"Running version does not support status diagnostics\n";
    std::wstring text;
    const wchar_t* fields[] = {L"battery_entries", L"visible_entries", L"widget_alive", L"worker_age_ms",
        L"visible_other", L"visible_mice", L"visible_headsets", L"visible_keyboards", L"visible_gamepads",
        L"dimmed_other", L"dimmed_mice", L"dimmed_headsets", L"dimmed_keyboards", L"dimmed_gamepads"};
    for (WPARAM i = 0; i < ARRAYSIZE(fields); ++i) {
        DWORD_PTR value = 0;
        if (!SendMessageTimeoutW(window, WM_DRAY_READ_STATUS, i < 4 ? i : i + 1, 0,
                SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &value))
            return text + L"UI did not answer within 1 second\n";
        text += std::wstring(fields[i]) + L"=" + std::to_wstring(value) + L"\n";
    }
    return text;
}

std::wstring DiagnoseConnectedDevices() {
    std::wstring text = L"Read-only OS diagnostics (no vendor battery commands)\n\nBluetooth:\n";
    text += DiagnoseBluetooth();
    text += L"\nControllers (OS reports, not authenticated vendor battery readings):\n";
    text += DiagnoseWindowsControllers();
    text += L"\nWolverine HID descriptors:\n";
    bool complete = false, found = false;
    for (const auto& c : EnumHid(&complete)) if (c.vid == 0x1532) {
        found = true;
        text += Hex4(c.vid) + L":" + Hex4(c.pid) + L" if=" + std::to_wstring(c.iface) +
            L" page=" + Hex4(c.page()) + L" usage=" + Hex4(c.usage()) +
            L" feature=" + std::to_wstring(c.featLen()) + L" " + HidProduct(c) + L"\n";
    }
    if (!found) text += L"No Razer HID interfaces present\n";
    text += complete ? L"HID inventory complete\n" : L"HID inventory incomplete\n";
    return text;
}
