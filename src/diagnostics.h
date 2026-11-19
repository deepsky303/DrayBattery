#pragma once
#include <string>
#include <windows.h>
constexpr UINT WM_DRAY_READ_STATUS = WM_APP + 5;
// OS metadata/status only. Never sends HID feature/output or GIP commands.
std::wstring DiagnoseConnectedDevices();
std::wstring DiagnoseBluetooth();
std::wstring DiagnoseWindowsControllers();
std::wstring DiagnoseRunningApp();
