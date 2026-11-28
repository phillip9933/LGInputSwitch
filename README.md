# LG Input Switch

Switch monitor inputs from your system tray or keyboard on Windows and Linux.
Use LG’s alternate switching method or standard DDC/CI commands for compatible
monitors, including Dell.

<a href="https://github.com/phillip9933/LGInputSwitch/releases/download/1.3.4/LGInputSwitch-1.3.4-Windows-x64.exe"><img src="docs/badges/windows-x64.svg" alt="Download LG Input Switch 1.3.4 for Windows x64" height="40"></a>
&nbsp;&nbsp;&nbsp;
<a href="https://github.com/phillip9933/LGInputSwitch/releases/download/1.3.4/lginputswitch-1.3.4-1-x86_64.pkg.tar.zst"><img src="docs/badges/arch-linux-x64.svg" alt="Download LG Input Switch 1.3.4 for Linux x86_64 (CachyOS / Arch package)" height="40"></a>

**Support:** [Buy me a coffee](https://buymeacoffee.com/phillip9933) · [GitHub Sponsors](https://github.com/sponsors/phillip9933)

## Features

- Named profiles for one monitor or a group, with an input choice for each monitor.
- Cycle, previous-input and direct-action shortcuts, plus linked KDE Plasma shortcuts.
- Automatic input detection, custom mappings and per-monitor results with retry.
- System/light/dark themes, optional startup and built-in diagnostics.

## 2.0 beta

The [2.0.0 Beta 1 release](https://github.com/phillip9933/LGInputSwitch/releases/tag/2.0.0-beta.1) adds NVIDIA LG alternate switching on supported Windows outputs, Intel standard DDC/CI routing, per-monitor brightness, and a redesigned workspace and tray. Hardware reports and usability feedback are welcome. The buttons above download stable 1.3.4.

## Download and setup

Get the [latest release](https://github.com/phillip9933/LGInputSwitch/releases/latest).

- **Windows x64:** download the portable `.exe` and run it; no installation is needed.
  LG alternate switching uses AMD graphics; standard DDC/CI is available for
  compatible Windows connections. See [Windows setup](docs/windows-release.md).
- **CachyOS / Arch Linux:** install the `.pkg.tar.zst` package and launch
  **LG Input Switch**. Uses `ddcutil`; native shortcut integration requires KDE
  Plasma. See [Linux setup](docs/linux-install.md).

Add your monitors, review the detected inputs, and save. For grouped actions,
choose an input for every monitor. See [profiles and shortcuts](docs/desktop-ui.md#monitor-profiles).
Back up your settings and quit the app before upgrading.

Monitor support varies, and advertised inputs can be incomplete. Linux testing
has focused on CachyOS/KDE; physical Windows testing is limited. The beta adds the NVIDIA and Intel paths described above. Intel LG alternate switching and macOS remain future work. Use known input codes for your
monitor; commands are sent directly to the display.

## Help and development

For troubleshooting in the beta, open **Settings → Diagnostics → Create report**, review it, then copy
or save it with your [issue](https://github.com/phillip9933/LGInputSwitch/issues).
Reports stay local until you share them. Check for updates in **Settings → Application updates** in the beta or **Preferences → Application updates** in 1.3.4.

[Build the desktop app](docs/desktop-ui.md) · [Linux CLI](docs/linux-cli.md) ·
[Contributing](CONTRIBUTING.md) · [Release notes](docs/releases/1.3.4.md)

## License and credits

[AGPL-3.0-or-later](LICENSE.md). Dependencies retain their own terms; see
[third-party notices](THIRD_PARTY_LICENSES.md) and [copyright details](COPYRIGHT.md).
Originally inspired by [amildahl/amdddc-windows](https://github.com/amildahl/amdddc-windows).

## AI usage

I use AI tools for analysis, implementation, tests, and documentation. I decide what ships and take responsibility for maintaining it. Project-specific testing and known limitations are documented separately.
