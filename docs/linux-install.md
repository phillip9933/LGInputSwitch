# Linux installation

The first supported test environment is CachyOS with KDE Plasma 6 on Wayland.
The maintainer has verified discovery, input switching, tray/settings and linked
Plasma shortcuts on an LG UltraGear over DisplayPort. This is one tested setup,
not a compatibility claim for every LG monitor or GPU. Intel/NVIDIA validation
and other desktops remain future work.

## CachyOS / Arch package

The [1.2.0 release](https://github.com/phillip9933/LGInputSwitch/releases/tag/1.2.0) contains a native `.pkg.tar.zst` package, its corresponding
source archive, PKGBUILD, source revision and SHA256SUMS. Download the package, source and SHA256SUMS from the release. CI also provides the
`LGInputSwitch-CachyOS-Arch` artifact. This is
not yet an AUR package. Build against a current Arch/CachyOS system; rolling-release
packages may require a system update before installation.

Verify checksums in the extracted directory:

```
sha256sum -c SHA256SUMS
sudo pacman -U ./lginputswitch-*.pkg.tar.zst
```

The package installs the GUI launcher, scalable icon, CLI and notices under `/usr`.
Dependencies come from your distribution. The app runs as your normal user.
It never installs a service, enables startup, changes I2C permissions or sends a
monitor command during installation. Use ddcutil's packaged permission setup:
[ddcutil I2C permissions](https://www.ddcutil.com/i2c_permissions/).

Launch **LG Input Switch** from the application menu. Select your monitor, review
the enabled input mappings and save. Switch only to inputs whose codes are known
for that monitor. A success message means the command was sent; no extra readback
is attempted after switching away from this host.

For support, use **Diagnostics → Create report**, review it, then copy or save it
with your issue. The report includes read-only checks and advertised controls/input
values for the selected monitor; it does not change settings or upload anything.

In Preferences, enable global shortcuts and save. KDE System Settings → Keyboard
→ Shortcuts → Applications → LG Input Switch contains Cycle inputs, Previous
input and each enabled input. Assignments can be edited in either place. Existing
manual commands remain separate and can conflict with new assignments; remove or
change those manually if you no longer need them.

Enable **Launch when I sign in** and save to opt into startup. The app writes only
`$XDG_CONFIG_HOME/autostart/lginputswitch.desktop` (normally
`~/.config/autostart/lginputswitch.desktop`). Startup uses `--background`: a tray
host keeps the window hidden; without a tray host it opens the window. Unchecking
startup disables that entry. The GUI preference reflects the actual entry, so
changes from Plasma Autostart are respected on next launch. Moving a development
executable requires saving startup again to update its absolute path.

## Development checkout: monitor profiles

The next update adds named profile tabs. The first run imports the existing
single-monitor settings without modifying them; confirm the connection and save.
The new workspace is stored in the `profilesV2` entry of the same Qt settings file
(normally `~/.config/LGInputSwitch/LGInputSwitchDesktop.conf`). Back up that file
before trying development builds; legacy preference keys remain available for
rollback, but edits to profiles do not update the older editor's settings.

Use + Profile and + Monitor to configure groups. New monitors can use Standard
DDC/CI (0x60), including compatible Dell models, or the LG alternate method.
The app reads each monitor’s advertised inputs during setup/startup/Refresh.
Detected changes require Save settings once; custom mappings can opt out in the
monitor editor. Missing/unusable lists retain existing values. Select every member's input
in each action row. KDE's shortcut manager lists the actions under their profile
names, and existing migrated shortcut IDs are retained. See
[profile setup and retry behavior](desktop-ui.md#monitor-profiles-development-checkout).
The published 1.2.0 package still uses the single-monitor editor.

## Build a package from source

Install build requirements with the distribution package manager: `base-devel`,
`cmake`, `python`, `desktop-file-utils`, `qt6-base`, `kglobalaccel`, `ddcutil` and
optionally `qt6-wayland` for native Wayland windows. Then, as your normal user:

```
python3 scripts/prepare-arch-package.py --output build-arch-source
cd build-arch-source
makepkg --syncdeps
sudo pacman -U ./lginputswitch-*.pkg.tar.zst
```

The exporter uses the exact committed HEAD, including its matching recipe; local
uncommitted edits are excluded. Use `--ref` to select a release commit. It emits
provenance and a checked source archive, never personal config or build products.
The source package includes the full current source; the old inherited Windows
implementation has been removed from version 1.1.0.
CI builds as an unprivileged user in a clean Arch container and runs the same tests.

## Other Linux distributions

Install Qt 6.5+ Widgets/Concurrent, ddcutil and CMake; KF6GlobalAccel is optional
outside KDE. Build and test as described in [desktop-ui.md](desktop-ui.md), then:

```
cmake --install build-desktop --prefix "$HOME/.local"
```

Ensure `~/.local/bin` is in the graphical session's PATH. Launch from your menu
(or sign out/in if its application cache has not refreshed). Generic Qt CI
artifacts are development binaries, not portable Linux packages. Native shortcut
integration requires a build with KF6GlobalAccel and a running KDE session.

## Upgrade and removal

Exit the old app before upgrading. Per-user settings and Plasma assignments are
retained; Windows JSON is not migrated. The installed launcher has the same ID
as development builds. A manually installed `~/.local/share/applications/`
`lginputswitch.desktop` takes precedence over `/usr/share/applications/`; remove
that old development launcher when moving to the package. Do not remove separate
manual shortcut launchers unless you intend to remove those commands.

To uninstall, first disable startup in Preferences and quit, then use
`sudo pacman -R lginputswitch`. Package removal preserves your settings. To
remove them explicitly, delete the app's `~/.config/LGInputSwitch/` directory;
Plasma shortcut assignments can be removed from System Settings. No uninstall
step sends monitor commands.
