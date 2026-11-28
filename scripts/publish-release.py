#!/usr/bin/env python3
"""Publish a checked stable or beta release only after the reusable build passes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def run(*args):
    return subprocess.check_output(args, cwd=ROOT, text=True)


def find_release(repository, version):
    # Draft releases do not resolve through the public /releases/tags endpoint.
    pages = json.loads(run('gh', 'api', '--paginate', '--slurp', f'repos/{repository}/releases'))
    return next((release for page in pages for release in page
                 if release['tag_name'] == version), None)


def ensure_release_tag(repository, version, revision, create=False):
    """Create the tag without force, or verify an existing tag's peeled commit."""
    remote = f'https://github.com/{repository}.git'
    ref = f'refs/tags/{version}'
    entries = dict(line.split('\t', 1)[::-1] for line in
                   run('git', 'ls-remote', '--tags', remote, ref, ref + '^{}').splitlines())
    if ref in entries:
        if entries.get(ref + '^{}', entries[ref]) != revision:
            raise ValueError('Existing release tag points to a different revision')
        return
    if not create:
        raise ValueError('Release tag is missing')
    run('gh', 'api', '--method', 'POST', f'repos/{repository}/git/refs',
        '-f', f'ref={ref}', '-f', f'sha={revision}')


def validate():
    version = os.environ['RELEASE_VERSION']
    match = re.fullmatch(r'((?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*))(?:-beta\.[1-9][0-9]*)?', version)
    if not match:
        raise ValueError('Expected major.minor.patch with an optional -beta.N suffix')
    cmake = (ROOT / 'CMakeLists.txt').read_text()
    main = (ROOT / 'desktop/main.cpp').read_text()
    if f'VERSION {match.group(1)} ' not in cmake or f'setApplicationVersion("{version}")' not in main:
        raise ValueError('Version must match CMake and the desktop application')
    if not (ROOT / f'docs/releases/{version}.md').is_file():
        raise ValueError('Release notes missing')
    return version


def missing_draft_assets(release, revision, expected):
    if not release or not release['draft'] or release['target_commitish'] != revision:
        raise ValueError('Only a draft targeting the tested revision can be resumed')
    remaining = dict(expected)
    for asset in release['assets']:
        name = asset['name']
        if name not in remaining or asset.get('digest') != remaining.pop(name):
            raise ValueError('Unexpected, duplicate, or mismatched draft asset: ' + name)
    return sorted(remaining)


def publish(assets, version):
    revision = run('git', 'rev-parse', 'HEAD').strip()
    if revision != os.environ['GITHUB_SHA']:
        raise ValueError('Checkout differs from tested workflow revision')
    repository = os.environ['GITHUB_REPOSITORY']
    release = find_release(repository, version)
    if release and not release['draft']:
        raise ValueError('A release with this version already exists; never overwrite published builds')
    output = assets / 'publish'
    output.mkdir(exist_ok=True)
    arch_version = version.replace('-beta.', 'beta')
    selected = {'windows': f'LGInputSwitch-{version}-Windows-x64.exe',
                'linux': f'lginputswitch-{arch_version}-1-x86_64.pkg.tar.zst'}
    for kind, checksum in [('windows', 'SHA256SUMS-Windows'), ('linux', 'SHA256SUMS')]:
        directory = assets / kind
        if (directory / 'SOURCE_REVISION').read_text().strip() != revision:
            raise ValueError('Artifact source revision mismatch')
        verified = set()
        for line in (directory / checksum).read_text().splitlines():
            expected, name = line.split(maxsplit=1)
            name = name.strip().removeprefix('./')
            if Path(name).name != name:
                raise ValueError('Unsafe checksum path')
            path = directory / name
            if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
                raise ValueError('Artifact checksum mismatch: ' + name)
            verified.add(name)
        name = selected[kind]
        if name not in verified:
            raise ValueError('Release asset missing or unchecked: ' + name)
        (output / name).write_bytes((directory / name).read_bytes())
    hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(output / name for name in selected.values())}
    (output / 'SHA256SUMS').write_text(''.join(value + '  ' + name + '\n' for name, value in hashes.items()))
    expected = {name: 'sha256:' + digest for name, digest in hashes.items()}
    manifest = output / 'SHA256SUMS'
    expected[manifest.name] = 'sha256:' + hashlib.sha256(manifest.read_bytes()).hexdigest()
    ensure_release_tag(repository, version, revision, create=release is None)
    if release is None:
        run('gh', 'release', 'create', version, '--repo', repository, '--draft', '--verify-tag',
            '--target', revision, '--title', 'LG Input Switch ' + version,
            '--notes-file', str(ROOT / f'docs/releases/{version}.md'),
            *(str(output / name) for name in sorted(expected)))
    else:
        missing = missing_draft_assets(release, revision, expected)
        if missing:
            run('gh', 'release', 'upload', version, '--repo', repository,
                *(str(output / name) for name in missing))
    if missing_draft_assets(find_release(repository, version), revision, expected):
        raise ValueError('Draft is missing release assets')
    ensure_release_tag(repository, version, revision)
    run('gh', 'release', 'edit', version, '--repo', repository, '--draft=false',
        *(('--prerelease', '--latest=false') if '-beta.' in version else ('--prerelease=false', '--latest')))
    print(run('gh', 'release', 'view', version, '--repo', repository, '--json', 'url'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--validate-only', action='store_true')
    parser.add_argument('--assets', type=Path)
    args = parser.parse_args()
    selected = validate()
    if not args.validate_only:
        if not args.assets:
            parser.error('--assets required')
        publish(args.assets.resolve(), selected)
