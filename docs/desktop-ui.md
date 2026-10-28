# Modern desktop frontend (Windows and Linux)

The existing Windows UI uses fixed dialog units, crowded group boxes and text-only
shortcut fields. It has no appearance policy, opens multiple modeless settings
windows, performs monitor writes on the UI thread, and only handles right-clicks
on its tray icon. Repainting those individual Win32 controls would duplicate the
Linux UI and complicate dark-theme behavior.

This increment adds an optional shared Qt Widgets frontend rather than replacing
the maintenance executable before hardware testing. Qt 6.5+ provides system color
scheme change notifications and cross-platform tray support. A restrained blue
accent, clearer hierarchy, rounded cards and comfortable input rows work in both
light and dark modes. The monitor glyph is drawn at multiple resolutions, so tray
and window icons scale without raster stretching.

## Behavior

- Inputs: enable, name and reorder inputs; direct and cycle actions. Editable
  input codes are revealed from Advanced rather than dominating setup.
- Preferences: Follow system (default), Light or Dark, plus notifications.
  Windows adds shortcut recording, cycle shortcut and startup settings. Appearance
  changes preview immediately and persist when settings are saved.
- Advanced: source address, debounce and editable input mapping.
- Diagnostics: create, review, copy or save a local JSON support report. Reports
  include OS/Qt/app versions and build revision, selected target, normalized input codes, unsaved-edit
  status and the last command's target/result, advertised VCP controls/MCCS version,
  and any advertised standard/LG input values. Custom labels, shortcuts, raw EDID
  and capability strings, serial numbers, user names and personal file paths are
  omitted. Nothing is uploaded.
- Tray: open the app by left/double click, right-click for enabled inputs, cycle,
  open and quit. Closing hides only when a tray is available; without one the GUI
  remains usable and closing quits. A second instance shows where to find the app.
- Discovery and writes run on one serialized worker, keeping the GUI responsive.
  Writes require an explicitly selected/discovered, saved target. Editing settings
  disables tray switching until saved. Reconnect/refresh never silently selects a
  different monitor. Failures stay visible; success says command sent, not verified.
- Windows hotkeys are released while recording a shortcut so recording cannot
  accidentally switch the monitor. Registration conflicts are shown in the GUI.

On Windows, the new executable reads and saves the existing config.json beside
it. Preserve that file when moving a release executable. Existing enabled
custom codes/order and additional configured targets are retained; disabled rows
are retained in desktop preferences so they can be re-enabled. Run one Windows
frontend at a time to avoid duplicate startup/hotkey ownership.

Linux desktop preferences use Qt's per-user config location, separately from the
Windows JSON and CLI. There is no automatic config migration. On KDE Plasma, optional KGlobalAccel support lets you enable global shortcuts in
Preferences, record combinations on Inputs and save. This works while the app is
running, including with its window hidden. Shortcut registration is off until
you enable it. Existing shortcuts belonging to other apps or manual Plasma
commands are reported as conflicts and are never stolen. Active shortcuts are suspended while recording and deactivated on exit; their
Plasma assignments remain registered. LG Input Switch appears under Applications
in Plasma Shortcuts when its desktop launcher is installed. Both editors use the
same assignments: changes in Plasma update the app and its saved preferences,
and app changes update Plasma. Unsaved app edits remain drafts until saved.

Other desktops (or builds without KGlobalAccel) retain the desktop-configured CLI
fallback. Linux offers opt-in per-user desktop autostart; see [installation and upgrades](linux-install.md). GNOME requires tray support via an appropriate
extension; desktops without a tray can use the window. Tray-host and Wayland
integration require desktop-specific manual testing.

## Built-in diagnostics

Open **Diagnostics → Create report** after a failed switch, review the report,
then use **Copy report** or **Save report** to attach it to a support request.
There is no need to save unfinished settings before generating a report: the
report identifies unsaved edits, and records the last command separately with
the target and codes actually used at that time.

Diagnostics send no input-switching commands, change no settings and request no
elevation. They read standard brightness (`0x10`) and input (`0x60`) values, plus
the monitor's advertised capabilities.
On Linux, the existing ddcutil dependency reads the currently selected display/bus;
without a selected monitor, those reads are skipped. Only recognized numeric VCP
results and parsed capability fields are exported, never raw ddcutil output. VCP
reads time out after five seconds and the capability request after ten seconds.
On Windows, system-provided Dxva2 APIs query up to eight desktop displays and
eight physical monitors. These Windows display identifiers are distinct
from the app's AMD adapter/display indices; the report does not assert a mapping.
GPU model and installed driver version are collected using Windows display and
registry APIs. The Windows check runs in an isolated child process with a
20-second timeout, before GUI/ADL initialization. An unsuccessful/timed-out read
does not establish that a monitor is unsupported.

Capabilities include the advertised VCP code list, MCCS version when provided,
and advertised value lists for standard Input Source (`0x60`) and manufacturer
control `0xF4`. Common standard input values have names such as HDMI 1 or
DisplayPort 1; vendor values remain numeric rather than guessing their meaning.
The report distinguishes a missing control, an advertised control without a
value list, an explicitly empty list, a failed request and a malformed response.
No advertised value is automatically applied to the user's configuration.

The bounded shared parser accepts only top-level capability fields and numeric
control/value tokens. Free-text model/serial/vendor fields are discarded; nested
text cannot inject a control list. Responses over 64 KiB, excessive nesting,
ambiguous duplicate VCP sections and malformed/oversized input-value lists are
reported as unreadable rather than exporting partial guessed data. Windows
child output is capped at 256 KiB; other command output is capped at 64 KiB.

Advertised capabilities are not authoritative: a missing control/value is not
proof that the monitor cannot use it, and an advertised one is not proof it works.
The report replaces the external capability/control screenshot normally requested
for initial troubleshooting; it is not a full monitor-control utility.

Standard DDC/CI reads do not verify LG's alternate `0xF4`/source-`0x50` path, and a
successful send does not confirm a physical input change. No vendor-code scans,
factory-menu operations, automatic fallback writes or guessed input values are
performed. Preview mode uses synthetic data and does not run diagnostic probes.

The implementation shares a bounded Qt process runner with Linux discovery,
uses Qt's existing JSON/export facilities and adds no third-party runtime library.
Dxva2 is provided by Windows. Qt, optional KDE shortcuts and Linux ddcutil remain
the existing dependencies; removing those would require independently replacing
their GUI, desktop integration and hardware handling. The Qt-free CLI and legacy
Windows frontend are unchanged by this increment.

Windows API references (new implementation; no upstream implementation copied):
[GetVCPFeatureAndVCPFeatureReply](https://learn.microsoft.com/en-us/windows/win32/api/lowlevelmonitorconfigurationapi/nf-lowlevelmonitorconfigurationapi-getvcpfeatureandvcpfeaturereply)
and [physical monitor enumeration](https://learn.microsoft.com/en-us/windows/win32/api/physicalmonitorenumerationapi/).
Capability retrieval uses
[CapabilitiesRequestAndCapabilitiesReply](https://learn.microsoft.com/en-us/windows/win32/api/lowlevelmonitorconfigurationapi/nf-lowlevelmonitorconfigurationapi-capabilitiesrequestandcapabilitiesreply);
Linux uses [ddcutil capabilities --terse](https://www.ddcutil.com/command_capabilities/).

## Build

For native KDE shortcuts, also install the KDE Frameworks 6 GlobalAccel development
package (`kglobalaccel` on CachyOS/Arch). CMake enables the integration when it is
available; generic Qt builds keep working without it.

Install Qt 6.5+ development packages (Widgets and Concurrent), a C++17 compiler
and CMake. Windows Visual Studio 2022 requires CMake 3.21+.

```
cmake -S . -B build-desktop -DLG_BUILD_DESKTOP=ON
cmake --build build-desktop --config Release
ctest --test-dir build-desktop -C Release --output-on-failure
```

The executable is `LGInputSwitchDesktop` (Windows: `Release/LGInputSwitchDesktop.exe`).
Windows releases include deployed Qt, the official runtime installer, exact source and notices; CI tests the extracted ZIP. Linux
needs the matching Qt runtime, desktop platform plugin and ddcutil. CMake installs
the Linux executable, launcher and icon. The original Windows solution/executable
and Linux CLI still build with no Qt dependency when the option is off.

For a Linux qmake-only development environment:

```
mkdir -p build-desktop
cd build-desktop
qmake6 ../desktop/desktop.pro
make
```

## Design preview and verification

```
LGInputSwitchDesktop --preview --screenshot-dir previews
```

Preview mode never loads/saves user config, registers shortcuts, discovers hardware
or sends monitor commands. It renders synthetic monitor data in both palettes,
all three tabs and tray menus. Do not interpret the synthetic monitor as a support
claim. Qt offscreen mode (`QT_QPA_PLATFORM=offscreen`) can render it in CI.

Local Linux checks cover both palettes, visible rows, preview isolation, fake
monitor discovery, explicit target selection, settings roundtrip and asynchronous
writes to a fake ddcutil process. Existing core/backend tests remain in CI. Windows
CI builds both executables and runs preview smoke checks; physical tray, native
shortcut capture and OS theme transitions still need manual verification.

Additional real-hardware acceptance should cover system
light/dark changes while the app is open, 100/150/200% scaling, keyboard navigation,
contrast/accessibility, Explorer/tray restart, unavailable tray hosts, suspend and
monitor reconnect, actual AMD hardware switching and legacy config upgrades.
Unknown desktop color-scheme hints fall back to light; users can override them.
Qt licensing/deployment notices must accompany a stable runtime distribution.

Implementation, design and tests used substantial Codex AI assistance.
References: [Qt color scheme](https://doc.qt.io/qt-6/qstylehints.html#colorScheme-prop),
[Qt tray support](https://doc.qt.io/qt-6/qsystemtrayicon.html).

Native shortcut registration, readback, cleanup and non-stealing conflict handling
were tested against the actual CachyOS/Plasma Wayland session using temporary
actions with no monitor callbacks. The maintainer subsequently confirmed physical shortcut activation and the linked Plasma application catalogue work on CachyOS/KDE Wayland.
The probe source is tests/kde_shortcut_probe.cpp.

## Linked shortcut action catalogue

The native component `lginputswitch` resolves to the installed `lginputswitch.desktop` launcher, with a catalogue of:
Cycle inputs, Previous input, and Switch to each enabled input. Actions with no
key assigned remain visible for configuration. Disabling/renaming inputs removes
obsolete actions when the active catalogue is refreshed. The earlier manually
created command is separate and is not deleted automatically.

Previous input tracks accepted commands in the current session. After selecting
DP and then USB-C through the app, Previous selects DP; pressing it again selects
USB-C. Re-selecting the current input and failed writes do not replace history.
History resets when the target/source changes; the app never guesses the initial
physical input or sends readback probes. It reports when no previous input is
known. The same previous-input action is available to Windows desktop shortcuts.

The native probe uses a separate configuration process to verify Plasma-to-app
change signals, persistence/reload, app-to-Plasma writes and conflict handling.
All probe callbacks are hardware-free. GUI smoke tests verify previous-input
routing against a fake backend. No physical switch is sent by automated tests.

Native shortcut components deliberately omit the `.desktop` suffix. On Plasma,
using the launcher-service ID makes component cleanup remove dynamic actions.
A native-component/launcher-service comparison reproduced this failure; the
window-level probe also covers registration after construction, discovery and
focus events with isolated preferences and a fake monitor backend.
