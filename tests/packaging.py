#!/usr/bin/env python3
"""Hardware-free provenance and recipe export regression."""
import hashlib
import importlib.util
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest

MODULE = Path(__file__).resolve().parents[1] / 'scripts/prepare-arch-package.py'
spec = importlib.util.spec_from_file_location('arch_export', MODULE)
export = importlib.util.module_from_spec(spec)
spec.loader.exec_module(export)


class Packaging(unittest.TestCase):
    def test_exact_committed_source_and_checksum(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(['git', 'init', '-q', str(root)], check=True)
            (root / 'packaging/arch').mkdir(parents=True)
            (root / 'packaging/arch/PKGBUILD.in').write_text('@VERSION@ @SHA256@')
            (root / 'tracked').write_text('committed')
            (root / 'amdddc').mkdir()
            (root / 'amdddc/inherited.cpp').write_text('excluded Windows source')
            subprocess.run(['git', '-C', str(root), 'add', '.'], check=True)
            subprocess.run(['git', '-C', str(root), '-c', 'user.name=Test',
                            '-c', 'user.email=test@example.invalid', 'commit', '-qm', 'fixture'], check=True)
            revision = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
            (root / 'tracked').write_text('uncommitted')
            (root / 'private-config').write_text('must not ship')
            output = root / 'out'
            export.prepare(root, output, '1.1.0beta1', 'HEAD')
            archive = output / 'lginputswitch-source.tar.gz'
            digest = hashlib.sha256(archive.read_bytes()).hexdigest()
            self.assertEqual((output / 'PKGBUILD').read_text(), '1.1.0beta1 ' + digest)
            with tarfile.open(archive) as source:
                self.assertEqual(source.extractfile('LGInputSwitch/tracked').read(), b'committed')
                self.assertEqual(source.extractfile('LGInputSwitch/SOURCE_REVISION').read(),
                                 (revision + '\n').encode())
                self.assertNotIn('LGInputSwitch/private-config', source.getnames())
                self.assertNotIn('LGInputSwitch/amdddc/inherited.cpp', source.getnames())
            with self.assertRaises(FileExistsError):
                export.prepare(root, output, '1.1.0beta1', 'HEAD')
            with self.assertRaises(ValueError):
                export.prepare(root, root / 'invalid', '1;bad', 'HEAD')


if __name__ == '__main__':
    unittest.main()
