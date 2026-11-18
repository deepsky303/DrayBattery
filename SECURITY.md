# Security and privacy

DrayBattery is a Windows utility that reads local peripheral battery status. It is not intended to send battery readings to remote servers.

## Reporting a security concern

Avoid publicly posting credentials, hardware serial numbers, detailed private paths or other sensitive information. If GitHub private vulnerability reporting is available for this repository, prefer it for security issues. Otherwise, open a minimal issue with no sensitive details and request a private coordination channel.

## Device communication

Some supported battery protocols transmit query commands to HID peripherals. Such commands are intended for battery-status requests, but hardware and firmware variants may behave differently. No guarantees of safety on untested devices are implied. DrayBattery does not intentionally change device firmware, profiles, lighting or key mappings.
