#!/usr/bin/env python3
"""Publish a checked numeric full release only after the reusable build passes."""
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


def validate():
    version = os.environ['RELEASE_VERSION']
    if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', version):
        raise ValueError('Release tags must contain only a numeric major.minor.patch version')
    cmake = (ROOT / 'CMakeLists.txt').read_text()
    main = (ROOT / 'desktop/main.cpp').read_text()
    if f'VERSION {version} ' not in cmake or f'setApplicationVersion("{version}")' not in main:
        raise ValueError('Version must match CMake and the desktop application')
    if not (ROOT / f'docs/releases/{version}.md').is_file():
        raise ValueError('Release notes missing')
    return version


def publish(assets, version):
    revision = run('git', 'rev-parse', 'HEAD').strip()
    if revision != os.environ['GITHUB_SHA']:
        raise ValueError('Checkout differs from tested workflow revision')
    repository = os.environ['GITHUB_REPOSITORY']
    if find_release(repository, version) is not None:
        raise ValueError('A release with this version already exists; never overwrite published builds')
    output = assets / 'publish'
    output.mkdir()
    for kind, checksum in [('windows', 'SHA256SUMS-Windows'), ('linux', 'SHA256SUMS')]:
        directory = assets / kind
        if (directory / 'SOURCE_REVISION').read_text().strip() != revision:
            raise ValueError('Artifact source revision mismatch')
        for line in (directory / checksum).read_text().splitlines():
            expected, name = line.split(maxsplit=1)
            name = name.strip().removeprefix('./')
            if Path(name).name != name:
                raise ValueError('Unsafe checksum path')
            path = directory / name
            if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
                raise ValueError('Artifact checksum mismatch: ' + name)
        for path in directory.iterdir():
            if path.is_file() and path.name not in ['SHA256SUMS', 'SHA256SUMS-Windows', 'SOURCE_REVISION']:
                target = output / path.name
                if target.exists() and target.read_bytes() != path.read_bytes():
                    raise ValueError('Conflicting artifact name: ' + path.name)
                target.write_bytes(path.read_bytes())
    # Both platform revisions were checked above; publish one canonical LF file.
    (output / 'SOURCE_REVISION').write_bytes((revision + '\n').encode('ascii'))
    names = {p.name for p in output.iterdir()}
    required = {f'LGInputSwitch-{version}-Windows-x64.zip',
                f'LGInputSwitch-{version}-Windows-legacy-x64.zip',
                f'LGInputSwitch-{version}-source.tar.gz',
                f'lginputswitch-{version}-1-x86_64.pkg.tar.zst', 'lginputswitch-source.tar.gz',
                'qtbase-everywhere-src-6.8.3.tar.xz', 'qtsvg-everywhere-src-6.8.3.tar.xz'}
    if not required <= names:
        raise ValueError('Release assets missing: ' + str(required - names))
    hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(output.iterdir())}
    (output / 'SHA256SUMS').write_text(''.join(value + '  ' + name + '\n' for name, value in hashes.items()))
    files = sorted(p for p in output.iterdir() if p.is_file())
    run('gh', 'release', 'create', version, '--repo', repository, '--draft', '--target', revision,
        '--title', 'LG Input Switch ' + version, '--notes-file', str(ROOT / f'docs/releases/{version}.md'),
        *map(str, files))
    release = find_release(repository, version)
    if release is None or not release['draft'] or release['target_commitish'] != revision:
        raise ValueError('Uploaded draft release does not match tested revision')
    remote = {entry['name']: entry.get('digest') for entry in release['assets']}
    for path in files:
        if remote.get(path.name) != 'sha256:' + hashlib.sha256(path.read_bytes()).hexdigest():
            raise ValueError('Uploaded release asset hash mismatch: ' + path.name)
    run('gh', 'release', 'edit', version, '--repo', repository, '--draft=false', '--prerelease=false', '--latest')
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
