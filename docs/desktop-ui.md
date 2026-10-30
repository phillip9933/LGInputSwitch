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

## Monitor profiles (development checkout)

This section describes the current development checkout. The published 1.2.0
release has the earlier single-monitor editor; the legacy Win32 frontend and
standalone Linux CLI retain their existing single-target behavior.

The **Monitors & profiles** card contains named tabs. **+ Profile** creates an
independent action list using an existing saved monitor, without copying its
shortcuts. Rename/remove buttons manage the selected tab. Switching tabs keeps
unsaved form edits; **Save settings** saves the entire workspace. Unsaved changes
block all switching, including native keys and tray actions, until saved.

1. Select the connection for the first monitor. Use **Edit → Edit monitor &
   inputs** to name it and review its input names/codes and source address.
2. Use **+ Monitor** to attach another saved monitor or a newly detected one.
   A saved monitor has one input mapping shared by every profile using it.
3. Name actions such as **Linux PC** and **Windows PC**. Choose one input for
   **every** assigned monitor in each row. New group members have no guessed
   input assignment; select the mappings before saving.
4. Enable the actions you want, reorder them with Move up/down, and optionally
   record direct, cycle and previous shortcuts. **+ Action** adds a custom action.
5. Save, then use a row's **Switch**, **Cycle inputs**, **Previous input**, a
   profile's tray submenu or its shortcut. The selected GUI tab does not change
   which profile a shortcut controls.

For example, a Linux PC action can send DisplayPort on the main monitor and
HDMI on the side monitor. Commands run on the serialized background worker, so
one action dispatches all its commands while the GUI stays responsive. Exact
simultaneity is not guaranteed. A missing/failed monitor does not stop subsequent
members. Per-monitor results show command acceptance or failure; **Retry failed
monitors** retries only the failed members. Refresh can update connection status
before retrying. A sent command does not confirm a physical input change.

Cycle follows enabled action order. Previous remembers complete accepted actions
in this session, never the display's initial physical input. Partial attempts do
not advance history: Previous can restore the last completely sent action.
A successful retry completes the action and updates history once. Sending to a
shared monitor from another profile invalidates the affected profile's old
history/retry so it cannot silently reuse stale group state. Changes to monitor
mappings, routes or action membership invalidate affected session state; names
and shortcut changes retain history. History is not persisted across restarts.

Connected monitors are matched by unique local EDID fingerprints where available
(Linux can also use the manufacturer/model/serial information from ddcutil).
Native display/bus numbers are re-enumerated before each member’s command,
including after an earlier group command changes the connected topology. Duplicate/missing
fingerprints and replacements require explicit connection selection. For a
monitor without a unique fingerprint, the confirmation lasts only for this app
session; a changed route/model/fingerprint requires selecting it again. Reconnect
never silently substitutes another display. Identity hashes and raw serial/EDID
are not exported in support reports.

The monitor library is available from **Advanced → Manage saved monitors**. Edit
there or beside an assigned monitor to update its shared mapping. Inputs used
by any action cannot be deleted until those actions are remapped. Removing a
monitor from one profile keeps the library definition; only unused definitions
can be deleted. At least one profile, monitor per profile and enabled action
must remain. Configuration limits are 16 saved monitors, 32 profiles and 64
actions/inputs per profile/monitor.

Appearance (Follow system/Light/Dark), notifications, startup and debounce remain
app-wide. Advanced can reveal input codes beside the input names. Closing hides
to the tray when available; without a tray it quits after the unsaved-edits prompt.

### Standard switching and other monitor brands

Each saved monitor has its own **Switching method**, selected when adding it or
in **Edit monitor & inputs**. **Standard DDC/CI (0x60)** supports Dell and other
monitors that implement standard Input Source writes. It uses the standard 0x51
source. **LG alternate (0xF4)** retains the existing configurable source address,
normally 0x50. Migrated setups keep the LG method; new monitor definitions default
to Standard. Groups can freely mix the two methods.

Standard presets include DisplayPort 1/2 (0x0f/0x10) and HDMI 1/2 (0x11/0x12).
**Load method presets** is an explicit replacement; matching input names keep
IDs, and unmatched profile mappings must be selected again before saving. Method
selection alone never silently translates custom codes. On Linux, **Read
advertised inputs** performs bounded read-only queries of the selected monitor
and appends advertised values for the chosen method without removing existing
inputs. On Windows, review the built-in diagnostics and enter the appropriate
values manually: native diagnostic identifiers are not assumed to match AMD
indices. USB-C/vendor-specific inputs require the monitor's own advertised or
known-working values; no universal USB-C code is guessed.

Standard switching uses the existing platform transports: ddcutil on Linux and
AMD ADL on Windows. It does not add Intel/NVIDIA Windows GPU backends. DDC/CI must
be enabled in the monitor, and some models/cables/docks/firmware do not implement
input switching or will not accept it while inactive. An advertised code is
useful evidence, not a compatibility guarantee. No automatic fallback sends both
standard and LG commands. Protocol background: [ddcutil Input Source example](https://www.ddcutil.com/getvcp_known_u3011_output/).

### Settings and migration

Existing desktop settings are read into one **Main monitor** profile, retaining
custom codes, input order, disabled rows, direct/cycle/previous shortcuts and app
preferences. No hardware commands are sent by migration. Confirm the connection
and save to store the new schema. Malformed/newer profile files block saving and
switching so a fallback cannot overwrite them; back up and repair/remove that
configuration before restarting. Migration leaves the original 1.2 settings intact.

On Windows, the modern app imports the existing `config.json` and Qt desktop
preferences once, then saves its complete workspace in **desktop-profiles.json**
beside the executable, using an atomic file replacement. Back up/copy both JSON
files when moving the app. Additional legacy targets remain intact in the original
config.json. The legacy frontend does not understand profiles; avoid running both
frontends together because startup/key ownership still overlaps.

On Linux, the workspace is the versioned `profilesV2` entry in the existing
per-user Qt settings file, normally
`~/.config/LGInputSwitch/LGInputSwitchDesktop.conf`. The standalone CLI does not
read these profiles. Plasma Autostart changes are respected at next launch.

### Shortcuts and tray

Windows uses RegisterHotKey; KDE Plasma uses the optional KGlobalAccel integration
on X11 and Wayland. Each enabled action, Cycle inputs and Previous input is named
under its profile in the tray and Plasma shortcut catalogue. Both editors use
the same saved assignments. Cross-profile duplicates are rejected, and keys
belonging to other applications or manual Plasma commands are never stolen.
Initial migration retains the existing Plasma action IDs; renaming/reordering an
action or profile keeps its stable ID and shortcut assignment. Removed/disabled
actions are pruned from this component after a successful update. Native keys
are suspended during shortcut recording and restored on leaving capture/hiding
the app. Existing shortcuts on other desktops remain available through their
single-monitor CLI bindings; native profile shortcuts require KDE on Linux.

External Plasma edits update the saved profile without writing unrelated dirty
form drafts. Saving an explicitly edited app draft applies that draft. Preview
mode never registers keys, discovers monitors, writes settings or sends commands.

## Built-in diagnostics

Open **Diagnostics → Create report** after a failed switch, review the report,
then use **Copy report** or **Save report** to attach it to a support request.
There is no need to save unfinished settings before generating a report: the
report identifies unsaved edits, and records the last command separately with
the targets and codes actually used at that time. Development reports include
all monitors assigned to the selected profile, with separate read results and
normalized input mappings. Profile/monitor/action labels, IDs and local identity
hashes are omitted. Windows probes still enumerate desktop displays independently
of AMD target indices; no unverified mapping between those APIs is asserted.

Diagnostics send no input-switching commands, change no settings and request no
elevation. They read standard brightness (`0x10`) and input (`0x60`) values, plus
the monitor's advertised capabilities.
On Linux, the existing ddcutil dependency reads each explicitly resolved profile
monitor's display/bus; reads for unresolved members are skipped. Each member's
read/capability query has the same timeout bounds, so large groups can take longer. Only recognized numeric VCP
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
all four tabs and tray menus. Do not interpret the synthetic monitor as a support
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

## Profile implementation and regression coverage

`desktop/profiles.*` owns versioned persistence, migration, validation, identity
resolution, per-monitor dispatch and per-profile session history. `platform.*`
retains individual native target transports. The shared Qt editor, tray and
native shortcut callbacks all dispatch saved profile/action IDs. Discovery and
commands run on one worker; the desktop ddcutil write path has a 10-second process
timeout so a hung child does not indefinitely block later members. AMD's optional
[EDID read API](https://gpuopen-librariesandsdks.github.io/adl/group__I2CDDCEDIDAPI.html)
provides local display fingerprints without introducing another runtime library.
Its absence does not prevent explicit session selection or switching.

The `monitor-profiles` regression group covers migration, file/JSON roundtrips,
corrupt/newer file preservation, shortcut conflicts, unique/ambiguous/replaced
monitor resolution, per-monitor input/source mapping, partial failures, failed-only
retry, cycle/previous state and shared-monitor invalidation. `desktop-smoke` drives
the editor with synthetic discovery and a fake ddcutil process, including drafts
across tabs, grouped dispatch from a different active tab, export privacy, per-member
results, external shortcut updates and disconnected members. Theme previews include
single-monitor and grouped configurations. Windows CI builds the same editor and
runs synthetic profile/preview tests; its fake AMD driver tests the optional EDID
entry point. This does not establish physical Windows multi-monitor compatibility.
