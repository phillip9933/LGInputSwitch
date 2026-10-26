# LG Input Switch (Windows Tray)

## Roadmap and work

[Windows release scope](https://github.com/phillip9933/LGInputSwitch/issues/8) · [Open work](https://github.com/phillip9933/LGInputSwitch/issues) · [Issue conventions](CONTRIBUTING.md#public-roadmap-and-issue-management)

The previous board links pointed to another project (Kura). LGInputSwitch currently has no linked public project; use the issue list until a dedicated board is configured.

Follow release scope and maturity separately from individual work. Dates are estimates; unscheduled ideas have no release milestone.

A small Windows tray utility that switches **LG monitor inputs** (DisplayPort / USB-C / HDMI1 / HDMI2) on **AMD GPUs** using **DDC/CI over I²C**, with configurable hotkeys, a settings dialog, first-run welcome, and a dynamic tray menu.

Note: I have only tested this on an AMD 7900XTX and 45GX950A-B monitor.

> **⚠ AMD-ONLY:** This app uses AMD’s ADL to talk I²C/DDC. It will not work on non-AMD GPUs.  
> **⚠ I²C WARNING:** DDC/CI commands are sent directly to your monitor. Use at your own risk. Malformed commands can cause misbehavior on some displays.

## Features
- Tray menu entries for the inputs you enable
- **Hotkeys**: cycle through inputs, or jump directly to DP/USB-C/HDMI1/HDMI2
- **Settings UI** with first-run welcome
- **Cycle order** respects enabled inputs and your chosen order
- **Notifications** (optional), **debounce**, and **I²C address** (LG often `0x50`)
- **Start with Windows**: Settings checkbox, applied to the current user's logon. You can also edit `startWithWindows` in config.json and restart the app. Windows Startup Apps settings can independently disable startup.

## Download / Run
1. Download the latest release from the **Releases** page.
2. Run `LGInputSwitch.exe`.  
   - On first run it creates `config.json` next to the EXE and opens **Settings**.
   - Pick your **monitor** (Adapter/Display), enable inputs, set hotkeys, confirm I²C address (default `0x50`), and save.


## Build (Visual Studio 2022)
- Open the solution, set **x64 / Release**, and **Build**.
- This repo includes everything we used during development, including ADL glue.
- If you replace ADL headers with your own copy, ensure your include paths still point to them.

Hardware-free regression tests: `cmake -S . -B build-tests`, `cmake --build build-tests --config Release`, then `ctest --test-dir build-tests -C Release --output-on-failure`. The Windows startup test writes and removes this app's per-user Run entry; run it in an isolated test account/CI runner.

See [code review and verification](docs/maintenance-review.md) and the [cross-platform architecture proposal](docs/cross-platform-design.md).

## Linux CLI (experimental)

An initial CLI uses the installed `ddcutil` tool and the shared command-value
validation. This is a development preview; Linux tray, hotkeys and settings UI
will follow hardware validation. Windows JSON is not migrated or read by this CLI.

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
amdddc_* / adl.* # ADL bridge + raw DDC/CI I²C calls
/resource
app.rc, resource.h, app.ico
LICENSE
THIRD_PARTY_LICENSES.md
ATTRIBUTIONS.md


## Usage Tips
- **I²C subaddress**: many LG models need `0x50` for input switching via this path; set it in Settings.
- **Adapter/Display indices**: these can change (driver updates / device changes). Re-run Settings → Monitor if switching stops working.
- **Debounce**: if you get double-switches or flaky behavior, increase debounce in Settings.

---

## Credits & Licenses
- **amildahl/amdddc-windows** — Original CLI and approach for AMD+LG I²C input switching.  This project is essentially a
  tray-based wrapper with UI/hotkeys around that approach so HUGE thanks to them for their work.
  https://github.com/amildahl/amdddc-windows
- **nlohmann/json** — MIT  
  https://github.com/nlohmann/json
- **AMD ADL SDK** — Provided under AMD’s SDK EULA (proprietary). AMD retains ownership of the SDK.
- DDC/CI references — ddcutil docs/wiki and other notes (documentation only).
- **Jason7536/lg-input-switch** — Separate Intel IGCL-based Windows port: https://github.com/Jason7536/lg-input-switch . Intel GPU users can evaluate that project; its compatibility is maintained independently.

See [THIRD_PARTY_LICENSES.md](./THIRD_PARTY_LICENSES.md) and [ATTRIBUTIONS.md](./ATTRIBUTIONS.md) for details.

---

## Disclaimer
This project is not affiliated with AMD or LG. Use at your own risk.
