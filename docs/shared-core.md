# Shared switching core

The portable C++17 core is used by the Windows/Linux desktop app and standalone
Linux CLI. The original Win32 tray/dialog frontend and its Visual Studio project
are retired. Substantial Codex AI assistance was used for implementation and tests.

## Data flow

The Qt desktop dispatches saved profile/action IDs, resolves each monitor's current
connection, then sends its command through the selected platform transport. Grouped
actions retain per-monitor results and retry only failed members.

- Windows AMD connections use `AdlTransport` and the installed AMD driver.
- Native Windows connections use the bounded Windows DDC/CI child-process path.
- Linux uses the installed ddcutil through `DdcutilTransport` and bounded processes.
- The CLI uses the portable command/cycle helpers and the Linux transport directly.

`core/input_switcher.*` owns input definitions, commands, results and the
`InputTransport` interface, with no OS or UI headers. A transport is bound to one
selected target; adapter/display indices and Linux display/bus selectors are not
interchanged. Commands keep full unsigned values until validation. LG alternate
`0xF4` commands retain their custom source; standard `0x60` writes use source `0x51`.
Native Windows connections cannot express LG's custom source address.

Results distinguish invalid arguments, backend failure and command sent without
physical readback verification. `OrderInputs` retains configured enabled labels,
then appends remaining enabled inputs alphabetically. `CycleInputs` commits the
next position only after an accepted send.

## Configuration and OS services

`desktop/profiles.*` owns versioned workspace persistence, validation, monitor
identity resolution and profile history. Saves remain atomic and preserve
unreadable/newer files. `desktop/legacy_config.*` is a read-only importer for old
Windows config.json and pre-profile Qt settings; the desktop no longer depends
on the retired frontend or its JSON library/writers.

`desktop/windows_hotkeys.*` parses native Windows shortcuts independently of the
Windows SDK. Platform startup helpers remain separate from monitor transports.
The Qt window, tray and native shortcuts share the same saved action dispatch.

## Build and verification

CMake builds the portable core and hardware-free regressions. Enable
`LG_BUILD_DESKTOP=ON` for the Qt app; Linux also builds the standalone CLI. Windows
uses CMake with the Visual Studio toolchain, without a separate solution file.
The Linux qmake application, smoke-test and KDE probe entry points are also built
in CI. See [desktop builds](desktop-ui.md#build).

Regressions cover strict command parsing and wire packets, cycle/history state,
AMD transport guards and fake driver failures, native Windows routing, shortcut
parsing, read-only settings migration, workspace preservation, grouped switching,
diagnostic privacy and update verification. Tests use synthetic displays, fake
drivers/processes and simulated network replies. Hardware support still requires
feedback from owners of the affected monitor, GPU and connection.
