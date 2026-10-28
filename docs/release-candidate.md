# Release publication

The project uses numeric versions and full releases, as requested by the maintainer.
The current version is 1.2.0; see [release notes](releases/1.2.0.md),
[Windows setup](windows-release.md) and [Linux setup](linux-install.md).

The Publish full release workflow checks that the selected numeric version matches
the source, runs all Windows/Linux build and packaging jobs, verifies provenance
and SHA256 hashes, uploads application/dependency source and binaries as a draft,
checks the uploaded hashes and then publishes it as the latest full release. It
will not overwrite an existing numeric release. Physical hardware limitations are
recorded in release notes; they are not represented as verified by CI.

The previous Linux beta was an earlier publication and is superseded by 1.2.0.
Future builds do not use alpha/beta/RC version suffixes or prerelease labels.

Intel/NVIDIA backends and macOS remain roadmap work; Windows reports #4/#5 remain
unconfirmed. No new GPU/monitor support is inferred from a successful CI run.
