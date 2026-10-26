# Cross-platform architecture proposal

This is a technical design, not a duplicate work board. Issues and release
trackers remain the source of truth for live status and scope.

The long-term product is **one LGInputSwitch application for AMD, Intel and NVIDIA
systems**, with shared settings, monitor profiles and UX, rather than separate
GPU-specific forks. Current Windows support remains AMD-only until each new
backend has hardware evidence.

## Delivery order and gates

1. **Windows maintenance:** ship focused fixes and diagnostics after CI and
   maintainer hardware validation. Preserve the existing tray and config contract;
   compatibility claims require evidence for each reported monitor/GPU.
2. **Shared core and Linux first:** establish portable builds, strict command
   validation, model/profile definitions, ordered selection and transport result
   types. Prove explicit Linux switching through a CLI before desktop integration.
3. **Linux and Windows UX:** use the validated transports to improve setup,
   monitor selection, custom input mappings, shortcuts, diagnostics and recovery.
   Choose a shared UI framework only after a small tray/hotkey prototype on both
   operating systems. Qt 6 now supplies the optional shared desktop frontend; the original Windows UI remains available pending hardware validation.
4. **macOS last:** first prototype monitor access on actual Intel/Apple Silicon
   hardware and each connection type, then add the backend and desktop experience.
   Do not promise support based on the Linux implementation.

Intel and NVIDIA Windows transports are explicit long-term scope. Design their
interfaces during Linux extraction; implement and validate them as separate
backend increments after the initial Linux path and during the shared desktop
phase, without holding up Windows maintenance or requiring a new app per GPU.
macOS remains the final platform milestone. An OS/vendor/monitor matrix determines
support; adding a backend is not a promise that every combination works.

No stable version or date is selected until release scope and test hardware are
known. Stable publication depends on verification, not elapsed time.

## Boundaries

The first extraction implements `core/input_switcher.*`: typed commands/results,
target-bound `InputTransport`, validation, ordering and cycle state. Windows wraps
the existing ADL bridge in `platform/windows/adl_transport.*`; Linux implements
the same interface through ddcutil. Core regression tests compile without Win32,
ADL or process APIs. CMake builds the full Windows app as well as the Linux CLI,
alongside the original Visual Studio solution.

Discovery and OS services remain separate frontend/backend code in this increment.
The Windows JSON schema/location are unchanged; no migration is performed. Stable
display identity, structured Linux discovery and full config extraction remain
under #10. The transport interface can grow to support these after validation.

Keep C++17 initially; the working Windows ADL bridge and protocol need no language
rewrite. CMake supplies a portable build alongside the existing Visual Studio
solution until parity is proven. Separate these concerns:

| Layer | Responsibility |
| --- | --- |
| `core` | validated values, input profiles, cycle order/state, portable config schema and structured results; no Windows/ADL/UI headers |
| Windows backends | AMD ADL now; Intel IGCL and NVIDIA NVAPI later; OS error mapping and per-display backend routing; current-user startup and hotkey services separately |
| Linux backend | discovery and writes through installed ddcutil initially; device access errors and exact child status |
| macOS backend | future feasibility-tested monitor transport; no emulated ADL |
| Frontends | CLI first on Linux, existing Windows tray, later tray/settings UX; serialize work on a worker to keep UI responsive |

Use a transport interface with `discover()` and `setInput(target, profile, value)`.
A target records backend, stable display identity where available, current native
route and an explicit user choice; never assume Windows adapter IDs map to Linux
display numbers. A result distinguishes invalid configuration, no target,
permission denied, missing dependency, transport rejection, and **command sent
without readback verification**. Preserve raw backend status for diagnostics.

Keep the LG F4/source-50 profile separate from standard input VCP 60 and future
model-specific mappings. Never probe random codes or cycle through unselected
monitors. The current defaults are evidence for specific displays only.

## Linux initial implementation

Use `ddcutil detect` for discovery and an explicit `--display` selector for writes.
The initial CLI supports `detect`, `switch` and a write-free preview. Invoke the
executable with an argument vector, never a shell command; validate selectors,
source and input values before launching it. Propagate child failures and keep
stderr visible. Do not request a VCP readback after switching away from the host.

The known-working commands in #5 provide the first manual comparison:

```
ddcutil -d 1 setvcp 0xf4 0x90 --i2c-source-addr=0x50 --noverify
ddcutil -d 1 setvcp 0xf4 0xd0 --i2c-source-addr=0x50 --noverify
```

Use distribution packages and ddcutil's device-permission setup. Report missing
`i2c-dev`, unavailable monitor buses and permission errors; do not run the app as
root or automatically alter permissions. Test AMD first with a monitor whose
command is known, then evaluate Intel/NVIDIA individually. GPU independence is a
goal, not a verified compatibility promise.

Tests use a fake ddcutil process to verify exact argv, malformed arguments,
preview with no process execution, missing dependency and child failure. Hardware
acceptance compares CLI switching with the exact working ddcutil command in both
directions and records monitor/GPU/driver/cable path. No Linux tray/hotkey or config
migration is implied by the CLI prototype.

## Intel and NVIDIA consolidation

Reviewed the Intel reference's `src/Program.cs` and the NVIDIA reference's
`lg_switch.py`, in addition to their README and license files. The Intel approach
uses IGCL `ctlI2CAccess`, with the LG source byte carried in its Offset field.
The NVIDIA approach uses NVAPI raw I2C. They support the architectural direction
of one shared profile/command model with multiple native transports, not copying
entire tray apps into this repository.

| Reference | Candidate backend | Integration constraints |
| --- | --- | --- |
| [Jason7536/lg-input-switch](https://github.com/Jason7536/lg-input-switch) | Intel IGCL discovery + I2C write | MIT license; retain notices for reused code. Its implementation requires elevated writes. Validate with current Intel documentation and decide a narrow privilege boundary before integrating. |
| [meer-cha/lg-input-switch](https://github.com/meer-cha/lg-input-switch) | NVIDIA NVAPI discovery + raw I2C | Repository LICENSE is GPL v3. The maintainer has selected AGPL-3.0-or-later for future LGInputSwitch releases. Review the exact source licenses and preserve attribution before any reuse; no code has been imported. Implement from vendor API documentation and independently tested protocol requirements. |

No reference code is imported by this patch. Repository activity alone is not
evidence that a project is abandoned; consolidation does not depend on that claim.
Do not adopt experimental Picture-by-Picture commands as part of input switching.

Backend selection follows the GPU driving each selected display, especially on
hybrid laptops; do not select the first available GPU or broadcast writes across
all outputs. Discovery must retain vendor/native route, report unsupported
connections and re-resolve after hotplug. Shared UI and config must expose a
consistent experience while backend diagnostics preserve driver-specific detail.

Acceptance for each vendor includes real hardware discovery, one explicitly
selected monitor, known input codes, direct and cycle switching, failed-write
handling, reconnect/resume, startup and shortcut behavior, mixed-vendor systems,
and clear permission handling. Keep tested combinations in a compatibility matrix
with vendor, OS, GPU/driver, monitor/firmware, cable/dock, profile and evidence.
Evaluate Linux on all three vendors through ddcutil separately from Windows APIs.

## Configuration and desktop UX

Keep legacy Windows JSON intact during the maintenance release. Introduce a
versioned portable schema later, migrating a copy with rollback and round-trip
tests. Use per-user writable storage and explicit portable mode; select OS-native
startup behavior without reusing `startWithWindows` as a Linux setting. Keep
custom input codes, enabled inputs, cycle order and shortcut intent.

For Linux, evaluate X11 and Wayland separately: global shortcuts and tray support
depend on the compositor/desktop. Prototype the GlobalShortcuts portal and
StatusNotifier support and document fallbacks before selecting a framework.
The CLI provides an interim command for desktop-configured shortcuts. Windows
keeps RegisterHotKey until replacement parity is proven.

UX acceptance should cover zero/one/many monitors, stale target after reconnect,
clear command-sent versus confirmed-switch messages, shortcut conflicts, saved
custom profiles, actionable failures and a responsive tray during writes.

## macOS feasibility gate

Inventory supported display transports and public APIs on current macOS, then
test external DP/HDMI/USB-C displays on actual hardware. Research existing monitor
control projects and license constraints before reuse. Intel and Apple Silicon
may need distinct paths, with docks/adapters adding limitations. Decide support
only after a safe explicit input switch succeeds. Then add macOS packaging,
shortcuts, login items, signing and notarization as platform work.

References: [ddcutil permissions](https://www.ddcutil.com/i2c_permissions/),
[ddcutil source/docs](https://github.com/rockowitz/ddcutil),
[ADL DDC API](https://gpuopen-librariesandsdks.github.io/adl/group__I2CDDCEDIDAPI.html).
