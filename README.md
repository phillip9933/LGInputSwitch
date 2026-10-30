# LG Input Switch

## Development and support

[Open issues](https://github.com/phillip9933/LGInputSwitch/issues) · [Intel/NVIDIA work](https://github.com/phillip9933/LGInputSwitch/issues/12) · [Contributing](CONTRIBUTING.md)

For troubleshooting, open the modern app's **Diagnostics → Create report**, review
it, then copy or save it with your issue. Compatibility information comes from
voluntary owner reports; advertised controls do not guarantee switching support.

Originally a small Windows tray utility that switches **LG monitor inputs** (DisplayPort / USB-C / HDMI1 / HDMI2) on **AMD GPUs** using **DDC/CI over I²C**, with configurable hotkeys, a settings dialog, first-run welcome, and a dynamic tray menu.

Note: I have only tested this on an AMD 7900XTX and 45GX950A-B monitor.

> **Windows requires AMD:** Windows uses AMD ADL. The Linux version uses ddcutil; GPU and monitor support must be verified for each setup.
> **⚠ I²C WARNING:** DDC/CI commands are sent directly to your monitor. Use at your own risk. Malformed commands can cause misbehavior on some displays.

## Monitor profiles

Version 1.3.0 adds named monitor tabs to the modern Windows/Linux app. Each
profile can switch one or several saved monitors with a single action, with a
separate input choice for each monitor. Cycle/previous shortcuts, per-monitor
results, failed-monitor retries and profile submenus in the tray are included.
A monitor's switching method, source address and input-code mapping are shared
across profiles. Standard DDC/CI Input Source (0x60) is available for Dell and other
compatible monitors, alongside the existing LG alternate method. Mixed groups are
supported; Windows switching still requires AMD. Each monitor can read and set up
its own advertised inputs automatically, with custom overrides and safe fallback
when it does not report a usable list.
See [profile setup and migration](docs/desktop-ui.md#monitor-profiles).

## Features
- Tray menu entries for the inputs you enable
- **Hotkeys**: cycle through inputs, or jump directly to DP/USB-C/HDMI1/HDMI2
- **Settings UI** with first-run welcome
- **Cycle order** respects enabled inputs and your chosen order
- **Notifications** (optional), **debounce**, and **I²C address** (LG often `0x50`)
- **Start with Windows**: Settings checkbox, applied to the current user's logon. You can also edit `startWithWindows` in config.json and restart the app. Windows Startup Apps settings can independently disable startup.

## Download / Run (Windows)

Download [version 1.3.0](https://github.com/phillip9933/LGInputSwitch/releases/tag/1.3.0).
Use the Windows x64 ZIP for the modern app or the legacy ZIP for the Win32 frontend.
See [installation, upgrades and runtime setup](docs/windows-release.md).

Legacy frontend instructions:
1. Download the latest release from the **Releases** page.
2. Run `LGInputSwitch.exe`.  
   - On first run it creates `config.json` next to the EXE and opens **Settings**.
   - Pick your **monitor** (Adapter/Display), enable inputs, set hotkeys, confirm I²C address (default `0x50`), and save.


## Build (Visual Studio 2022)
- Open the solution, set **x64 / Release**, and **Build**.
- This repo includes everything we used during development, including ADL glue.
- If you replace ADL headers with your own copy, ensure your include paths still point to them.

Hardware-free regression tests: `cmake -S . -B build-tests`, `cmake --build build-tests --config Release`, then `ctest --test-dir build-tests -C Release --output-on-failure`. The Windows startup test writes and removes this app's per-user Run entry; run it in an isolated test account/CI runner.

CMake also builds the native Windows tray app with Visual Studio 2022 (CMake
3.21+): `cmake -S . -B build -A x64`, then
`cmake --build build --config Release`. The existing solution remains supported;
CI builds it and the CMake application target. Both frontends share a portable
C++17 library for command validation, input ordering, cycle state and results.

See [code review and verification](docs/maintenance-review.md) and the [cross-platform architecture proposal](docs/cross-platform-design.md).

## Linux CLI

An initial CLI uses the installed `ddcutil` tool and the shared command-value
validation. A shared Qt tray/settings GUI and linked KDE Plasma shortcuts are now available
as a Linux version; see [Linux installation](docs/linux-install.md). Windows JSON is not migrated or read by this CLI.

Build with a C++17 compiler and CMake:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/lginputswitch detect
./build/lginputswitch switch --display 1 --input 0x90 --dry-run
```

Install `ddcutil` using your distribution's package manager and configure its
normal I2C device access. Review the preview, then omit `--dry-run` to send the
command to the selected monitor. Use a known input code for your monitor; the
example selects HDMI1 on some LG models. `--bus N` can select a specific I2C bus
instead of a display number. See [Linux setup and verification](docs/linux-cli.md).

The long-term target is one app for AMD, Intel and NVIDIA systems on Windows and
Linux, followed by macOS. Current Windows releases still require AMD; adding a
backend or CLI does not establish compatibility for untested hardware.

### Project layout (simplified)

/src
app_tray.cpp # tray + hotkeys + menu + first-run
app_config.* # config load/save, defaults (JSON via nlohmann::json)
app_toggle.* # DDC/CI send helpers (input codes)
settings_ui.* # settings dialog
welcome_ui.* # welcome dialog for first run
platform/windows # documented ADL API session + DDC packet transport
/resource
app.rc, resource.h, app.ico
LICENSE
THIRD_PARTY_LICENSES.md
COPYRIGHT.md


## Usage Tips
- **I²C subaddress**: many LG models need `0x50` for input switching via this path; set it in Settings.
- **Adapter/Display indices**: these can change (driver updates / device changes). Re-run Settings → Monitor if switching stops working.
- **Debounce**: if you get double-switches or flaky behavior, increase debounce in Settings.

---

## Credits & Licenses
- **amildahl/amdddc-windows** — Original CLI and approach for AMD+LG I²C input switching.  The project began as a tray wrapper around that approach. Version 1.1.0 replaces
  the inherited implementation with new API/session/packet code and retains this credit.
  https://github.com/amildahl/amdddc-windows
- **nlohmann/json** — MIT  
  https://github.com/nlohmann/json
- **AMD ADL SDK headers** — MIT notices are embedded in the bundled headers; AMD retains its copyright. Driver binaries are not bundled.
- DDC/CI references — ddcutil docs/wiki and other notes (documentation only).
- **Jason7536/lg-input-switch** — Separate Intel IGCL-based Windows port: https://github.com/Jason7536/lg-input-switch . Intel GPU users can evaluate that project; its compatibility is maintained independently.

Project code is **AGPL-3.0-or-later**; see [LICENSE.md](LICENSE.md)
and [copyright and source distribution](COPYRIGHT.md). Earlier MIT releases retain
their original terms. Dependencies keep their own licenses; see
[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

---

## Disclaimer
This project is not affiliated with AMD or LG. Use at your own risk.

## Modern desktop (Windows and Linux)

An optional shared tray/settings frontend adds a refreshed layout, scalable monitor
icon, shortcut recording on Windows, asynchronous monitor operations and system,
light and dark appearance modes. Build with Qt 6.5+ using
`cmake -S . -B build-desktop -DLG_BUILD_DESKTOP=ON`, then
`cmake --build build-desktop --config Release`. See [desktop setup, preview and
limitations](docs/desktop-ui.md). The Linux GUI uses ddcutil. KDE Plasma builds with KGlobalAccel can record and
register shortcuts directly in the app; other desktops retain the CLI fallback. Known platform and hardware limitations are documented in the release notes.


CachyOS/KDE Wayland testing has verified monitor switching, the tray/settings UI
and linked Plasma shortcuts. Linux offers opt-in startup and cycle/previous/direct
input shortcuts. [Install the native Arch/CachyOS package](docs/linux-install.md)
or build from source. Windows packages include both the modern and legacy frontends. Physical Windows
hardware validation remains limited; see the release notes.

See [release notes](docs/releases/1.3.0.md) and [publication workflow](docs/release-candidate.md).
