#pragma once
#include <string>
namespace draybattery::device_db {

// No network access in either of these read-only functions.
std::wstring IdentifyConnectedHid();
std::wstring CacheInfo();

// Explicit opt-in: downloads the public usb.ids catalogue (never sends VID/PID).
// Existing cached data is kept intact on any failure.
bool UpdateNames(std::wstring& result);

} // namespace draybattery::device_db
