# Contributing to DrayBattery

Thanks for helping improve battery reporting on Windows.

## Getting started

1. Open an issue describing the target device, exact model, connection mode and expected behavior.
2. Build the project with Visual Studio 2022 C++ Build Tools using `build-msvc.cmd`.
3. Run `run-tests-msvc.cmd` before submitting a pull request.
4. Keep driver additions isolated in `src/drivers` and preserve the fallback order in `src/devices.cpp`.
5. Include test frames or a repeatable reproduction where available, but never submit private device IDs or credentials.

## Battery-data guidelines

- Report the real value supplied by a device or its software interface. Never fabricate an exact percentage from coarse XInput levels.
- Mark cached, sleeping or stale readings clearly.
- Only merge transport instances with a trustworthy physical identifier, never by matching model names alone.
- Limit polling frequency and avoid changes to firmware, profiles, lighting or controller input behavior.
- Explain device-specific commands and cite public protocol documentation or a reproducible hardware observation.

## Licensing

The project is a derivative of [Wireless Device Battery Monitor](https://github.com/krisgfx9/Wireless-Device-Battery-Monitor), licensed under GPL-3.0-or-later. Contributors agree to submit their work under compatible terms, retaining any applicable third-party notices.
