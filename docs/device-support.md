# Device support, battery protocols and USB catalogue

[Русская версия](device-support.ru.md)

## Three levels of support

| Status | Meaning | Battery reading? |
| --- | --- | --- |
| **Verified on DrayBattery hardware** | We successfully read a real battery report | Yes, in the tested configuration |
| **Protocol implemented** | The source includes a compatible device driver or Windows fallback | Only if the device responds correctly |
| **Identified by the USB database** | A public VID:PID record names the hardware | **No. Identification is not battery monitoring.** |

## Verified on physical hardware

| Device | Connection / source | Status | Notes |
| --- | --- | --- | --- |
| Logitech G903 LIGHTSPEED | Local G HUB | Verified | Battery percentage |
| Logitech G915 X LIGHTSPEED | Local G HUB | Verified | Charging, sleeping and physical identity |
| Razer Wolverine V3 Pro | Windows Mode, 1532:0A4C | Verified | HID battery field, normalized from 0–255 |
| Razer Wolverine V3 Pro | Xbox Mode, 1532:0A3F | Verified | Foreground GIP request and last-known cache |

These are validated protocol reads, not independent measurements of battery capacity.

## Other protocols present in the code

The following are **implemented driver families, not independently tested guarantees for every model**. They depend on exact IDs, interfaces, firmware versions and response formats.

| Device families and examples | Mechanism |
| --- | --- |
| Logitech mice/keyboards/G headsets | HID++ 1.0/2.0; local G HUB |
| Razer mice/keyboards, BlackShark V3 | Vendor HID |
| SteelSeries Arctis / Nova / Aerox / Rival | Vendor HID |
| Corsair Void / HS70 / HS80 / Virtuoso | Vendor HID |
| HyperX Cloud II / III / Alpha / Flight | Vendor HID |
| Glorious and selected Hitscan mice | Vendor HID |
| Cooler Master MM712 / MM731 | Vendor HID |
| Rapoo VT mice | HID receiver reports ([protocol notes](rapoo.md)) |
| ASUS ROG mice and keyboards | Vendor HID |
| BenQ ZOWIE EC-CW / DW | Vendor HID |
| LAMZU / Keychron / Kysona / ATK / VXE / Pulsar | Model-specific protocols |
| AULA wireless keyboards | Vendor HID |
| GameSir Nova family | Vendor HID |
| Sony DualShock 4 / DualSense | Controller reports |
| Xbox controllers | Windows.Gaming.Input; coarse XInput fallback |
| **CHERRY MX 3.0S Wireless** (046A:00E3 / 00EA) | Vendor status report; imported from upstream v1.0.8, not yet hardware-tested in DrayBattery |
| **Valve Steam Controller** (vendor 28DE) | Passive HID battery report; imported from upstream v1.0.8, not yet hardware-tested in DrayBattery |
| Generic HID devices | Standard Battery Strength report |
| Bluetooth peripherals | Windows Bluetooth battery property |
| Additional headsets | Optional external HeadsetControl installation |

The [upstream project](https://github.com/krisgfx9/Wireless-Device-Battery-Monitor) has additional implementations for AirPods, Galaxy Buds, 8BitDo, Audeze, Astro and more. They do **not** become available in DrayBattery automatically: source integration, validation and testing are required.

## Optional, on-demand USB device identification database

DrayBattery can download the public [hwdata usb.ids](https://github.com/vcrhonek/hwdata/blob/master/usb.ids) catalogue **on request only**. Windows/HID product names remain available without it. Once cached, ID lookup works offline.

From the application directory:

    DrayBattery.exe --db-info
    DrayBattery.exe --identify
    DrayBattery.exe --update-device-db

- **--db-info:** show cached vendor and product counts.
- **--identify:** identify the local USB/HID devices by VID:PID. It does **not** read their batteries.
- **--update-device-db:** fetch the full catalogue over HTTPS, validate it and atomically replace the cached copy. Failed updates leave the previous file intact.

You can also select **Update USB device names (online)…** from the context menu. The database lives at %LOCALAPPDATA%\DrayBattery\usb.ids. **Normal app startup and battery polling never contact this service. No hardware identifiers, serial numbers or device lists are sent in the update request.**

The dataset's [license](https://github.com/vcrhonek/hwdata/blob/master/LICENSE) offers GPL-2.0-or-later or XFree86 1.0; its GPL option is compatible with DrayBattery's GPL-3.0-or-later.

**Why no automatic downloaded battery drivers?** Unreviewed HID commands can disrupt input devices. The online database only supplies names; new battery protocol code is distributed in tested app releases.

## Identity, duplicates and requests

### Diagnostics without peripheral commands

    DrayBattery.exe --diagnose
    DrayBattery.exe --status
    DrayBattery.exe --dump
    DrayBattery.exe --list --windows-only
    DrayBattery.exe --list --ghub-only

`--diagnose` reports Bluetooth connection evidence and cached Windows levels,
XInput/WGI status and HID descriptors. `--status` reads the running UI's visible
device counts by kind, widget health and polling age. These commands send no
HID/GIP commands and change no device settings.

Plain `--list` and `--list --wolverine-only` actively query batteries. They are
blocked while the tray app is running to avoid concurrent receiver requests.
The Wolverine probe sends only GET `0x07/0x80`; the unverified `0x07/0x84` flag is
no longer interpreted as charging state. For local UI verification, `--os-only`
uses Windows properties and localhost G HUB without peripheral commands.

This fix was hardware-tested with Wolverine `1532:0A4C` in Windows mode.
In Xbox mode (`1532:0A3F`), automatic use of the old model-only cache is disabled;
the foreground GIP reader menu item does not imply a working automatic indicator.
That mode still needs separate verification outside gameplay.

VID:PID identifies a device **model/type**, not a specific physical unit. DrayBattery only merges different transports with verified identity or receiver/slot evidence. Two real same-model units must remain separate.

For Wolverine V3 Pro, cross-mode mapping was verified with one physical controller; multiple simultaneous units remain unverified.

To request battery support, start with **--identify**, **--diagnose** and **--status**, specify the exact model, firmware and transport, and open an [issue](https://github.com/deepsky303/DrayBattery/issues). Review diagnostics before sharing: remove serials, private paths and unique hardware signatures. A model name alone is not sufficient evidence for implementing a battery protocol.
