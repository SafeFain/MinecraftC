"""Portable failure injection for optional CI cache startup."""
import contextlib
import importlib.util
import io
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("ci_sccache", Path(__file__).resolve().parents[1] / "tools/ci_sccache.py")
ci = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ci)


class CacheTests(unittest.TestCase):
    def setUp(self):
        patcher = patch.dict(os.environ, {"SCCACHE_PATH": "cache directory/sccache",
            "SCCACHE_GHA_ENABLED": "true", "SCCACHE_GHA_VERSION": "windows-x86_64-build-v1"}, clear=True)
        patcher.start()
        self.addCleanup(patcher.stop)

    def result(self, code):
        return subprocess.CompletedProcess([], code, "", "ServerBusy 503 sig=private")

    def test_healthy_shared_cache_keeps_configuration_and_publishes_launcher(self):
        with tempfile.TemporaryDirectory() as tmp:
            environment = Path(tmp) / "env"
            with patch.dict(os.environ, {"GITHUB_ENV": str(environment)}), \
                    patch.object(ci.subprocess, "run", return_value=self.result(0)) as run:
                self.assertEqual(ci.start(), "enabled")
            self.assertEqual(run.call_count, 1)
            self.assertIn("MINECRAFTC_COMPILER_LAUNCHER=cache directory/sccache\n", environment.read_text())
            self.assertEqual(os.environ["SCCACHE_GHA_VERSION"], "windows-x86_64-build-v1")

    def test_503_falls_back_and_clears_version_override_in_child_and_following_steps(self):
        calls = []
        def execute(command, **kwargs):
            calls.append((command, kwargs["env"]))
            return self.result(1 if len(calls) == 1 else 0)
        log = io.StringIO()
        with tempfile.TemporaryDirectory() as tmp:
            environment = Path(tmp) / "env"
            with patch.dict(os.environ, {"GITHUB_ENV": str(environment)}), \
                    patch.object(ci.subprocess, "run", side_effect=execute), contextlib.redirect_stdout(log):
                self.assertEqual(ci.start(), "local")
                self.assertTrue(os.environ["MINECRAFTC_COMPILER_LAUNCHER"])
            self.assertIn("SCCACHE_GHA_VERSION=\n", environment.read_text())
            self.assertIn("SCCACHE_GHA_ENABLED=false\n", environment.read_text())
        self.assertEqual([call[0][1] for call in calls], ["--start-server", "--stop-server", "--start-server"])
        self.assertEqual(calls[-1][1]["SCCACHE_GHA_VERSION"], "")
        self.assertEqual(calls[-1][1]["SCCACHE_GHA_ENABLED"], "false")
        self.assertNotIn("sig=private", log.getvalue())

    def test_failed_local_storage_disables_launcher_in_subsequent_steps(self):
        with tempfile.TemporaryDirectory() as tmp:
            environment = Path(tmp) / "env"
            with patch.dict(os.environ, {"GITHUB_ENV": str(environment)}), \
                    patch.object(ci.subprocess, "run", return_value=self.result(1)):
                self.assertEqual(ci.start(), "disabled")
                self.assertEqual(os.environ["SCCACHE_PATH"], "")
            self.assertIn("MINECRAFTC_COMPILER_LAUNCHER=\n", environment.read_text())
            self.assertIn("SCCACHE_PATH=\n", environment.read_text())

    def test_start_timeout_and_stop_failure_still_allow_local_cache(self):
        with patch.object(ci.subprocess, "run", side_effect=[
                subprocess.TimeoutExpired("cache", 30), OSError("stop failed"), self.result(0)]) as run:
            self.assertEqual(ci.start(), "local")
        self.assertTrue(all(call.kwargs["timeout"] == 30 for call in run.call_args_list))

    def test_missing_executable_uses_direct_compiler(self):
        with patch.dict(os.environ, {"SCCACHE_PATH": ""}), \
                patch.object(ci.shutil, "which", return_value=None), patch.object(ci.subprocess, "run") as run:
            self.assertEqual(ci.start(), "disabled")
        run.assert_not_called()

    def test_unlaunchable_executable_does_not_block_compilation(self):
        with patch.object(ci.subprocess, "run", side_effect=OSError("not executable")):
            self.assertEqual(ci.start(), "disabled")


if __name__ == "__main__":
    unittest.main()
