"""Portable behavior checks for the iOS CI helper; no Apple tools required."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch, Mock

spec = importlib.util.spec_from_file_location("ci_ios", Path(__file__).resolve().parents[1] / "tools/ci_ios.py")
ci = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ci)


class IosCiTests(unittest.TestCase):
    def setUp(self):
        environment = patch.dict(os.environ, {"GITHUB_STEP_SUMMARY": ""})
        environment.start()
        self.addCleanup(environment.stop)

    def test_fingerprint_invalidates_toolchain_configuration_and_paths(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "CMakeLists.txt").write_text("first")
            def capture(command):
                return "CMakeLists.txt" if command[:2] == ["git", "ls-files"] else "tool-v1"
            with patch.dict(os.environ, {"ImageVersion": "image1"}), patch.object(ci, "output", side_effect=capture):
                first = ci.fingerprint(root)
                self.assertEqual(first, ci.fingerprint(root))
                (root / "CMakeLists.txt").write_text("second")
                self.assertNotEqual(first, ci.fingerprint(root))
                (root / "CMakeLists.txt").write_text("first")
                with patch.dict(os.environ, {"ImageVersion": "image2"}):
                    self.assertNotEqual(first, ci.fingerprint(root))
                with patch.object(ci, "output", side_effect=lambda c: "CMakeLists.txt" if c[0] == "git" else "tool-v2"):
                    self.assertNotEqual(first, ci.fingerprint(root))
                moved = root / "moved"
                moved.mkdir()
                (moved / "CMakeLists.txt").write_text("first")
                self.assertNotEqual(first, ci.fingerprint(moved))

    def test_summary_records_failed_steps_and_cache_transfer(self):
        with tempfile.TemporaryDirectory() as tmp, patch.dict(os.environ, {"RUNNER_TEMP": tmp}), \
                patch.object(ci, "note") as note, patch.object(ci.sys, "argv", ["ci_ios.py", "summary"]):
            fixture = {"jobs": [{"name": "ios-arm64", "steps": [{"name": "Restore simulator configuration",
                "started_at": "2026-11-15T14:00:00Z", "completed_at": "2026-11-15T14:00:05Z",
                "conclusion": "failure"}]}]}
            (Path(tmp) / "minecraftc-ios-jobs.json").write_text(json.dumps(fixture))
            ci.main()
            note.assert_called_once_with("Restore simulator configuration", "5s (failure)")

    def test_restored_configuration_retries_once_without_stale_files(self):
        with tempfile.TemporaryDirectory() as tmp, patch.dict(os.environ,
                {"SCCACHE_PATH": "cache", "MINECRAFTC_MOLTENVK_ROOT": "molten"}):
            root = Path(tmp)
            build = root / "build-ios-simulator"
            build.mkdir()
            (build / "CMakeCache.txt").write_text("stale")
            calls = []
            def configure(args):
                calls.append(args)
                if len(calls) == 1:
                    raise subprocess.CalledProcessError(1, args)
                self.assertFalse(build.exists())
            with patch.object(ci, "run", side_effect=configure):
                ci.configure("simulator", root)
            self.assertEqual(len(calls), 2)
            with patch.object(ci, "run", side_effect=subprocess.CalledProcessError(1, "cmake")) as command:
                with self.assertRaises(subprocess.CalledProcessError):
                    ci.configure("simulator", root)
                self.assertEqual(command.call_count, 1)
            with self.assertRaises(ValueError):
                ci.configure("../outside", root)

    def test_device_selection_filters_runtime_availability_and_family(self):
        devices = {"chosen": [
            {"name": "iPhone 16", "udid": "b", "isAvailable": True},
            {"name": "iPhone 15", "udid": "a", "isAvailable": True},
            {"name": "iPhone 14", "udid": "0", "isAvailable": False},
            {"name": "iPad", "udid": "c", "isAvailable": True}]}
        self.assertEqual(ci.choose_device(devices, "chosen", "iPhone")["udid"], "a")
        self.assertIsNone(ci.choose_device(devices, "missing", "iPhone"))

    def smoke_case(self, reused, fail, contents="MinecraftC initialized\nVulkan first frame presented\n"):
        calls = []
        runtime = "com.apple.CoreSimulator.SimRuntime.iOS-18-5"
        def capture(command):
            if "runtimes" in command:
                return json.dumps({"runtimes": [{"identifier": runtime, "version": "18.5", "isAvailable": True}]})
            if "devicetypes" in command:
                return json.dumps({"devicetypes": [{"name": f, "identifier": f} for f in ("iPhone", "iPad")]})
            if "devices" in command:
                return json.dumps({"devices": {runtime: [{"name": f, "udid": f, "isAvailable": True,
                    "state": "Shutdown"} for f in ("iPhone", "iPad")] if reused else []}})
            return "created-device"
        def execute(command, **kwargs):
            calls.append(command)
            if fail and "install" in command:
                raise subprocess.CalledProcessError(1, command)
            return Mock(returncode=0)
        def launch(command, **kwargs):
            kwargs["stdout"].write(contents)
            kwargs["stdout"].flush()
            return Mock(poll=lambda: 0)
        with tempfile.TemporaryDirectory() as tmp, patch.dict(os.environ, {"RUNNER_TEMP": tmp}), \
                patch.object(ci, "output", side_effect=capture), patch.object(ci, "run", side_effect=execute), \
                patch.object(ci.subprocess, "run", side_effect=execute), \
                patch.object(ci.subprocess, "Popen", side_effect=launch):
            if fail or "MinecraftC initialized" not in contents or "Metal argument buffers" in contents or "Vulkan first frame presented" not in contents:
                with self.assertRaises((subprocess.CalledProcessError, RuntimeError)):
                    ci.smoke(Path("app"))
            else:
                ci.smoke(Path("app"))
        self.assertTrue(any("shutdown" in c for c in calls))
        self.assertEqual(any("delete" in c for c in calls), not reused)
        if not fail and "MinecraftC initialized" in contents and "Metal argument buffers" not in contents and "Vulkan first frame presented" in contents:
            self.assertEqual(sum("install" in c for c in calls), 2)

    def test_reused_devices_survive_success_and_failure(self):
        self.smoke_case(True, False)
        self.smoke_case(True, True)

    def test_created_devices_deleted_on_success_and_failure(self):
        self.smoke_case(False, False)
        self.smoke_case(False, True)

    def test_failed_launch_and_renderer_checks_still_clean_up(self):
        for reused in (True, False):
            self.smoke_case(reused, False, "launch exited\n")
            self.smoke_case(reused, False, "Vulkan first frame presented\n")
            self.smoke_case(reused, False, "MinecraftC initialized\nVulkan first frame presented\n"
                "Descriptor sets binding resources using Metal argument buffers\n")


if __name__ == "__main__":
    unittest.main()
