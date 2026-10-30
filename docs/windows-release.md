# LG Input Switch 1.2.0 — Windows x64

Download `LGInputSwitch-1.2.0-Windows-x64.zip` from the numeric `1.2.0` release.
Extract the complete ZIP to a writable folder in your user profile. Keep the
folders, DLLs and notices beside `LGInputSwitchDesktop.exe`; start that executable.
Windows currently requires an AMD GPU/driver. Intel/NVIDIA backends and macOS
support remain roadmap work.

If Windows reports missing MSVCP/VCRUNTIME DLLs, run the included, unmodified
`runtime/VC_redist.x64.exe`. It is the official Microsoft installer, whose
Authenticode signature is checked during packaging. It presents Microsoft's own
license terms; Microsoft files are not relicensed by the application's AGPL.
Existing newer VC++ runtimes can be retained. No runtime installer is executed
silently by this application.

Choose your monitor explicitly, review enabled inputs/codes and save. Input
switching uses LG's F4/source-50 profile. Codes differ across monitor models; do
not guess or scan codes. Settings include direct, cycle and previous-input
shortcuts, notifications, system/light/dark appearance and start-on-sign-in.
Previous input remembers accepted selections in this session, not the initial
physical input. A success notification means the command was sent without readback.

For troubleshooting, open **Diagnostics → Create report**, review it, then copy or
save it with your issue. It includes the last command, read-only monitor checks,
advertised controls/input values and system/build details. No input is switched
and nothing is uploaded automatically. See [diagnostic details](desktop-ui.md#built-in-diagnostics).

## Upgrade and legacy frontend

Quit any older LGInputSwitch before upgrading. Back up its config.json, then copy
that file next to the new executable before starting it. The modern frontend
reads the legacy first target, custom mappings, enabled order and shortcuts;
additional configured targets are retained when saving. Its appearance and
previous/disabled-row preferences are stored per-user by Qt. Restore your backed-up
config and old executable to roll back. Do not extract over a running executable.

`LGInputSwitch-1.2.0-Windows-legacy-x64.zip` contains the original Win32 frontend
with maintenance fixes, for users who prefer it. Run only one frontend at a time:
they share config.json conventions, the Windows startup entry and global keys.
Both folders must remain writable for config.json. Windows Startup Apps may
independently disable an enabled startup entry. Disable startup in the app before
removing its folder; uninstalling is otherwise just quitting and removing the
extracted folder. Keep your backed-up configuration if you want to retain it.

## Development checkout: monitor profiles

The next update adds profiles to the modern frontend. Existing 1.2 config.json
and per-user preferences are imported into one Main monitor profile. Confirm the
connection and save. The new workspace is stored atomically in
**desktop-profiles.json beside LGInputSwitchDesktop.exe**; keep this file when
moving/upgrading the modern app. The original config.json is preserved for legacy
rollback. The legacy frontend does not support profiles or grouped commands. Per-monitor
method selection also adds standard DDC/CI input-source commands (0x60), including
for compatible Dell models, through the existing AMD backend. This does not remove
the Windows AMD requirement.

Use + Profile for an independent set of actions, + Monitor to assign displays,
and each monitor's Edit menu to maintain its shared input-code mapping. One action
can select a different input on each monitor. Shortcuts and tray entries name the
profile; partial failures have per-monitor results and a failed-only retry.
See [profile setup](desktop-ui.md#monitor-profiles-development-checkout).

## Licenses, corresponding source and replacement libraries

Project code is AGPL-3.0-or-later; full terms are in LICENSE.md. MIT dependency
notices and embedded-code attribution are in licenses/ and THIRD_PARTY_LICENSES.md.
The earlier adapted upstream AMD implementation and unused CLI have been removed
from this release. The replacement in platform/windows/ is newly written against
AMD's documented API; the protocol encoder in core/ constructs the documented
wire data. Historical inspiration and upstream acknowledgement remain credited.
No AMD driver DLL is distributed; the app loads the installed x64 driver from the
Windows system directory.

The modern frontend dynamically links **Qt 6.8.3** under LGPLv3. Copyright The Qt
Company Ltd. and other authors identified in the included notices and sources.
`qt-notices/` contains the exact upstream license texts, attribution records and
third-party source notices for the shipped Qt modules. `QT-SOURCE.md` identifies
the complete qtbase/qtsvg source archives attached beside the application ZIP.
These archives are pinned by SHA256 in packaging/windows/qt-sources.json. The
Windows bundle omits the optional Mesa software-OpenGL DLL and Microsoft shader
compiler; this app uses raster Widgets and does not require Qt Quick/OpenGL.

Corresponding application source and build/packaging scripts are attached as
`LGInputSwitch-1.2.0-source.tar.gz`. SOURCE_REVISION identifies the exact tested
Git commit. BUILD-INFO.json records the Qt source hashes and runtime-installer hash;
MANIFEST.json hashes the extracted bundle files. The release-level SHA256SUMS
covers all downloadable application/dependency source and package assets.

To rebuild the app, use Visual Studio 2022 C++ tools (Windows SDK), CMake and Qt
6.8.3 MSVC2022 x64. Configure `cmake -S . -B build -A x64 -DLG_BUILD_DESKTOP=ON`,
build with `cmake --build build --config Release`, and run
`ctest --test-dir build -C Release --output-on-failure`. The full release workflow
and exact SDK setup are in .github/workflows/build.yml and release.yml.

To replace Qt, build the attached Qt module sources for Windows x64 using the
upstream README/CMake build instructions and a matching MSVC runtime. The original
SDK configuration files are in qt-build-config/. Build qtbase first, then qtsvg
against that installation. Quit the app and replace its Qt DLLs/plugins with
interface-compatible builds, retaining the matching platform plugin. Rebuild or
relink the application if your Qt build changes the ABI. No integrity check or
signature enforcement prevents replacement, debugging or relinking of LGPL code.
Our application license adds no prohibition on debugging those modifications.
See https://doc.qt.io/qt-6.8/windows-building.html for platform build prerequisites.

## Verification limits

The release workflow builds both frontends, tests the real Windows loader/packet
path against isolated fake driver DLLs, exercises failure/retry/targeting and
startup registry behavior, and launches the extracted modern ZIP with Qt SDK PATH
entries removed. No test sends monitor commands. Linux physical switching and
shortcuts were tested by the maintainer; physical Windows AMD switching, real
sign-in, DPI/theme changes and hardware reports #4/#5 remain unverified. These
limits are stated in this full release rather than implied to be hardware fixes.

The archives are unsigned. Windows may display SmartScreen prompts for an
unsigned/new application. Checksums verify file integrity, not publisher identity.
