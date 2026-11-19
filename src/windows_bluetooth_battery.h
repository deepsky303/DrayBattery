#pragma once
// Pure conversion used by the real Windows enumerator and mock acceptance tests.
#include "bluetooth_presence.h"

namespace draybattery {
inline DeviceKind BluetoothDeviceKind(unsigned cod) {
    const auto major = (cod >> 8) & 0x1F, minor = (cod >> 2) & 0x3F;
    if (major == 4) return DeviceKind::Headset;
    if (major == 5) {
        if ((minor & 0x0F) == 1 || (minor & 0x0F) == 2) return DeviceKind::Gamepad;
        if ((minor >> 4) == 1) return DeviceKind::Keyboard;
        if ((minor >> 4) == 2) return DeviceKind::Mouse;
    }
    return DeviceKind::Other;
}
inline std::vector<DeviceStatus> WindowsBluetoothBatteries(
        const std::vector<BluetoothNode>& nodes, const PhysicalPresence& presence) {
    std::vector<DeviceStatus> result;
    for (const auto& id : presence.btConnected) {
        DeviceStatus proof; proof.connectionId = id; proof.linkKind = LinkKind::Bluetooth;
        if (ConnectionPresence(presence, proof) != Presence::Present) continue;
        const BluetoothNode* root = nullptr;
        const BluetoothNode* battery = nullptr;
        for (const auto& n : nodes) {
            if (L"container:" + LowerId(n.container) != id) continue;
            const auto instance = LowerId(n.instance);
            // Host adapters cannot supply a peripheral battery, even with a bad container ID.
            if (instance.rfind(L"bthenum\\", 0) && instance.rfind(L"bthle\\", 0) &&
                instance.rfind(L"bthledevice\\", 0)) continue;
            const bool positiveRoot = BluetoothPeripheralRoot(instance) && n.connection == Presence::Present;
            if (positiveRoot && !root) root = &n;
            if (n.percent >= 0 && n.percent <= 100 && (!battery || positiveRoot)) battery = &n;
        }
        if (!root || !battery) continue;
        DeviceStatus s;
        s.key = L"bt:" + LowerId(root->container);
        s.physicalId = L"bt:" + id;
        s.connectionId = id;
        s.linkKind = LinkKind::Bluetooth;
        s.name = root->name;
        if (s.name.empty()) s.name = L"Bluetooth device";
        s.kind = BluetoothDeviceKind(root->classOfDevice);
        s.via = L"Bluetooth (Windows cached level)";
        s.percent = battery->percent;
        s.batteryCached = true;
        s.windowsBatteryProperty = true;
        s.batteryPropertyId = LowerId(battery->instance);
        s.batteryObservedMs = presence.observedMs;
        s.asleep = true;
        result.push_back(std::move(s));
    }
    return result;
}
} // namespace draybattery
