# LG Input Switch

Switch monitor inputs from your system tray or keyboard on Windows and Linux.
Use LG’s alternate switching method or standard DDC/CI commands for compatible
monitors, including Dell.

## Features

- Named profiles for one monitor or a group, with an input choice for each monitor.
- Cycle, previous-input and direct-action shortcuts, plus linked KDE Plasma shortcuts.
- Automatic input detection, custom mappings and per-monitor results with retry.
- System/light/dark themes, optional startup and built-in diagnostics.

## Download and setup

Get the [latest release](https://github.com/phillip9933/LGInputSwitch/releases/latest).

- **Windows x64:** extract the modern Windows ZIP and run `LGInputSwitchDesktop.exe`.
  **An AMD GPU is required.** See [Windows setup](docs/windows-release.md).
- **CachyOS / Arch Linux:** install the `.pkg.tar.zst` package and launch
  **LG Input Switch**. Uses `ddcutil`; native shortcut integration requires KDE
  Plasma. See [Linux setup](docs/linux-install.md).

Add your monitors, review the detected inputs, and save. For grouped actions,
choose an input for every monitor. See [profiles and shortcuts](docs/desktop-ui.md#monitor-profiles).
Back up your settings and quit the app before upgrading.

Monitor support varies, and advertised inputs can be incomplete. Linux testing
has focused on CachyOS/KDE; physical Windows testing is limited. Intel/NVIDIA
Windows backends and macOS remain future work. Use known input codes for your
monitor; commands are sent directly to the display.

## Help and development

For troubleshooting, open **Diagnostics → Create report**, review it, then copy
or save it with your [issue](https://github.com/phillip9933/LGInputSwitch/issues).
Reports stay local until you share them.

[Build the desktop app](docs/desktop-ui.md) · [Linux CLI](docs/linux-cli.md) ·
[Contributing](CONTRIBUTING.md) · [Release notes](docs/releases/1.3.0.md)

## License and credits

[AGPL-3.0-or-later](LICENSE.md). Dependencies retain their own terms; see
[third-party notices](THIRD_PARTY_LICENSES.md) and [copyright details](COPYRIGHT.md).
Originally inspired by [amildahl/amdddc-windows](https://github.com/amildahl/amdddc-windows).
