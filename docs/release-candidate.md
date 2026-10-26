# Candidate publication plan

## Proposed releases

Publish a **Linux 1.1.0 beta 1** first, marked as a prerelease. Its initial test
environment is CachyOS/KDE Plasma 6 on Wayland. Keep the modern Windows frontend
experimental. Do not describe this candidate as a stable cross-platform release.
The focused legacy Windows maintenance candidate can ship separately once its
remaining Windows checks are supplied. No release tag or release has been created.

## Linux beta notes

- New Qt desktop app with a scalable tray icon and system/light/dark themes.
- Select the monitor explicitly; configure enabled inputs, labels, mappings and
  order. Discovery and sends run off the GUI thread.
- Linked KDE shortcuts: Cycle inputs, Previous input and each enabled input.
  Edit the same assignments in the app or Plasma Shortcuts. Conflicts with other
  applications or manual commands are reported without taking their keys.
- Previous input toggles accepted selections from the current session; the app
  does not guess which input was active when it launched.
- Optional launch on sign-in; background startup keeps the app in the tray when
  available. Per-user settings, startup and package installation remain separate.
- Native Arch/CachyOS package installs GUI, CLI, launcher, icon and license notices.
  Candidate artifacts include corresponding source, exact revision and checksums.
- Project license changes from MIT to AGPL-3.0-or-later for future revisions.
  Earlier MIT revisions and third-party dependency terms remain unchanged.

The maintainer confirmed discovery, physical switching, tray/settings, physical
shortcut activation and the linked Applications entry on CachyOS/KDE Wayland,
using the attached LG UltraGear with DisplayPort as the host's connection.
Automated checks use fake hardware and do not send real monitor commands.

Known limitations: one selected monitor; LG F4/source-50 profile only; monitor
codes vary; previous history is session-only; bus numbering can change after
hardware changes; other compositors and Intel/NVIDIA GPU combinations are not
verified. GNOME tray support may need an extension. No macOS backend exists yet.
See [installation/upgrade/removal](linux-install.md).

## Windows maintenance notes

The legacy frontend now applies startup settings, preserves custom input codes,
validates values and hotkeys, reports registration/ADL failures, preserves cycle
position after failed sends and saves config through a temporary replacement.

CI builds both the Visual Studio and CMake targets and checks registry startup,
configuration-independent core behavior and fake-backend desktop flows. There is
no current Windows machine for real tray/hotkey/theme, logon, config-upgrade or
AMD monitor verification. Issues #4 and #5 remain unconfirmed; these changes must
not be presented as fixes for those specific monitor/GPU failures.

## Publication evidence

The stacked review order is #9 → #15 → #16 → #17 → #18 → #19 → packaging/release
preparation. Preserve their commits when integrating, so source provenance and
existing check evidence stay traceable. Re-run the full workflow against the
final combined main revision before making a tag.

Before publishing the Linux beta:

- Full final-revision Linux, Windows build and KDE integration checks pass.
- Native `.pkg.tar.zst` is built/tested as a normal user; inspect the manifest and
  stage an install without changing the maintainer's active application.
- Confirm exported source revision and package checksums match that exact build.
- Include the source archive, PKGBUILD and SHA256SUMS next to the binary package.
- Use the beta notes above and make the release a prerelease. No Linux package or
  key is signed yet; checksums detect corruption, not publisher identity.

Before publishing Windows binaries, resolve the pre-existing upstream provenance
question for adapted amildahl/amdddc-windows code: no explicit upstream license
was found during review. Retain all existing attribution; do not claim that the
maintainer's license change grants rights to third-party code. The bundled AMD
SDK headers themselves carry MIT notices (corrected from the old EULA claim).
The Linux source package excludes that inherited Windows implementation, which
is neither compiled nor needed on Linux. The Windows Qt runtime also needs its complete applicable dependency notices
and corresponding-source distribution reviewed before public release.

Stable Windows promotion additionally requires the real-hardware checks in
[maintenance-review.md](maintenance-review.md) and [desktop-ui.md](desktop-ui.md).
Linux package startup/logon and a fresh-machine install remain manual checks;
unit tests cover entry creation, quoting, disable, failures and preference state.

## Next roadmap increments

1. Gather Linux beta feedback: reconnect/resume, multiple display topologies and
   startup on a clean CachyOS account; improve UX in the shared frontend.
2. Obtain Windows testers for upgrade/startup/tray/hotkeys and the reported AMD
   monitor combinations before replacing the legacy UI.
3. Add Intel and NVIDIA backends to this same app, with per-display routing and
   hardware evidence; compare the existing fork approaches and licenses first.
4. Prototype macOS monitor access on real Intel/Apple Silicon hardware, then add
   its UI integration, packaging, signing and notarization.

Open issues remain the public work tracker; the formerly linked Kura board was
unrelated and has been removed from project links. A dedicated LG board remains
separate repository administration work.
