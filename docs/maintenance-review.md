# Maintenance review — 26 October 2026

Reviewed main at `2f78747`, all six open issues, comments on the three hardware
reports, the Visual Studio build and GitHub Actions workflow, and the public
Project links. Implementation and test preparation used substantial Codex AI
assistance; the maintainer must validate hardware behavior before a stable release.

## Report assessment

| Report | Evidence | Assessment |
| --- | --- | --- |
| [#1](https://github.com/phillip9933/LGInputSwitch/issues/1) | Reporter confirmed v1.0.1 works with 45GX950A + 9070XT / driver 25.11.1, then reported startup failure | Original detection problem has reporter confirmation of a fix. Startup was never implemented despite the saved option. |
| [#4](https://github.com/phillip9933/LGInputSwitch/issues/4) | 34WP60C + RX6650XT blanks and returns to the same input | Unreproduced; transport acceptance does not prove the monitor accepted an input code. Need exact source/destination, config, driver, connection path and known-working command. |
| [#5](https://github.com/phillip9933/LGInputSwitch/issues/5) | 29U511A + Radeon 890M reports failure; Linux ddcutil commands work with F4/90 and F4/D0, source 50 | Useful known-working Linux protocol evidence, but Windows ADL path remains unverified. Capture ADL return status and target. |

Issues #6 and #8 already track compatibility triage and the Windows maintenance
release. Issue #7's claimed board setup is incomplete: README/CONTRIBUTING linked
to Project 2, **Kura roadmap**, which publicly contains Kura work only. The LG
repository Projects page has no linked projects and the owner's four public
projects contain no LG board. These docs now point to repository issues. A private
board could exist; it was not accessible through the public view. No unrelated
project was changed. No open pull requests were returned by the GitHub search.

## Confirmed code defects addressed

- Apply `startWithWindows` via the current user's Run key at launch and after
  Settings saves; provide a checkbox and report registry errors. Paths are quoted
  and use Unicode. Disabled means remove only this app's value.
- Failed sends no longer advance cycle state; direct selection remembers its
  position in the configured cycle order; saving settings resets stale position.
- Preserve configured input codes for enabled inputs instead of replacing them
  with model-specific defaults on every Settings save.
- Reject malformed/out-of-range command values before ADL, rather than silently
  parsing/truncating them. Reject malformed and duplicate hotkey combinations;
  report OS registration failures and suppress held-key repeat.
- Failed/partial ADL initialization unloads and resets pointers, allowing retry;
  only a successful control initialization counts as initialized. Initialize
  AdapterInfo size fields before enumeration.
- Save to a sibling temporary file, flush/close it and replace config.json only
  after success. A save failure keeps Settings open and leaves in-memory config
  unchanged. The app directory must still be writable.
- Allocate room for the UTF-8 conversion terminator. Failure notifications now
  include ADL status/target/command and safely truncate long notification text.

The packet, VCP F4, source 50 defaults and ADL transport are unchanged. These
changes do not establish a fix for #4 or #5. The write call has no monitor readback.

## Verification and remaining release gate

Hardware-free C++ regression checks cover invalid commands never reaching the
transport, failed-send retry, wraparound/stale cycle position and hotkey parsing.
They passed locally under GCC with address/undefined sanitizers (leak detection
disabled because the sandbox uses ptrace). CI also runs the tests on Linux and
Windows and tests startup registration/readback/removal in an isolated Windows
runner, with a test executable path containing spaces. Native Windows builds and
tests require the CI result; this Linux environment has no Windows runtime.

Before stable publication, record:

1. Windows x64 build and regression results, including startup tests.
2. Real Windows logoff/logon with startup enabled and disabled, including a path
   with spaces; check Windows Startup Apps has not independently disabled it.
3. Upgrade with an existing config, custom codes/order/hotkeys and repeated Settings
   open/save; test an unwritable folder and malformed config without losing files.
4. Direct and cycle switching on the maintainer's original 7900XTX + 45GX950A-B,
   including a failed send and a conflicting hotkey.
5. Matching-hardware evidence for #4/#5, or explicit remaining limitations in the
   candidate notes. Do not close these reports merely because this patch builds.

## Remaining architectural concerns

The main thread sleeps 700 ms per send, config paths use the ANSI executable path,
target selection relies on unstable adapter/display indices, multiple configured
targets are silently reduced to the first, and the settings UI exposes only fixed
input labels. Modeless dialog lifecycle and hotkey suspension while editing also
need focused UX work. Keep these separate from the maintenance release. Config
schema validation/recovery and moving installed-app config to a writable per-user
directory need explicit migration tests.

References: [ADL DDC API](https://gpuopen-librariesandsdks.github.io/adl/group__I2CDDCEDIDAPI.html),
[Microsoft Run keys](https://learn.microsoft.com/en-us/windows/win32/setupapi/run-and-runonce-registry-keys).
