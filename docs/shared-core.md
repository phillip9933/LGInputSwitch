# Shared switching core

This first extraction keeps the existing Windows tray and the experimental Linux
CLI working while putting their switching behavior behind a shared C++17 boundary.
It does not introduce a new config schema or UI framework. Substantial Codex AI
assistance was used for implementation, review and regression tests.

## Data flow

```
Windows tray/settings -> app_toggle adapter -> portable input/cycle core
                                             -> AdlTransport -> existing AMD bridge

Linux CLI parser -> typed InputCommand -> DdcutilTransport -> installed ddcutil
```

`core/input_switcher.h` owns input definitions, commands, results and the transport
interface. The core has no Windows, ADL or POSIX includes. `InputTransport::Send`
operates on one target selected by the frontend and bound into the transport.
Adapter/display indices and Linux display/bus selectors are never interchanged.

Commands retain full unsigned values until range validation (source/feature byte,
16-bit value); no narrowing conversion can silently change the requested write.
Each backend validates commands and its native target again. Both initial
backends accept the existing F4 LG input profile only. Future Intel/NVIDIA
transports can implement this interface without duplicating tray or cycle logic.

Results distinguish invalid arguments, backend failure and command sent without
readback verification. Native status and backend details are preserved. More
specific driver/permission classifications and discovery belong in later backend
work; a sent result never proves the physical input changed.

`OrderInputs` preserves the established Windows order: configured enabled labels
first, then remaining enabled labels alphabetically, with duplicates removed.
`InputPosition` maps direct selection to that order. `CycleInputs` commits the next
position only on an accepted send; a retry targets the same failed input.

## Build and verification

CMake builds `lginput_core`, the platform frontend and regression executables.
On Windows, it also builds the full Win32 tray app using the same resources,
Unicode settings, C++17 and static MSVC runtime as the existing solution. CI still
builds the original solution too, so either build description drifting causes
visible build failures. Linux installs only the CLI, not a desktop service.

Local GCC/undefined-sanitizer checks passed for:

- portable core command/result and ordering/state regression checks;
- existing Windows adapter/hotkey regression checks with a fake ADL entry point;
- all 10 real-CLI/fake-ddcutil integration tests, including unchanged argv/status.

Core tests verify that invalid strings never reach the transport, known F4/source
values survive conversion, native failure details are preserved, custom input
codes survive ordering, and failure/direct selection interact correctly with the
cycle. Backends continue to require hardware acceptance as described in the
maintenance and Linux verification documents.

## Deliberate remaining work

Windows monitor discovery still lives in Settings; Linux discovery currently
forwards ddcutil output. Stable identities, structured discovery, a portable config
schema with migration/rollback, asynchronous UI writes and desktop services remain
separate work. This extraction does not close #10 by itself or change any hardware
compatibility claim. Live scope remains in #10–#14.
