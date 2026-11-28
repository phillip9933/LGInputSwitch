# Application updates

The desktop app checks public GitHub releases once a day while running. In
**Settings → Application updates**, automatic checks can be disabled and a
manual check can be run at any time. A manual check bypasses the daily interval
and the one-version Later dismissal. Updates never download or install silently.
Preview mode and automated GUI fixtures do not make network requests.

Failed checks retry on the next hourly timer; the daily interval starts only
after a successful check. A verified download stays ready to install until you
install it, choose **Later**, or quit. Checks preserve it rather than downloading
it again.

A new compatible release produces a tray notification when notifications are
enabled. Choose **Download update** to see progress, or **Later** to dismiss that
version. Cancelling, network timeouts and GitHub rate limits leave the installed
app unchanged. After verification, Windows offers **Install and restart** and asks about
unsaved settings. On Linux, choose **Save package…**. Release notes open the exact numbered GitHub release.

## Verification and trust

Beta builds can upgrade to the stable release of the same base version. New betas are manual downloads.
Only strict, newer numeric releases from `phillip9933/LGInputSwitch` are selected;
drafts, prereleases, downgrades and ZIP-only historical Windows releases are not
automatic-install candidates. The download must have the exact platform/version
filename, uploaded state, bounded size and SHA256 digest in GitHub’s asset metadata.
The selected version’s SHA256SUMS must itself match its GitHub digest and agree
with the binary digest. The download is checked for exact length and SHA256 again
before installation. Metadata has a three-minute deadline. Downloads retain the
size limit and a 30-second inactivity timeout, allowing slow transfers to finish
while stopping stalled ones. Redirect counts are bounded.
All requests and redirects require HTTPS on fixed GitHub download hosts; TLS
certificate verification remains enabled. No GitHub credentials or telemetry are sent.

This trusts GitHub’s HTTPS service and the repository’s release permissions.
Checksums establish integrity against that metadata; they are not an independent
publisher signature. Raiun’s signed Tauri updater is a different implementation.
No signing key, Tauri runtime or installer framework is added here.

## Windows portable replacement

The app folder must be writable by the current user. A copy of the running app
acts as a temporary helper; no PowerShell/batch script or elevated process is used.
It validates the parent process identity/creation time and waits for it to exit.
It rechecks the staged bytes, file version and old executable, then renames only
the executable. The previous executable is retained as `<app.exe>.previous`.
Settings, profiles and shortcut assignments are not overwritten.

The new app confirms startup. Failure to replace/start it restores the previous
executable when possible; a started app that does not confirm within the timeout
leaves the backup available for manual recovery. Temporary helper files are
removed after restart. To recover manually, quit the app, preserve the failed
executable if desired and rename `.previous` back to the original executable name.
Installing inside a protected folder is refused; move the portable app and its
settings to a user-writable folder or update manually.

## Linux package installation

**Save package…** exports the GitHub-verified Arch/CachyOS package to a location
you choose. The app checks the exact bytes it exports. Install the saved package
with your system package manager and restart LG Input Switch. For installations
managed by a distribution repository, update through that repository as usual.

You can verify a saved download again using the release’s SHA256SUMS before
installing it. Linux rollback uses pacman’s normal package cache or a previous
package.
