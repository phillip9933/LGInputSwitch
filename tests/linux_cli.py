"""Exercise the real CLI against a fake ddcutil, never against monitor hardware."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

BINARY = str(Path(sys.argv.pop(1)).resolve())


class LinuxCliTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.log = self.root / "argv.json"
        fake = self.root / "ddcutil"
        fake.write_text(
            f"#!{sys.executable}\n"
            "import json, os, pathlib, signal, sys\n"
            "pathlib.Path(os.environ['ARGV_LOG']).write_text(json.dumps(sys.argv))\n"
            "print('backend output')\n"
            "if os.environ.get('FAKE_SIGNAL'): os.kill(os.getpid(), signal.SIGTERM)\n"
            "sys.exit(int(os.environ.get('FAKE_EXIT', '0')))\n"
        )
        fake.chmod(0o755)
        self.env = {**os.environ, "PATH": str(self.root), "ARGV_LOG": str(self.log)}

    def run_cli(self, *args, **env):
        return subprocess.run([BINARY, *args], env={**self.env, **env},
                              capture_output=True, text=True, timeout=10)

    def test_detect(self):
        result = self.run_cli("detect")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(self.log.read_text())[1:], ["detect"])
        self.assertIn("backend output", result.stdout)

    def test_explicit_switch_matches_issue_5(self):
        for code in ("0x90", "0xd0"):
            result = self.run_cli("switch", "--display", "1", "--input", code)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(self.log.read_text())[1:],
                ["--display", "1", "setvcp", "0xf4", code,
                 "--i2c-source-addr=0x50", "--noverify"])
            self.assertIn("not verified", result.stdout)

    def test_bus_zero_and_decimal_values(self):
        result = self.run_cli("switch", "--bus", "0", "--input", "144", "--source", "80")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(self.log.read_text())[1:],
            ["--bus", "0", "setvcp", "0xf4", "0x90", "--i2c-source-addr=0x50", "--noverify"])

    def test_dry_run_never_spawns_even_without_ddcutil(self):
        result = self.run_cli("switch", "--display", "2", "--input", "0xd1", "--dry-run", PATH="")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout), ["ddcutil", "--display", "2", "setvcp",
            "0xf4", "0xd1", "--i2c-source-addr=0x50", "--noverify"])
        result = self.run_cli("detect", "--dry-run")
        self.assertEqual(json.loads(result.stdout), ["ddcutil", "detect"])
        self.assertFalse(self.log.exists())

    def test_bad_arguments_never_spawn(self):
        commands = [[], ["unknown"], ["detect", "--bus", "1"],
            ["switch"], ["switch", "--input", "0x90"],
            ["switch", "--display", "0", "--input", "0x90"],
            ["switch", "--display", "-1", "--input", "0x90"],
            ["switch", "--display", "0x1", "--input", "0x90"],
            ["switch", "--display", "1", "--bus", "2", "--input", "0x90"],
            ["switch", "--display", "1", "--input", "0x90; touch BAD"],
            ["switch", "--display", "1", "--input", "0x10000"],
            ["switch", "--display", "1", "--input", "0x90", "--source", "256"],
            ["switch", "--display", "1", "--input", "0x90", "--input", "0xd0"],
            ["switch", "--display", "1", "--input", "0x90", "--source", "80", "--source", "80"],
            ["switch", "--display", "1", "--input", "0x90", "--dry-run", "--dry-run"],
            ["switch", "--display"], ["switch", "--display", "2147483648", "--input", "0x90"]]
        for command in commands:
            with self.subTest(command=command):
                result = self.run_cli(*command)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertFalse(self.log.exists())

    def test_child_failure_propagates(self):
        result = self.run_cli("switch", "--display", "1", "--input", "0x90", FAKE_EXIT="13")
        self.assertEqual(result.returncode, 13)
        self.assertIn("status 13", result.stderr)
        self.assertNotIn("Input command sent", result.stdout)

    def test_child_signal(self):
        result = self.run_cli("detect", FAKE_SIGNAL="1")
        self.assertEqual(result.returncode, 143)
        self.assertIn("terminated by a signal", result.stderr)

    def test_missing_dependency(self):
        result = self.run_cli("detect", PATH="")
        self.assertEqual(result.returncode, 127)
        self.assertIn("not found", result.stderr)
        self.assertFalse(self.log.exists())

    def test_nonexecutable_dependency(self):
        (self.root / "ddcutil").chmod(0o644)
        result = self.run_cli("detect")
        self.assertEqual(result.returncode, 126)
        self.assertIn("Could not start", result.stderr)
        self.assertFalse(self.log.exists())

    def test_help_never_spawns(self):
        result = self.run_cli("--help")
        self.assertEqual(result.returncode, 0)
        self.assertIn("experimental", result.stdout)
        self.assertFalse(self.log.exists())


if __name__ == "__main__":
    unittest.main()
