#!/usr/bin/env python3
"""Assemble Windows release archives, exact source, notices and provenance."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = '1.1.0'
QT = json.loads((ROOT / 'packaging/windows/qt-sources.json').read_text())


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def dependencies(output):
    output.mkdir(parents=True, exist_ok=True)
    for entry in QT['sources']:
        path = output / entry['name']
        if not path.exists():
            url = 'https://download.qt.io/archive/qt/6.8/6.8.3/submodules/' + entry['name']
            request = urllib.request.Request(url, headers={'User-Agent': 'LGInputSwitch-release/1.1.0'})
            with urllib.request.urlopen(request, timeout=180) as source, path.open('wb') as target:
                shutil.copyfileobj(source, target)
        if digest(path) != entry['sha256']:
            raise RuntimeError('Qt source checksum mismatch: ' + entry['name'])
    return output


def collect_notices(source_dir, destination):
    """Keep exact upstream texts/attribution records alongside their source paths."""
    for entry in QT['sources']:
        with tarfile.open(source_dir / entry['name']) as archive:
            for member in archive:
                if not member.isfile():
                    continue
                path = Path(member.name)
                if path.is_absolute() or '..' in path.parts:
                    raise RuntimeError('Unsafe source archive entry')
                lower = path.name.lower()
                if ('license' in lower or 'copying' in lower or lower == 'qt_attribution.json'
                        or 'LICENSES' in path.parts or '3rdparty' in path.parts):
                    target = destination / path
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(archive.extractfile(member).read())
    for name in ['LGPL-3.0-only.txt', 'GPL-3.0-only.txt']:
        matches = list(destination.rglob(name))
        if not matches:
            raise RuntimeError('Required Qt license missing: ' + name)
        shutil.copy2(matches[0], destination / name)


def project_notices(destination):
    destination.mkdir(parents=True, exist_ok=True)
    for name in ['LICENSE.md', 'COPYRIGHT.md', 'THIRD_PARTY_LICENSES.md']:
        shutil.copy2(ROOT / name, destination / name)
    shutil.copytree(ROOT / 'licenses', destination / 'licenses', dirs_exist_ok=True)
    shutil.copy2(ROOT / 'docs/windows-release.md', destination / 'README.md')


def manifest(directory, revision):
    (directory / 'SOURCE_REVISION').write_text(revision + '\n')
    entries = {str(p.relative_to(directory)).replace('\\', '/'): digest(p)
               for p in sorted(directory.rglob('*')) if p.is_file()}
    (directory / 'MANIFEST.json').write_text(json.dumps(entries, indent=2) + '\n')


def zip_directory(directory, target):
    with zipfile.ZipFile(target, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(directory.rglob('*')):
            if path.is_file():
                archive.write(path, path.relative_to(directory))


def package(desktop, legacy, qt_prefix, output, source_dir, redist):
    if output.exists():
        raise RuntimeError('Use a new release directory')
    output.mkdir(parents=True)
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    desktop_out = output / 'desktop'
    legacy_out = output / 'legacy'
    shutil.copytree(desktop, desktop_out)
    # These optional graphics runtimes are deliberately not deployed by the workflow.
    for forbidden in ['opengl32sw.dll', 'D3Dcompiler_47.dll']:
        if (desktop_out / forbidden).exists():
            raise RuntimeError('Unexpected optional runtime: ' + forbidden)
    for folder in [desktop_out, legacy_out]:
        project_notices(folder)
    shutil.copy2(legacy, legacy_out / 'LGInputSwitch.exe')
    installer = desktop_out / 'runtime/VC_redist.x64.exe'
    installer.parent.mkdir()
    shutil.copy2(redist, installer)
    collect_notices(source_dir, desktop_out / 'qt-notices')
    source_names = [entry['name'] for entry in QT['sources']]
    (desktop_out / 'QT-SOURCE.md').write_text(
        'This app dynamically links Qt ' + QT['version'] + ' under LGPLv3.\n'
        'Copyright The Qt Company Ltd. and the other authors identified in qt-notices/.\n'
        'Complete unmodified module source archives are published beside this ZIP:\n' +
        '\n'.join('- ' + name for name in source_names) + '\n\n'
        'You may replace these DLLs with interface-compatible modified builds and debug/relink them.\n'
        'No signature or integrity check prevents replacing the Qt libraries.\n'
        'See README.md for source-build and replacement instructions.\n')
    (desktop_out / 'runtime/README.txt').write_text(
        'Microsoft Visual C++ Redistributable (x64), unmodified official installer.\n'
        'Run VC_redist.x64.exe if the app reports missing MSVCP/VCRUNTIME DLLs.\n'
        'The installer presents Microsoft license terms; they apply separately from AGPL.\n'
        'Source: https://aka.ms/vs/17/release/vc_redist.x64.exe\n'
        'Redistribution: https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution\n')
    build_info = {'application': VERSION, 'source_revision': revision, 'qt': QT,
                  'compiler_runtime_installer_sha256': digest(installer)}
    # Qt SDK configuration helps rebuild interface-compatible libraries.
    config_out = desktop_out / 'qt-build-config'
    for relative in ['mkspecs/qconfig.pri', 'mkspecs/qmodule.pri']:
        path = qt_prefix / relative
        if path.exists():
            config_out.mkdir(exist_ok=True)
            shutil.copy2(path, config_out / path.name)
    (desktop_out / 'BUILD-INFO.json').write_text(json.dumps(build_info, indent=2) + '\n')
    for folder in [desktop_out, legacy_out]:
        manifest(folder, revision)
    zip_directory(desktop_out, output / f'LGInputSwitch-{VERSION}-Windows-x64.zip')
    zip_directory(legacy_out, output / f'LGInputSwitch-{VERSION}-Windows-legacy-x64.zip')
    subprocess.run(['git', 'archive', '--format=tar.gz', '--prefix=LGInputSwitch/',
                    '--output=' + str(output / f'LGInputSwitch-{VERSION}-source.tar.gz'), revision],
                   cwd=ROOT, check=True)
    for name in source_names:
        shutil.copy2(source_dir / name, output / name)
    (output / 'SOURCE_REVISION').write_text(revision + '\n')
    hashes = ''.join(digest(p) + '  ' + p.name + '\n' for p in sorted(output.iterdir()) if p.is_file())
    (output / 'SHA256SUMS-Windows').write_text(hashes)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--download-only', action='store_true')
    parser.add_argument('--source-dir', type=Path, required=True)
    parser.add_argument('--desktop', type=Path)
    parser.add_argument('--legacy', type=Path)
    parser.add_argument('--qt-prefix', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--redist', type=Path)
    args = parser.parse_args()
    source_dir = dependencies(args.source_dir.resolve())
    if not args.download_only:
        if any(value is None for value in [args.desktop, args.legacy, args.qt_prefix, args.output, args.redist]):
            parser.error('Packaging requires desktop, legacy, qt-prefix, output and redist paths')
        package(args.desktop.resolve(), args.legacy.resolve(), args.qt_prefix.resolve(),
                args.output.resolve(), source_dir, args.redist.resolve())
