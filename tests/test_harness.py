#!/usr/bin/env python3
"""Guard test isolation/diagnostics and reject missing material overrides."""
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from readback_checks import compare_natural

RUNNER = Path(__file__).with_name("run_test.py")


class IsolationTests(unittest.TestCase):
    def test_parallel_runs_ignore_unwritable_parent_temp_and_cleanup(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "runs"
            environment = {**os.environ, "TEMP": str(Path(directory) / "missing/invalid"),
                           "TMP": str(Path(directory) / "missing/invalid")}
            child = ("import os,tempfile; from pathlib import Path; "
                     "p=Path(tempfile.gettempdir()); "
                     "assert str(p)==os.environ['TEMP']==os.environ['TMP']==os.environ['TMPDIR']; "
                     "(p/'same-fixture').write_text('data'); print(p)")
            def launch(_):
                return subprocess.run([sys.executable, str(RUNNER), "--name", "isolation",
                                       "--root", str(root), "--", sys.executable, "-c", child],
                                      env=environment, capture_output=True, text=True)
            with ThreadPoolExecutor(max_workers=2) as pool:
                results = list(pool.map(launch, range(2)))
            for result in results:
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertFalse(Path(result.stdout.strip()).exists())
            self.assertNotEqual(results[0].stdout, results[1].stdout)

    def test_failures_report_name_status_and_retained_files(self):
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run([sys.executable, str(RUNNER), "--name", "broken-test",
                                     "--root", directory, "--", sys.executable, "-c", "exit(9)"],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("FAILED [broken-test]: exit 9", result.stderr)
            self.assertTrue(Path(result.stderr.split("files: ")[1].strip()).is_dir())
            result = subprocess.run([sys.executable, str(RUNNER), "--name", "missing-binary",
                                     "--root", directory, "--", str(Path(directory) / "absent")],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("INFRASTRUCTURE [missing-binary]", result.stderr)


class MaterialReadbackTests(unittest.TestCase):
    def test_each_consumer_must_change_against_default(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            def write(prefix, name, color):
                (root / f"{prefix}-{name}.ppm").write_bytes(b"P6\n20 20\n255\n" + bytes(color) * 400)
            for prefix in ("natural", "default"):
                write(prefix, "background", (0, 0, 0))
                for name in ("opaque", "translucent", "dropped", "held", "ui"):
                    write(prefix, name, (255, 0, 0) if prefix == "default" else (0, 255, 0))
            compare_natural(root)
            for name in ("opaque", "translucent", "dropped", "held", "ui"):
                with self.subTest(consumer=name):
                    write("natural", name, (255, 0, 0))
                    with self.assertRaisesRegex(ValueError, name + ": Natural override missing"):
                        compare_natural(root)
                    write("natural", name, (0, 255, 0))
            write("natural", "background", (0, 0, 255))
            with self.assertRaisesRegex(ValueError, "control background changed"):
                compare_natural(root)


if __name__ == "__main__":
    unittest.main()
