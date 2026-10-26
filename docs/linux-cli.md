# Experimental Linux CLI

The first Linux frontend invokes the distribution's installed `ddcutil` through
an argument vector. It uses the same strict numeric-value validation as Windows.
No external source implementation was imported. Implementation and tests used
substantial Codex AI assistance.

## Build and test

Requirements: Linux, a C++17 compiler, CMake 3.16+, and Python 3 for tests.
ddcutil is needed at runtime, not for building or hardware-free tests.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

To compile the CLI directly without CMake:

```
g++ -std=c++17 cli/main_linux.cpp platform/linux/ddcutil_backend.cpp -o lginputswitch
python3 tests/linux_cli.py ./lginputswitch
```

Use `cmake --install build --prefix "$HOME/.local"` for an optional current-user
installation. The CLI has no service, autostart, tray or global shortcut process.
Desktop shortcut settings can invoke an explicit `switch` command for now.

## Device access

Install your distribution's `ddcutil` package. ddcutil requires access to monitor
I2C buses, normally via its packaged udev rules and the kernel's `i2c-dev` module.
Follow [ddcutil permissions documentation](https://www.ddcutil.com/i2c_permissions/).
The CLI does not elevate, modify permissions, load modules or install packages.

```
./build/lginputswitch detect
```

Discovery forwards ddcutil's output. Select the correct display or bus from that
output; display numbers can change when the display topology changes. Every
switch requires exactly one explicit target. On hybrid systems the relevant GPU
is the one physically driving the selected monitor.

## Switching

```
./build/lginputswitch switch --display 1 --input 0x90 --dry-run
./build/lginputswitch switch --display 1 --input 0x90
./build/lginputswitch switch --bus 6 --input 0xd0
```

`--dry-run` prints the complete argument vector as JSON and launches no process,
even if ddcutil is missing. The first real command above is equivalent to:

```
ddcutil --display 1 setvcp 0xf4 0x90 --i2c-source-addr=0x50 --noverify
```

The profile is LG's alternate input-switch path: VCP F4 with source address 50.
Source can be overridden explicitly using `--source` for a known profile. Input
and source accept decimal or 0x-prefixed hex; command values are normalized to
explicit hex for ddcutil. Display selectors start at 1; bus selectors start at 0.

Existing default mappings are DP=D0, USB-C=D1, HDMI1=90, HDMI2=91; these vary by
monitor. USB-C in particular must be verified for the selected model. Use known
codes, not guessed scans. The program sends no extra readback after a write,
because switching away may make the display unavailable. A zero exit status means
ddcutil accepted the command, not that the monitor visibly changed inputs.

## Errors and verification

Invalid CLI arguments return 2 before launching ddcutil. Missing dependency returns
127; launch/wait failures return 126. Child exit status is propagated, and a child
signal maps to 128 + signal number. ddcutil output and errors remain visible.

Local GCC build with warnings treated as errors passed. Ten Python integration
tests exercise detection, both #5 command values, decimal conversion/bus zero,
write-free preview, invalid/duplicate/injected arguments, child failures/signals,
missing/non-executable dependencies, and help. These tests use a temporary fake
ddcutil and cannot write to a monitor. Existing C++ regressions remain separate.

A real discovery attempt with installed ddcutil 3.0.2 reported no `/dev/i2c`
devices and returned status 1, which the CLI propagated. No real input-switch
command was executed. There is no hardware-verified Linux support claim yet.

For hardware acceptance, first record the successful standalone ddcutil command,
then compare this CLI's preview and test the same operation in both directions.
Record OS, GPU and driver, monitor and firmware, connection/dock, source/input
codes and observed behavior. Start with AMD and #5's known-working LG commands,
then validate Intel and NVIDIA independently. Verify permission errors and
disconnect/reconnect without sending to an unintended monitor.
