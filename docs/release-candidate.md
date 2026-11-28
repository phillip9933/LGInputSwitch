# Release publication

Stable releases use numeric tags. A public beta uses a tag such as `2.0.0-beta.1` and GitHub’s pre-release flag. The beta leaves the stable Latest release in place.

The **Publish release** workflow verifies the selected version against the source and release notes. It builds and tests both platforms, checks the artifact revisions and hashes, and verifies the uploaded draft before publication. An existing public release cannot be overwritten.

The uploaded assets are the portable Windows x64 executable, the Arch/CachyOS x86_64 package and SHA256SUMS. GitHub supplies the source archives. Arch represents the beta tag as `2.0.0beta1` so the package manager orders it before the final `2.0.0`. The Windows executable embeds its notices and rebuild information.

See the [beta notes](releases/2.0.0-beta.1.md), [Windows setup](windows-release.md) and [Linux setup](linux-install.md).
