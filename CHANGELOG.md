# Changelog

## 0.1.8

- Fixed temporary duplicate icons after Logitech changes battery-reporting source between direct HID++ and G HUB. The superseded last-known entry disappears as soon as a verified live reading for the same receiver/slot is available, instead of lingering for up to 30 minutes.
- Kept distinct same-model devices separate when their physical identities or receiver aliases differ.
- Added regression tests for source transitions, unrelated receivers, disconnected sources and sleeping entries.
- Hardened the Windows test runner so abnormal negative process exit codes cannot incorrectly report all tests passing.

## 0.1.7

- Three persistent icon appearance modes: adaptive colors, classic white and adaptive monochrome.
- Removed the leading approximation glyph from Wolverine V3 Pro battery figures; explanation remains in tooltips.
- Replaced corrupted labels in the Xbox GIP reader with valid UTF-8 source strings.
- Fixed the command line used to launch the foreground Xbox GIP reader.
- Added a per-user Setup.exe installer and refreshed documentation, build guidance and release packaging.

## 0.1.6

- Added an Xbox Mode foreground GIP battery reader for Razer Wolverine V3 Pro and last-reading cache.
- Introduced physical-device identity reconciliation for supported transports and a conservative legacy Logitech cache migration.
- Added validation tests for device identity behavior.

## 0.1.5

- Added a direct HID battery reader for Razer Wolverine V3 Pro in Windows Mode.

## 0.1.4

- Added Logitech G HUB localhost battery readings and last-known-value cache.

## 0.1.3

- Added continued bounded polling retries when Logitech HID++ receivers miss battery reports.

## 0.1.2

- Added startup battery-query retries for temporarily sleeping Logitech peripherals.

## 0.1.1

- Added taskbar-aware adaptive color palettes and improved tooltip placement.

## 0.1.0

- Initial DrayBattery derivative of Wireless Device Battery Monitor v1.0.4.
