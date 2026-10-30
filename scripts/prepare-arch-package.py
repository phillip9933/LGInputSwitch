#!/usr/bin/env python3
"""Export an exact committed revision and a checksummed local Arch recipe."""
import argparse
import gzip
import hashlib
import io
from pathlib import Path
import re
import subprocess
import tarfile


def prepare(root, output, version, ref):
    if not re.fullmatch(r"[0-9]+(?:\.[0-9]+)*", version):
        raise ValueError("Use an Arch version such as 1.3.0")
    revision = subprocess.check_output(
        ["git", "rev-parse", "--verify", ref + "^{commit}"], cwd=root, text=True
    ).strip()
    archive = subprocess.check_output(
        ["git", "archive", "--format=tar", "--prefix=LGInputSwitch/", revision], cwd=root
    )
    # Include build provenance in the corresponding source, not local config or binaries.
    source = io.BytesIO()
    with tarfile.open(fileobj=io.BytesIO(archive)) as original, tarfile.open(
        fileobj=source, mode="w"
    ) as result:
        for member in original:
            result.addfile(member, original.extractfile(member) if member.isfile() else None)
        content = (revision + "\n").encode()
        member = tarfile.TarInfo("LGInputSwitch/SOURCE_REVISION")
        member.size, member.mode, member.mtime = len(content), 0o644, 0
        result.addfile(member, io.BytesIO(content))
    compressed = gzip.compress(source.getvalue(), mtime=0)
    digest = hashlib.sha256(compressed).hexdigest()
    # Read the recipe from the SAME commit as the source, preventing mixed revisions.
    template = subprocess.check_output(
        ["git", "show", revision + ":packaging/arch/PKGBUILD.in"], cwd=root, text=True
    )
    output.mkdir(parents=True, exist_ok=False)
    (output / "lginputswitch-source.tar.gz").write_bytes(compressed)
    (output / "PKGBUILD").write_text(
        template.replace("@VERSION@", version).replace("@SHA256@", digest)
    )
    (output / "SOURCE_REVISION").write_text(revision + "\n")
    (output / "SHA256SUMS").write_text(digest + "  lginputswitch-source.tar.gz\n")
    print(f"Prepared {version} from {revision} in {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="New output directory")
    parser.add_argument("--version", default="1.3.0")
    parser.add_argument("--ref", default="HEAD", help="Committed revision to export")
    args = parser.parse_args()
    prepare(Path(__file__).resolve().parents[1], args.output.resolve(), args.version, args.ref)
