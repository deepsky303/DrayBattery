#pragma once
#include <string>
#include <vector>
#include <cstdint>

enum class DeviceKind { Other, Mouse, Headset, Keyboard, Gamepad };
enum class LinkKind { Unknown, UsbReceiver, Bluetooth, UsbCable };
enum class Presence { Unknown, Present, Absent };

// Physical inventory is independent of protocol replies and battery availability.
// A complete HID inventory proves USB absence, never Bluetooth disconnection.
struct PhysicalPresence {
    std::vector<std::wstring> usbPresent;
    std::vector<std::wstring> btConnected, btDisconnected, btUnknown;
    bool usbComplete = false, bluetoothComplete = false;
    std::uint64_t observedMs = 0;
};

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
    std::wstring connectionId; // actual USB/BT instance, never just VID/PID/model
    bool receiverBacked = false;
    LinkKind linkKind = LinkKind::Unknown;
    bool batteryFresh = false; // successful acquisition, not a cached property/replayed status
    bool batteryCached = false;
    bool windowsBatteryProperty = false; // OS-reported level; never a live acquisition
    std::wstring batteryPropertyId; // exact PnP node that supplied the Windows property
    std::uint64_t batteryObservedMs = 0; // time we READ the property, not battery measurement time
    bool presenceUncertain = false;
    bool ghub = false;         // eligible for read-only localhost status refreshes
    std::uint64_t lastLiveMs = 0; // monotonic acquisition time; zero means no live sample
    std::wstring via;          // "2.4 GHz receiver", "Bluetooth", "USB cable", ...
    DeviceKind kind = DeviceKind::Other;
    int  percent    = -1;      // 0..100, or -1 when unknown right now
    std::wstring levelText;    // set when the device only reports coarse levels ("Low", "Full"...)
    bool approximate = false; // never present XInput categories as exact battery percentages
    bool estimated = false;   // numeric value is converted from a controller-reported raw level
    bool charging   = false;
    bool asleep     = false;   // dim last reported value; does not prove hardware sleep
    bool connected  = true;    // false: confirmed disconnect; radio sleep uses asleep
    int  millivolts = 0;       // battery voltage when reported (0 = unknown)
};

// Queries every supported device. Blocking (can take a second or two); call from a worker thread.
std::vector<DeviceStatus> PollAllDevices();

// Worker -> UI snapshot. Partial snapshots only replace the listed connections.
struct DevicePoll {
    std::vector<DeviceStatus> devices;
    PhysicalPresence presence;
    std::vector<std::wstring> polledConnections;
    bool complete = true;
    unsigned generation = 0;
};

// Prints every HID collection (for adding support for new devices).
std::wstring DumpHidCollections();
