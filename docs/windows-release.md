# Windows portable app

Download `LGInputSwitch-1.3.4-Windows-x64.exe` from the
[1.3.4 release](https://github.com/phillip9933/LGInputSwitch/releases/tag/1.3.4),
put it in a writable folder and run it. There is no installer, Qt DLL folder or
separate Microsoft runtime setup. LG alternate switching uses an AMD-driven
connection. Standard DDC/CI connections, including Windows dock discovery, are
also available; support for a particular monitor/dock still needs verification.
LG's alternate command cannot be sent through the native Windows DDC/CI path.

Choose your monitors, review the input mappings and save. Profiles, tray controls,
shortcuts, themes and diagnostics work as before. **Launch when I sign in** starts
in the tray; save settings after moving/renaming the executable to update its path.

## Upgrade and updates

Quit the old app and back up config.json and desktop-profiles.json before a manual
upgrade. Keep them beside the replacement executable. Keep the filename and folder
stable for future upgrades. Old Qt DLL/runtime folders are not needed by the new
portable executable. Earlier releases remain available for rollback.

The app retains read-only configuration import from older versions, including
after retirement of the original Win32 frontend and Visual Studio project. Complete profiles
are saved atomically in desktop-profiles.json; appearance preferences and update
check preferences use the existing per-user Qt settings. No updater replaces
settings files, changes monitor mappings or alters Windows Startup Apps policy.

Starting with 1.3.2, **Preferences → Application updates** can check GitHub, show
release notes, download a verified update, and install/restart when you choose.
The app asks about unsaved changes and keeps the previous executable. See
[update behavior, trust and recovery](updates.md). The first updater-capable
version must be downloaded manually.

To uninstall, disable startup, quit and remove the executable. Keep or remove your
settings separately. Windows may show SmartScreen prompts because builds are
unsigned. SHA256SUMS verifies the downloaded bytes against the release manifest.

## Source and licenses

The release has three uploaded assets: the Windows EXE, Linux package and
SHA256SUMS. GitHub supplies application-source ZIP/tar.gz downloads from the exact
tag. Qt source is supplied separately through the pinned official archive link
in the release notes and [source recipe](../packaging/windows/qt-sources.json).
Those links must remain available while binaries are distributed.

The executable embeds the project/dependency license texts, attribution, source
links, checksums and rebuild instructions. Export them to a text file with:

```
LGInputSwitch-1.3.4-Windows-x64.exe --licenses notices.txt
```

Project code is AGPL-3.0-or-later; Qt uses LGPLv3 and bundled third-party terms
remain intact. Qt and the C++ runtime are statically linked in the portable build.
No AMD driver DLL is included; the installed AMD driver is loaded from Windows.
No signature/integrity enforcement prevents rebuilding or running modified Qt or
application code. The updater only verifies downloaded updates before installation.

## Rebuild the Windows portable executable

Use the Visual Studio 2022 x64 developer environment with CMake 3.21+, Ninja and
Python 3. The repository includes the exact source URL, SHA256 and Qt configure
arguments. Run from an exact release checkout:

```
python scripts/build-static-qt.py --parallel 2
cmake -S . -B build-desktop -G Ninja -DCMAKE_BUILD_TYPE=Release -DLG_BUILD_DESKTOP=ON -DCMAKE_PREFIX_PATH=build-static-qt/install -DLG_DISTRIBUTION_DIR=build-distribution
cmake --build build-desktop --parallel 2
ctest --test-dir build-desktop --output-on-failure
python scripts/windows-release.py --executable build-desktop/LGInputSwitchDesktop.exe --output build-windows-release
```

Use absolute prefix/distribution paths if configuring outside the repository.
The SDK cache records its exact source/configuration. To rebuild modified Qt,
extract and modify the linked Qt source, configure it with the recorded options
and a separate prefix, build/install it, then point CMAKE_PREFIX_PATH at that prefix.
The full application source is supplied, so it can be recompiled/relinked against
that modified SDK. No proprietary application objects or signing key are needed.

The official workflow inspects PE dependencies and runs the executable alone
with Qt/compiler SDK paths removed, exports embedded notices, renders both themes,
and tests native update replacement/rollback with synthetic executables. It also
runs shared backend, profile and startup tests. No physical monitor commands are
sent. Real Windows hardware, sign-in and DPI/theme testing remain limited.
