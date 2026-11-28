#!/usr/bin/env python3
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import re
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
    def test_release_workflow_dependency_pins(self):
        for filename in ('build.yml', 'release.yml'):
            workflow = (ROOT / '.github/workflows' / filename).read_text()
            for kind, ref in re.findall(r'^\s*(?:-\s+)?(uses|container):\s*([^\s#]+)', workflow, re.M):
                ref = ref.strip('"\'')
                with self.subTest(workflow=filename, ref=ref):
                    if kind == 'container':
                        self.assertRegex(ref, r'@sha256:[0-9a-f]{64}$')
                    elif not ref.startswith('./'):
                        self.assertRegex(ref, r'@[0-9a-f]{40}$')

    def test_cross_platform_revision_line_endings(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fixtures = {
                'windows': ['LGInputSwitch-1.1.0-Windows-x64.exe',
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
            created = False
            def fake_run(*args):
                nonlocal created
                calls.append(args)
                if args[:2] == ('git', 'ls-remote'):
                    return 'fixture\trefs/tags/1.1.0\n'
                if args[0] == 'git':
                    return 'fixture\n'
                if args[:2] == ('gh', 'api'):
                    self.assertNotIn('/tags/', args[-1])
                    if not created:
                        return '[[]]'
                    return json.dumps([[{'tag_name':'1.1.0', 'draft':True, 'target_commitish':'fixture', 'assets': [
                        {'name': path.name, 'digest': 'sha256:' + hashlib.sha256(path.read_bytes()).hexdigest()}
                        for path in (root / 'publish').iterdir()]}]])
                if args[:3] == ('gh', 'release', 'create'):
                    created = True
                return '{}'
            class MissingRelease:
                returncode = 1
            with patch.object(publisher, 'run', fake_run), patch.object(publisher.subprocess, 'run', return_value=MissingRelease()), patch.dict(os.environ, {'GITHUB_SHA':'fixture','GITHUB_REPOSITORY':'owner/repo'}):
                publisher.publish(root, '1.1.0')
            self.assertEqual({p.name for p in (root / 'publish').iterdir()},
                             {'LGInputSwitch-1.1.0-Windows-x64.exe',
                              'lginputswitch-1.1.0-1-x86_64.pkg.tar.zst', 'SHA256SUMS'})
            self.assertEqual(len((root / 'publish/SHA256SUMS').read_text().splitlines()), 2)
            self.assertTrue(any('--prerelease=false' in call and '--latest' in call for call in calls))
            self.assertTrue(any('--verify-tag' in call for call in calls))
            # Retry a partial upload in the same working directory and forbid clobber.
            release = {'tag_name': '1.1.0', 'draft': True, 'target_commitish': 'fixture',
                       'assets': [{'name': 'SHA256SUMS', 'digest': 'sha256:' + hashlib.sha256(
                           (root / 'publish/SHA256SUMS').read_bytes()).hexdigest()}]}
            def resume(*args):
                if args[:2] == ('gh', 'api'):
                    return json.dumps([[release]])
                if args[:3] == ('gh', 'release', 'upload'):
                    self.assertEqual({Path(a).name for a in args[6:]}, {fixtures['windows'][0], fixtures['linux'][0]})
                    for file in map(Path, args[6:]):
                        release['assets'].append({'name': file.name,
                            'digest': 'sha256:' + hashlib.sha256(file.read_bytes()).hexdigest()})
                    return ''
                self.assertNotEqual(args[:3], ('gh', 'release', 'create'))
                return fake_run(*args)
            with patch.object(publisher, 'run', resume), patch.dict(os.environ,
                    GITHUB_SHA='fixture', GITHUB_REPOSITORY='owner/repo'):
                publisher.publish(root, '1.1.0')
                release['assets'].append({'name': 'extra', 'digest': 'sha256:unknown'})
                with self.assertRaisesRegex(ValueError, 'draft asset'):
                    publisher.publish(root, '1.1.0')
                release['assets'].pop()
                release['assets'][0]['digest'] = 'changed'
                with self.assertRaisesRegex(ValueError, 'draft asset'):
                    publisher.publish(root, '1.1.0')
                release['target_commitish'] = 'different'
                with self.assertRaisesRegex(ValueError, 'tested revision'):
                    publisher.publish(root, '1.1.0')
                release['draft'] = False
                with self.assertRaisesRegex(ValueError, 'published builds'):
                    publisher.publish(root, '1.1.0')


    def test_release_tag_must_point_to_tested_commit(self):
        refs = 'tag-object\trefs/tags/1.1.0\ncommit\trefs/tags/1.1.0^{}\n'
        with patch.object(publisher, 'run', return_value=refs) as mocked:
            publisher.ensure_release_tag('owner/repo', '1.1.0', 'commit')
            with self.assertRaisesRegex(ValueError, 'different revision'):
                publisher.ensure_release_tag('owner/repo', '1.1.0', 'other')
            self.assertTrue(all(call.args[:2] == ('git', 'ls-remote') for call in mocked.call_args_list))
        with patch.object(publisher, 'run', return_value='') as mocked:
            with self.assertRaisesRegex(ValueError, 'tag is missing'):
                publisher.ensure_release_tag('owner/repo', '1.1.0', 'commit')
            mocked.assert_called_once()
            publisher.ensure_release_tag('owner/repo', '1.1.0', 'commit', create=True)
            self.assertEqual(mocked.call_args.args[:4], ('gh', 'api', '--method', 'POST'))

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
                (root / 'desktop/main.cpp').write_text('app.setApplicationVersion("1.1.0-beta.1");')
                (root / 'docs/releases/1.1.0-beta.1.md').write_text('Beta notes')
                with patch.dict(os.environ, {'RELEASE_VERSION': '1.1.0-beta.1'}):
                    self.assertEqual(publisher.validate(), '1.1.0-beta.1')

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
                    value = b'GNU LESSER GENERAL PUBLIC LICENSE GNU GENERAL PUBLIC LICENSE'; entry = tarfile.TarInfo(name); entry.size = len(value)
                    archive.addfile(entry, io.BytesIO(value))
            with patch.object(windows, 'QT', {'sources':[{'name':'source.tar.xz'}]}):
                notices = windows.collect_notices(root)
                self.assertIn('LGPL-3.0-only.txt', notices)
                self.assertIn('qtbase/src/3rdparty/library/FTL.TXT', notices)
                with tarfile.open(root / 'source.tar.xz', 'w:xz') as archive:
                    entry = tarfile.TarInfo('../outside'); entry.size = 1
                    archive.addfile(entry, io.BytesIO(b'x'))
                with self.assertRaisesRegex(RuntimeError, 'Unsafe'):
                    windows.collect_notices(root)

    def test_portable_package_contains_only_executable_and_internal_metadata(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            executable = root / 'app.exe'; executable.write_bytes(b'MZ' + b'x' * 2048)
            output = root / 'release'
            with patch.object(windows.subprocess, 'check_output', return_value='fixture\n'):
                windows.package(executable, output)
            expected = f'LGInputSwitch-{windows.VERSION}-Windows-x64.exe'
            self.assertEqual({p.name for p in output.iterdir()}, {expected, 'SOURCE_REVISION', 'SHA256SUMS-Windows'})
            self.assertEqual((output / expected).read_bytes(), executable.read_bytes())
            self.assertEqual((output / 'SHA256SUMS-Windows').read_text(),
                hashlib.sha256(executable.read_bytes()).hexdigest() + '  ' + expected + '\n')

    def test_unchecked_release_executable_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); folder = root / 'windows'; folder.mkdir()
            (folder / 'SOURCE_REVISION').write_text('fixture\n')
            (folder / 'LGInputSwitch-1.1.0-Windows-x64.exe').write_bytes(b'unchecked')
            (folder / 'SHA256SUMS-Windows').write_text('')
            def fake_run(*args):
                if args[0] == 'git': return 'fixture\n'
                if args[:2] == ('gh', 'api'): return '[[]]'
                self.fail('Unchecked asset reached publication')
            with patch.object(publisher, 'run', fake_run), patch.dict(os.environ,
                    {'GITHUB_SHA':'fixture', 'GITHUB_REPOSITORY':'owner/repo'}):
                with self.assertRaisesRegex(ValueError, 'missing or unchecked'):
                    publisher.publish(root, '1.1.0')

    def test_corrupt_artifact_never_reaches_release_creation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); (root / 'windows').mkdir()
            (root / 'windows/SOURCE_REVISION').write_text('fixture\n')
            (root / 'windows/app.zip').write_bytes(b'incorrect')
            (root / 'windows/SHA256SUMS-Windows').write_text('0'*64 + '  app.zip\n')
            def git_only(*args):
                if args[:2] == ('gh', 'api'):
                    return '[[]]'
                self.assertEqual(args, ('git', 'rev-parse', 'HEAD'))
                return 'fixture\n'
            class MissingRelease:
                returncode = 1
            with patch.object(publisher, 'run', git_only), patch.object(publisher.subprocess, 'run', return_value=MissingRelease()), patch.dict(os.environ, {'GITHUB_SHA':'fixture','GITHUB_REPOSITORY':'owner/repo'}):
                with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
                    publisher.publish(root, '1.1.0')

if __name__ == '__main__':
    unittest.main()
