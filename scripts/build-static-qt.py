#!/usr/bin/env python3
"""Build the pinned static Qt SDK and embed distribution notices; no private keys."""
import argparse
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('windows_release', ROOT / 'scripts/windows-release.py')
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


def build(work, prefix, notices, parallel):
    sources = release.dependencies(work / 'downloads')
    release.prepare_resources(sources, notices)
    stamp = prefix / 'lginputswitch-static-qt.json'
    expected = release.QT
    if stamp.is_file() and json.loads(stamp.read_text()) == expected:
        print('Using verified static Qt SDK cache')
        return
    if os.name != 'nt':
        raise RuntimeError('Run this build from a Visual Studio x64 developer environment')
    source = work / 'source'
    source.mkdir(parents=True, exist_ok=False)
    entry = release.QT['sources'][0]
    with tarfile.open(sources / entry['name']) as archive:
        for member in archive:
            name = PurePosixPath(member.name)
            if name.is_absolute() or '..' in name.parts or not (member.isfile() or member.isdir()):
                raise RuntimeError('Unsafe Qt source archive entry')
            target = source.joinpath(*name.parts)
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(archive.extractfile(member).read())
    qt_source = next(source.iterdir())
    binary = work / 'build'
    binary.mkdir()
    arguments = [str(qt_source / 'configure.bat'), '-prefix', str(prefix), *release.QT['configure_args']]
    subprocess.run(subprocess.list2cmdline(arguments), cwd=binary, shell=True, check=True)
    subprocess.run(['cmake', '--build', '.', '--parallel', str(parallel)], cwd=binary, check=True)
    subprocess.run(['cmake', '--install', '.'], cwd=binary, check=True)
    stamp.write_text(json.dumps(expected, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, default=Path('build-static-qt'))
    parser.add_argument('--prefix', type=Path, default=Path('build-static-qt/install'))
    parser.add_argument('--notices', type=Path, default=Path('build-distribution'))
    parser.add_argument('--parallel', type=int, default=2)
    args = parser.parse_args()
    build(args.work.resolve(), args.prefix.resolve(), args.notices.resolve(), args.parallel)
