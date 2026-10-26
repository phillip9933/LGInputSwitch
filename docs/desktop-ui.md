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
it. Preserve that file when moving a candidate executable. Existing enabled
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
fallback. Linux autostart is not installed by this increment. GNOME requires tray support via an appropriate
extension; desktops without a tray can use the window. Tray-host and Wayland
integration require desktop-specific manual testing.

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
Windows candidates need their deployed Qt runtime; CI runs windeployqt. Linux
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

Before replacing the legacy Windows UI or publishing stable builds, test system
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
actions with no monitor callbacks. Physical key activation remains a user check.
The probe source is tests/kde_shortcut_probe.cpp.

## Linked shortcut action catalogue

The application component matches `lginputswitch.desktop`, with a catalogue of:
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
