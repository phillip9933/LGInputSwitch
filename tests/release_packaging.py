#!/usr/bin/env python3
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'scripts' / (name + '.py'))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result

publisher = module('publish-release')
windows = module('windows-release')

class ReleasePackaging(unittest.TestCase):
    def test_cross_platform_revision_line_endings(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fixtures = {
                'windows': ['LGInputSwitch-1.1.0-Windows-x64.zip',
                            'LGInputSwitch-1.1.0-Windows-legacy-x64.zip',
                            'LGInputSwitch-1.1.0-source.tar.gz',
                            'qtbase-everywhere-src-6.8.3.tar.xz',
                            'qtsvg-everywhere-src-6.8.3.tar.xz'],
                'linux': ['lginputswitch-1.1.0-1-x86_64.pkg.tar.zst',
                          'lginputswitch-source.tar.gz']}
            for kind, names in fixtures.items():
                directory = root / kind; directory.mkdir()
                for name in names:
                    (directory / name).write_bytes(b'fixture')
                (directory / 'SOURCE_REVISION').write_bytes(b'fixture\r\n' if kind == 'windows' else b'fixture\n')
                checksum = 'SHA256SUMS-Windows' if kind == 'windows' else 'SHA256SUMS'
                (directory / checksum).write_text(''.join(
                    hashlib.sha256(path.read_bytes()).hexdigest() + '  ' + path.name + '\n'
                    for path in directory.iterdir()))
            calls = []
            def fake_run(*args):
                calls.append(args)
                if args[0] == 'git':
                    return 'fixture\n'
                if args[:2] == ('gh', 'api'):
                    return json.dumps({'assets': [
                        {'name': path.name, 'digest': 'sha256:' + hashlib.sha256(path.read_bytes()).hexdigest()}
                        for path in (root / 'publish').iterdir()]})
                return '{}'
            class MissingRelease:
                returncode = 1
            with patch.object(publisher, 'run', fake_run), patch.object(publisher.subprocess, 'run', return_value=MissingRelease()), patch.dict(os.environ, {'GITHUB_SHA':'fixture','GITHUB_REPOSITORY':'owner/repo'}):
                publisher.publish(root, '1.1.0')
            self.assertEqual((root / 'publish/SOURCE_REVISION').read_bytes(), b'fixture\n')
            self.assertTrue(any('--prerelease=false' in call and '--latest' in call for call in calls))

    def test_numeric_versions_match_source(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'desktop').mkdir(); (root / 'docs/releases').mkdir(parents=True)
            (root / 'CMakeLists.txt').write_text('project(LGInputSwitch VERSION 1.1.0 LANGUAGES CXX)')
            (root / 'desktop/main.cpp').write_text('app.setApplicationVersion("1.1.0");')
            (root / 'docs/releases/1.1.0.md').write_text('Notes')
            with patch.object(publisher, 'ROOT', root):
                for invalid in ['v1.1.0', '1.1.0-beta.1', '1.1', '1.1.1', '1.1.0\n']:
                    with patch.dict(os.environ, {'RELEASE_VERSION': invalid}), self.assertRaises(ValueError):
                        publisher.validate()
                with patch.dict(os.environ, {'RELEASE_VERSION': '1.1.0'}):
                    self.assertEqual(publisher.validate(), '1.1.0')

    def test_dependency_corruption_rejected_without_network(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'source.tar.xz').write_bytes(b'corrupt')
            with patch.object(windows, 'QT', {'sources':[{'name':'source.tar.xz','sha256':'0'*64}]}):
                with self.assertRaisesRegex(RuntimeError, 'checksum mismatch'):
                    windows.dependencies(root)

    def test_notice_extraction_and_archive_traversal(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with tarfile.open(root / 'source.tar.xz', 'w:xz') as archive:
                for name in ['qtbase/LICENSES/LGPL-3.0-only.txt', 'qtbase/LICENSES/GPL-3.0-only.txt',
                             'qtbase/src/3rdparty/library/FTL.TXT']:
                    value = b'fixture license'; entry = tarfile.TarInfo(name); entry.size = len(value)
                    archive.addfile(entry, io.BytesIO(value))
            with patch.object(windows, 'QT', {'sources':[{'name':'source.tar.xz'}]}):
                windows.collect_notices(root, root / 'notices')
                self.assertTrue((root / 'notices/LGPL-3.0-only.txt').exists())
                self.assertTrue((root / 'notices/qtbase/src/3rdparty/library/FTL.TXT').exists())
                with tarfile.open(root / 'source.tar.xz', 'w:xz') as archive:
                    entry = tarfile.TarInfo('../outside'); entry.size = 1
                    archive.addfile(entry, io.BytesIO(b'x'))
                with self.assertRaisesRegex(RuntimeError, 'Unsafe'):
                    windows.collect_notices(root, root / 'unsafe')

    def test_corrupt_artifact_never_reaches_release_creation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); (root / 'windows').mkdir()
            (root / 'windows/SOURCE_REVISION').write_text('fixture\n')
            (root / 'windows/app.zip').write_bytes(b'incorrect')
            (root / 'windows/SHA256SUMS-Windows').write_text('0'*64 + '  app.zip\n')
            def git_only(*args):
                self.assertEqual(args, ('git', 'rev-parse', 'HEAD'))
                return 'fixture\n'
            class MissingRelease:
                returncode = 1
            with patch.object(publisher, 'run', git_only), patch.object(publisher.subprocess, 'run', return_value=MissingRelease()), patch.dict(os.environ, {'GITHUB_SHA':'fixture','GITHUB_REPOSITORY':'owner/repo'}):
                with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
                    publisher.publish(root, '1.1.0')

if __name__ == '__main__':
    unittest.main()
