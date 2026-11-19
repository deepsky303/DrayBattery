# Changelog

## 0.1.11

- Restored short dimmed last-known battery indicators for sleeping Logitech G903/G915 devices: 90-second maximum from the most recent verified live measurement. Repeated cached/sleeping messages do not renew the timer. Receiver removal hides the indicator immediately.
- Fixed physical Logitech receiver matching across G HUB USB path numeric suffixes and different USB MI interfaces. Verified both G903 and G915 X through local G HUB repeatedly, without falling back to unreliable PID-only matching.
- Improved handling of identical Logitech receiver product IDs and HID++/G HUB source transitions; eliminated obsolete three-day registry cache resurrection and fictitious 100% readings.
- Reduced unnecessary polling and HID contention through separate local G HUB refresh; hardened Explorer/taskbar widget recreation, tray icon registration, startup/exit logging, and worker shutdown.
- Added lifecycle, Logitech driver, G HUB PnP matching, scheduling, and identity regression tests (nine MSVC test programs). GUI widget recreation and ordinary startup were smoke-tested on the developer's Windows 11 system.
- Physical mouse sleep/wake and receiver removal still require observation in normal use; synthetic tests cover these transitions.

## 0.1.10

- Fixed a Logitech G HUB ghost-device condition: NOT_CONNECTED now means disconnected, not asleep. G HUB explicitly disconnected devices invalidate their own stale cached indicators immediately.
- Physical identities and receiver/slot aliases are used to retire only the matching offline entry, while two actual same-model devices remain distinct.
- Added unit regression tests for ACTIVE, CONNECTED, SLEEPING, NOT_CONNECTED, and safe cross-poll invalidation.
- Does not remove valid asleep-device cache or discard old receiver history without evidence.

## 0.1.9

- Added an explicitly opt-in USB manufacturer and device-name catalogue updater using the public hwdata usb.ids source over HTTPS. Local cache persists offline; invalid updates preserve existing data.
- Added read-only commands --identify, --db-info and --update-device-db plus a context-menu updater. No hardware inventory or serial is sent to the database server.
- Imported reviewed, model-specific battery protocols for CHERRY MX 3.0S Wireless and Valve Steam Controller from upstream Wireless-Device-Battery-Monitor v1.0.8 (not hardware-tested in this derivative).
- Expanded the English and Russian compatibility guides to distinguish physically verified devices, implemented protocols and mere USB identification.
- Added usb.ids parser tests and retained the read-only existing battery polling pipeline.

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
