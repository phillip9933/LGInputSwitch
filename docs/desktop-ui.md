# Modern desktop frontend (Windows and Linux)

Windows and Linux share one Qt Widgets frontend, with system/light/dark themes,
a tray icon, profiles and global shortcuts. The monitor glyph is drawn at multiple
resolutions so tray and window icons scale cleanly.

The original Win32 frontend and its Visual Studio project have been retired.
Older settings still import through the desktop app's read-only compatibility
module; no separate legacy executable is built or published.

## Monitor profiles

The Windows/Linux desktop app supports monitor profiles. The standalone Linux
CLI retains its single-target behavior.

The top navigation contains **Profiles**, **Preferences**, **Advanced** and
**Diagnostics**. App-wide settings are separate pages; monitor controls appear
only on Profiles. Pages scroll at smaller sizes while status and Save settings
stay outside the scrolling content. The default window is 1000×740 and supports
620×460; large action tables also have their own scrolling.

On **Profiles**, named profile tabs sit above a monitor-status summary and a
read-only switching table. **Manage monitors** reveals connection details, the
+ Monitor button and Refresh; the section opens automatically when a monitor
newly needs a connection. Disabled actions are hidden in this normal view.
Use **Edit actions** to show enabled checkboxes, input selectors, shortcut
recorders, and action-management controls. **Done editing** returns to switching
view and keeps the draft; **Save settings** commits the entire workspace. Tab/page
changes retain drafts. Unsaved changes block switching, including native keys
and tray actions, until saved.

1. Use **Connect…** for a monitor needing a connection. Connected rows show status
   and their detected route as text. A row’s **… → Reconnect…** opens an explicit
   binding dialog; connections assigned to another saved monitor are excluded.
   The dialog can refresh discovery. This updates the saved monitor in all its
   profiles; to use another saved monitor in this profile, use **+ Monitor**.
2. Expand **Manage monitors**, then use **… → Edit monitor & inputs…** to name a monitor and review its method,
   input names/codes and source address. These mappings are shared by every
   profile using that saved monitor.
3. Use **+ Profile** for an independent action list without copied shortcuts.
   The adjacent profile-options **…** menu contains Rename/remove. **+ Monitor**
   attaches another saved or newly detected monitor and opens action editing.
4. In **Edit actions**, name actions such as **Linux PC** and **Windows PC**.
   Choose an input for every assigned monitor in every action. Port-named actions
   can match the same detected input name automatically; named PC scenes still
   need their per-monitor mapping because the app cannot identify connected PCs. **+ Action** adds rows; select a row and
   use **Action options** to move or remove it. Cycle/previous shortcut fields
   are also shown here. Native shortcut changes from Plasma update the read-only
   shortcut display without discarding drafts.
5. Save, then use a row’s **Switch**, **Cycle inputs**, **Previous input**, a
   profile’s tray submenu or its shortcut. A failed group reveals **Retry failed
   monitors** and per-monitor results. The selected GUI tab never changes which
   profile a shortcut controls.

**Diagnostics** has its own **Report for** profile selector, linked to the profile
tabs. It makes the report target explicit while keeping the diagnostic tools on
an app-level page.

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
can be deleted. At least one profile, monitor per profile and action must remain.
Profiles with no enabled actions can be saved for later review; switching is
unavailable until an action has a complete mapping and is enabled. Configuration limits are 16 saved monitors, 32 profiles and 64
actions/inputs per profile/monitor.

Appearance (Follow system/Light/Dark), notifications, startup and debounce remain
app-wide. Advanced can reveal input codes beside the input names. Closing hides
to the tray when available; without a tray it quits after the unsaved-edits prompt.

Since version 1.3.1, **Launch when I sign in** starts directly in the tray
on Windows as well as Linux. No separate minimized-start setting is needed.
Manual launches open the window; without a tray host, startup opens it too.
Unreadable or newer settings always open a recovery window, including at sign-in.
After upgrading from 1.3.0 on Windows, save settings once with startup enabled
to update the existing sign-in command. Shortcuts register while the window is hidden.

### Standard switching and other monitor brands

Each saved monitor has its own **Switching method**, selected when adding it or
in **Edit monitor & inputs**. **Standard DDC/CI (0x60)** supports Dell and other
monitors that implement standard Input Source writes. It uses the standard 0x51
source. **LG alternate (0xF4)** retains the existing configurable source address,
normally 0x50. Migrated setups keep the LG method; new monitor definitions default
to Standard. Groups can freely mix the two methods.

Input lists are read automatically on startup/Refresh, when connecting a monitor,
and when adding a new one. Each monitor is queried separately. New definitions
and old generic preset lists opt into automatic setup; customized legacy mappings
remain manual. The Add Monitor dialog has a 640×340 default and 540×300 minimum
so the description and selectors fit.

A usable advertised list replaces the generic choices for that monitor. Existing
input/action IDs, shortcuts and custom names are retained when their codes still
match. Single-monitor profiles gain actions for newly discovered inputs. Actions
whose inputs disappear are kept disabled with their name/ID/shortcut intact for
review. Unknown vendor values keep their numeric labels. Dell’s advertised 0x1b
is labeled USB-C only for Dell, not treated as a universal port code.

LG monitors often omit the alternate F4 value list. If they advertise standard
ports, the app can filter the monitor’s existing LG mapping for the documented
DP/HDMI equivalents; it retains that mapping’s labels/codes instead of guessing
a universal USB-C alternate code. If any advertised value cannot be mapped, or
the reply is missing/empty/invalid, the existing mapping remains intact with a
status explaining that automatic setup was unavailable. No write probes or
fallback switching commands are used. Relevant protocol notes are in the
[ddcutil LG wiki](https://github.com/rockowitz/ddcutil/wiki/Switching-input-source-on-LG-monitors).

The LG UltraFine **40U990A-W** has a model-specific **Thunderbolt** input using
`0xD2` with **LG alternate (0xF4)** and the configured source (normally `0x50`),
as [confirmed by the reporter of #33](https://github.com/phillip9933/LGInputSwitch/issues/33).
The shared Windows/Linux setup recognizes this exact model from live EDID,
ddcutil discovery or the capabilities model field. Development builds also match
the reported capabilities name `U990A` and EDID manufacturer/product **GSM / 0x9EBA**.
The EDID name alone can be the generic `LG ULTRAFINE`, so it does not identify the
model. The identifiers come from an owner's
[40U990A-W hardware output](https://note.com/a_244/n/n76eeb350686f); they still need
confirmation on the issue reporter's unit. Other UltraFine product IDs do not gain
this mapping. Automatic setup adds a separate
Thunderbolt input and, for a single-monitor profile, an action with no shortcut
assigned. It does not replace USB-C `0xD1` or assume that other LG models use
`0xD2`. If capabilities are incomplete/unavailable but the live model is known,
Thunderbolt is added while the existing input list is kept. Method presets also
include Thunderbolt when live discovery identifies this model. Saved/custom names
are not used as evidence of the hardware model, and only the allowlisted model
profile identifier is added to diagnostics; raw model/EDID/serial text stays private.
Manual input mode remains unchanged. Use **Refresh**, then **Save settings** to
apply automatic setup to an existing monitor. If the monitor reports a generic
model name, add a **Thunderbolt** input with `0xD2` manually in **Edit monitor &
inputs**. Other Thunderbolt models still require their own confirmed mappings.

Changes are drafts until **Save settings**. In **Edit monitor & inputs**, uncheck
**Read and set up this monitor’s inputs automatically** for a custom mapping.
Read advertised inputs can refresh an automatic list or append to a manual list;
method presets remain an explicit manual option.

Linux uses a bounded capability query on the resolved bus/display. Windows uses
an isolated timed child process: ADL’s selected monitor EDID must match exactly
one active Windows monitor with one physical handle, using the documented
[EnumDisplayDevices/SetupAPI link](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enumdisplaydevicesw).
Unmatched, duplicate, cloned or ambiguous native targets keep the existing
mapping. Native diagnostic indices are never assumed to be AMD indices. SetupAPI
is part of Windows; no additional third-party runtime library is introduced.

Standard switching uses ddcutil on Linux and AMD ADL on existing AMD Windows
connections. DDC/CI must
be enabled in the monitor, and some models/cables/docks/firmware do not implement
input switching or will not accept it while inactive. An advertised code is
useful evidence, not a compatibility guarantee. No automatic fallback sends both
standard and LG commands. Protocol background: [ddcutil Input Source example](https://www.ddcutil.com/getvcp_known_u3011_output/).

### Windows docks and standard connections (development)

After 1.3.3, native Windows monitor discovery also exposes monitors behind
DisplayLink docks and other graphics devices that AMD's interface cannot address.
A uniquely matched AMD connection retains AMD raw access. Other connections are
labeled **Standard DDC/CI** and use Windows' monitor API for explicit standard
`0x60` input writes. The native path does not require an AMD GPU or an added
runtime library. Actual monitor/driver/dock compatibility needs tester feedback.

This is not a raw-I2C transport: Windows' documented
[SetVCPFeature API](https://learn.microsoft.com/en-us/windows/win32/api/lowlevelmonitorconfigurationapi/nf-lowlevelmonitorconfigurationapi-setvcpfeature)
has no custom source-address argument. LG `0xF4` alternate switching is rejected
on these native connections with an explanatory message; no different command or
source is silently substituted. Some LG monitors ignore standard input writes,
even when they advertise `0x60`. Issue #5's LG-through-DisplayLink setup therefore
remains unconfirmed and may require a direct GPU connection for LG alternate
switching. Selecting **Standard** and loading its presets is an explicit test,
not a promised fix for that LG model.

Native discovery, capability reads and writes run in bounded child processes.
Each write rechecks the selected Windows display and its local identity; replaced,
missing or ambiguous targets are rejected. Physical monitor handles are released
on both success and failure. Native routes are saved under profile schema 4 so
older AMD-only builds reject them rather than treating Windows display numbers
as AMD indices. AMD/Linux setups without pending input recovery retain schema 3
and their mappings.

Diagnostic report format 3 identifies each selected connection's transport and
Windows display, automatic-input mode, known input profile, and EDID manufacturer/
product code. It reports these hardware fields for Windows displays even when
the app cannot select them. Raw EDID, arbitrary model text, interface paths,
serials and local identity hashes remain excluded. Standard DDC/CI reads can
succeed through a dock without proving support for LG's alternate writes.

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
config.json. The importer never writes that file. Once desktop-profiles.json exists,
it takes precedence over the old settings. The retired frontend can be found in
historical releases if needed for rollback.

The app reads workspace schemas 2, 3 and 4. Schema 3 adds an explicit automatic-input
preference and disabled actions with missing mappings. Schema 4 stores native
Windows connections and temporary input-recovery state. Inputs that disappear
from a complete advertised list disable affected actions until their original
input codes return, including after saving and restarting. Recovery retains
action names, IDs and shortcuts, respects manually disabled actions, and leaves
conflicting shortcuts disabled for review in **Edit actions**.
Older builds reject the newer schema rather than overwriting it; restore the
settings backup when rolling back.

On Linux, the workspace remains in the versioned `profilesV2` entry in the existing
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
their GUI, desktop integration and hardware handling. The standalone Linux CLI
uses nlohmann/json; the desktop importer and diagnostics use Qt JSON.

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

Install Qt 6.5+ development packages (Widgets, Concurrent and Network), a C++17
compiler, Python 3 and CMake. Windows Visual Studio 2022 requires CMake 3.21+.

```
cmake -S . -B build-desktop -DLG_BUILD_DESKTOP=ON
cmake --build build-desktop --config Release
ctest --test-dir build-desktop -C Release --output-on-failure
```

The executable is `LGInputSwitchDesktop` (Windows: `Release/LGInputSwitchDesktop.exe`).
Windows releases use one portable executable with Qt/runtime and notices embedded; CI tests it alone. Linux
needs the matching Qt runtime, desktop platform plugin and ddcutil. CMake installs
the Linux executable, launcher and icon. With the desktop option off, CMake builds
the portable regression tests and Linux CLI without Qt. Windows builds use CMake;
there is no separate Visual Studio solution or legacy target.

For a Linux qmake-only development environment:

```
mkdir -p build-desktop
cd build-desktop
qmake6 ../desktop/desktop.pro
make
```

## Application updates

Preferences includes automatic/manual checks, download progress/cancellation,
release notes, Later and explicit installation/restart. See [updates](updates.md)
for verification, platform behavior and recovery. The existing monitor controls,
profiles, shortcuts and system/light/dark themes are retained.

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
CI builds the portable desktop executable and runs preview smoke checks; physical tray, native
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

A live-session `kde_window_probe` uses only an isolated test component and a fake
ddcutil executable. It checks the migrated cycle action and separately named
group cycle/direct actions against the running shortcut daemon, then removes
its assignments. Offscreen rendering can be used for this daemon check without
sending keyboard events or physical-monitor commands.

Compact-layout regressions exercise 640×480 and 800×600 in both switching and
editing modes, across all global pages, including visible failed-monitor results.
They verify scrolling and separation of content, status and footer controls.
Reconnect regressions ensure another saved monitor’s connection is not offered.
Theme previews include compact editing and the normal switching view.

Automatic-input regressions use different synthetic Dell/LG capability lists.
They cover phantom-port removal, per-monitor USB-C naming, retained IDs/shortcuts,
retired actions, repeated reads, missing/empty/oversized lists, custom opt-out and
the real Add Monitor → asynchronous read → configured-input path. Read-only checks
on the maintainer’s connected Dell and LG confirmed different advertised lists;
no input-switch command was sent during those checks.

### Retired frontend compatibility

`desktop/legacy_config.*` reads old Windows config.json and pre-profile Qt
preferences, preserving input order/codes, disabled rows, shortcuts and appearance.
The Windows importer uses Qt JSON, limits input to 2 MiB and never writes the
original file. Malformed input produces defaults and a setup warning. Workspace
saves continue to use the existing atomic, versioned profile format.

`desktop/windows_hotkeys.*` contains the native Windows shortcut parser. Its
hardware-free tests run on Windows and Linux without Windows header stubs.
The former tray/dialog code, duplicate config writers, app_toggle wrappers,
Visual Studio project and obsolete CI executable artifact have been removed.
CMake and the Linux qmake entry points use the current desktop modules.
