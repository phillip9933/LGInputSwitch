#!/usr/bin/env python3
"""Build metadata and embedded notices for the single-file Windows release."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tarfile
import urllib.request
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parents[1]
VERSION = '1.3.4'
QT = json.loads((ROOT / 'packaging/windows/qt-sources.json').read_text())


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def dependencies(output):
    output.mkdir(parents=True, exist_ok=True)
    for entry in QT['sources']:
        path = output / entry['name']
        if not path.exists():
            request = urllib.request.Request(entry.get('url',
                'https://download.qt.io/archive/qt/6.8/6.8.3/submodules/' + entry['name']),
                headers={'User-Agent': 'LGInputSwitch-release/' + VERSION})
            with urllib.request.urlopen(request, timeout=180) as source, path.open('wb') as target:
                shutil.copyfileobj(source, target)
        if digest(path) != entry['sha256']:
            raise RuntimeError('Qt source checksum mismatch: ' + entry['name'])
    return output


def collect_notices(source_dir):
    records = []
    for entry in QT['sources']:
        with tarfile.open(source_dir / entry['name']) as archive:
            members = {m.name: m for m in archive if m.isfile()}
            selected = set()
            for name in members:
                path = PurePosixPath(name)
                if path.is_absolute() or '..' in path.parts:
                    raise RuntimeError('Unsafe source archive entry')
                lower = path.name.lower()
                if ('license' in lower or 'copying' in lower or 'copyright' in lower
                        or lower in {'qt_attribution.json', 'notice', 'authors', 'ftl.txt'}
                        or 'LICENSES' in path.parts):
                    selected.add(name)
                if lower == 'qt_attribution.json':
                    try:
                        value = json.loads(archive.extractfile(members[name]).read())
                    except (ValueError, UnicodeError):
                        continue
                    for attribution in value if isinstance(value, list) else [value]:
                        licenses = attribution.get('LicenseFile', '')
                        for relative in licenses if isinstance(licenses, list) else [licenses]:
                            candidate = str(path.parent / relative)
                            if candidate in members:
                                selected.add(candidate)
            for name in sorted(selected):
                records.append('\n=== ' + name + ' ===\n' +
                    archive.extractfile(members[name]).read().decode('utf-8', errors='replace'))
    text = '\n'.join(records)
    if 'GNU LESSER GENERAL PUBLIC LICENSE' not in text or 'GNU GENERAL PUBLIC LICENSE' not in text:
        raise RuntimeError('Qt license texts missing')
    return text


def prepare_resources(source_dir, output):
    output.mkdir(parents=True, exist_ok=True)
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    source_info = ('LG Input Switch ' + VERSION + '\nBuild: ' + revision +
        '\nThis executable statically links Qt ' + QT['version'] + ' (LGPLv3) and the Microsoft C++ runtime.\n'
        'Qt copyright: The Qt Company Ltd. and authors named below.\n'
        'Application source and exact build scripts: https://github.com/phillip9933/LGInputSwitch/tree/' + revision + '\n'
        'Complete unmodified Qt source (the build verifies these SHA256 hashes):\n' +
        '\n'.join(e['url'] + '\nSHA256 ' + e['sha256'] for e in QT['sources']) +
        '\nRebuild using scripts/build-static-qt.py, then CMake as described in docs/windows-release.md.\n'
        'You may modify Qt or this application, rebuild/relink them and run the modified executable.\n'
        'No signature or integrity check restricts modified local builds.\n\n')
    terms = '\n\n'.join('\n=== ' + name + ' ===\n' + (ROOT / name).read_text()
        for name in ['LICENSE.md', 'COPYRIGHT.md', 'THIRD_PARTY_LICENSES.md'])
    terms += '\n\n' + '\n'.join('\n=== ' + p.name + ' ===\n' + p.read_text()
        for p in sorted((ROOT / 'licenses').glob('*.txt')))
    (output / 'THIRD-PARTY-NOTICES.txt').write_text(source_info + terms + collect_notices(source_dir), encoding='utf-8')
    (output / 'BUILD-INFO.json').write_text(json.dumps({'application': VERSION,
        'source_revision': revision, 'qt': QT, 'linkage': 'static', 'msvc_runtime': 'static'}, indent=2))


def package(executable, output):
    if output.exists():
        raise RuntimeError('Use a new release directory')
    if not executable.is_file() or executable.stat().st_size < 1024:
        raise RuntimeError('Windows executable missing or truncated')
    output.mkdir(parents=True)
    name = f'LGInputSwitch-{VERSION}-Windows-x64.exe'
    shutil.copy2(executable, output / name)
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    (output / 'SOURCE_REVISION').write_text(revision + '\n')
    (output / 'SHA256SUMS-Windows').write_text(digest(output / name) + '  ' + name + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    package(args.executable.resolve(), args.output.resolve())
