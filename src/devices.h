#pragma once
#include <string>
#include <vector>

enum class DeviceKind { Other, Mouse, Headset, Keyboard, Gamepad };

// One battery-powered device, as seen during a single poll.
struct DeviceStatus {
    std::wstring key;          // stable identity across polls (used for the tray icon id)
    std::wstring name;         // human readable name
    // Optional verified, transport-independent identity (serial/vendor device signature).
    // Not derived solely from the model name: different identical units stay distinct.
    std::wstring physicalId;
    // All low-level sources combined into this logical device, for
    // receiver health monitoring without losing transport-specific IDs.
    std::vector<std::wstring> sourceKeys;
    std::wstring via;          // "2.4 GHz receiver", "Bluetooth", "USB cable", ...
    DeviceKind kind = DeviceKind::Other;
    int  percent    = -1;      // 0..100, or -1 when unknown right now
    std::wstring levelText;    // set when the device only reports coarse levels ("Low", "Full"...)
    bool approximate = false; // never present XInput categories as exact battery percentages
    bool estimated = false;   // numeric value is converted from a controller-reported raw level
    bool charging   = false;
    bool asleep     = false;   // receiver answered but the device is sleeping (keep last value)
    bool connected  = true;    // false: receiver present, device switched off (hidden)
    int  millivolts = 0;       // battery voltage when reported (0 = unknown)
    int  graceSec   = 0;       // keep showing the last value this long if the device stops answering
};

// Queries every supported device. Blocking (can take a second or two); call from a worker thread.
std::vector<DeviceStatus> PollAllDevices();

// Prints every HID collection (for adding support for new devices).
std::wstring DumpHidCollections();
