# Device support and identification

[**Русская версия**](device-support.ru.md)

DrayBattery reads battery status from supported device protocols and standard Windows interfaces. Having a VID:PID entry in a database **does not** mean the device publishes a readable battery percentage.

## Verified on physical hardware

| Device | Connection / source | Status | Notes |
| --- | --- | --- | --- |
| Logitech G903 LIGHTSPEED | G HUB local interface | Verified | Percentage obtained from the local running G HUB |
| Logitech G915 X LIGHTSPEED | G HUB local interface | Verified | Charging and sleep caching; hardware signature allows merging USB and LIGHTSPEED transport |
| Razer Wolverine V3 Pro | Windows Mode, USB VID:PID `1532:0A4C` | Verified raw report | HID report `0x04`, battery query `0x07/0x80`; reported 0–255 value converted to percentage |
| Razer Wolverine V3 Pro | Xbox Mode, USB VID:PID `1532:0A3F` | Verified GIP response | Foreground reader required; previous value may be cached and dimmed |

These are confirmations of **successful protocol reads**, not independent measurements of battery capacity.

## Additional drivers

Device-specific drivers inherited from [Wireless Device Battery Monitor](https://github.com/krisgfx9/Wireless-Device-Battery-Monitor) cover selected models from ATK, Aula, Cooler Master, Corsair, GameSir, Glorious, HyperX, Keychron, Kysona, Lamzu, Logitech, Razer, Rapoo, ROG, Sony INZONE, SteelSeries, Zowie and other manufacturers. The coverage depends on the **specific model and firmware**, not just its brand.

Standard HID battery reports, Bluetooth battery characteristics and Windows gaming input are used as fallback sources where available. Windows XInput often reports only an imprecise range such as Low, Medium or Full.

## Recognition and deduplication

USB vendor and product IDs identify a **type** of device, not necessarily a unique physical unit. A serial number or verified physical signature is required to safely merge a USB and wireless representation. DrayBattery leaves unresolved devices separate rather than incorrectly hiding a second device of the same model.

The Wolverine V3 Pro transport modes use a single-controller identity mapping in the currently supported configuration. Two simultaneously connected Wolverine V3 Pro units have **not** been validated for unique identification.

## Suggest a new device

1. Run `DrayBattery.exe --dump` and `DrayBattery.exe --list`.
2. Identify the exact model, firmware, operating mode, and transport (USB, 2.4 GHz, Bluetooth).
3. Search for an existing [issue](https://github.com/deepsky303/DrayBattery/issues), or open a new one with sanitized diagnostic output.

**Privacy:** `--dump` can contain device product names and interface information. Review and redact unique identifiers, serial numbers and private details before uploading any logs.

An optional offline identification catalogue may be added in the future, but no online database can replace device-specific battery protocol support.
