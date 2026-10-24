"""End-to-end package authoring, clean publication and runtime integration."""
import argparse
import importlib.util
import json
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[1]
VULKAN_SKIPPED = False
spec = importlib.util.spec_from_file_location("content_package", REPO / "tools/content_package.py")
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class ContentPackageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="minecraftc-package-tool-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def test_public_help(self):
        result = subprocess.run([sys.executable, str(REPO / "tools/content_package.py"), "--help"],
                                text=True, capture_output=True, check=True)
        self.assertIn("create", result.stdout)
        self.assertIn("build", result.stdout)
        self.assertNotIn("SUPPRESS", result.stdout)
        self.assertNotIn("metadata", result.stdout)

    def test_data_publication_and_refusals(self):
        source = self.root / "source"
        package.create(source, "author_data", "data")
        self.assertNotIn(b"\r", (source / "mod.json").read_bytes())
        self.assertTrue((source / ".gitattributes").is_file())
        (source / "code").mkdir()
        (source / "code/private.cpp").write_text("not a runtime resource")
        (source / "build").mkdir()
        (source / "build/cache.bin").write_bytes(b"cache")
        package.write_json(source / "blocks/nested/extra.json", {
            "key": "author_data:extra", "name": "Extra", "materials": "author_data:blue_brick"})
        (source / "LICENSE.md").write_text("Original example; GPL-3.0-only\n")
        output = self.root / "published/author_data"
        package.stage(source, output)
        self.assertTrue((output / "blocks/nested/extra.json").is_file())
        self.assertTrue((output / "LICENSE.md").is_file())
        self.assertFalse((output / "code").exists())
        self.assertFalse((output / "build").exists())
        self.assertFalse((output / "README.md").exists())
        before = {p.relative_to(output).as_posix(): p.read_bytes() for p in output.rglob("*") if p.is_file()}
        with self.assertRaises(ValueError):
            package.stage(source, output)
        self.assertEqual(before, {p.relative_to(output).as_posix(): p.read_bytes() for p in output.rglob("*") if p.is_file()})
        with self.assertRaises(ValueError):
            package.create(source, "author_data", "data")
        for nested in (source, source / "dist", source.parent):
            with self.assertRaises(ValueError):
                package.stage(source, nested)
        stage = self.root / "staging/author_data"
        package.stage(source, stage, builtin=True, refresh=True)
        inventory = package.read_json(stage / "resources.sha256.json")
        self.assertNotIn("code/private.cpp", inventory["sha256"])
        self.assertNotIn(".content-package-stage", inventory["sha256"])
        package.stage(source, stage, builtin=True, refresh=True)
        self.assertEqual(inventory, package.read_json(stage / "resources.sha256.json"))
        with self.assertRaises(ValueError):
            package.stage(source, output, builtin=True, refresh=True)

    def test_manifest_gate(self):
        source = self.root / "source"
        package.create(source, "author_data", "data")
        value = package.manifest(source)
        value["data"] = "content.json"
        package.write_json(source / "mod.json", value)
        with self.assertRaises(ValueError):
            package.manifest(source)
        value.pop("data")
        value["package_format"] = 2
        package.write_json(source / "mod.json", value)
        with self.assertRaises(ValueError):
            package.manifest(source)

    def test_links(self):
        source = self.root / "source"
        package.create(source, "author_data", "data")
        link = source / "blocks/linked.json"
        try:
            link.symlink_to(source / "blocks/blue_brick.json")
        except OSError:
            self.skipTest("Host does not permit creating symlinks")
        with self.assertRaises(ValueError):
            package.stage(source, self.root / "published/author_data")

    def test_native_build_and_runtime(self):
        global VULKAN_SKIPPED
        if not OPTIONS.plugin_tests:
            self.skipTest("Runtime executable not supplied")
        source = self.root / "native source"
        sdk = self.root / "installed SDK/sdk"
        shutil.copytree(REPO / "sdk", sdk)
        (sdk / "tools").mkdir()
        shutil.copyfile(REPO / "tools/content_package.py", sdk / "tools/content_package.py")
        tool = sdk / "tools/content_package.py"
        subprocess.run([sys.executable, str(tool), "create",
                        str(source), "--id", "package_demo", "--type", "native"], check=True)
        output = self.root / "published"
        subprocess.run([sys.executable, str(tool), "build",
                        str(source), "--output", str(output)], check=True)
        published = output / "package_demo"
        self.assertFalse((published / "code").exists())
        self.assertFalse((published / "CMakeLists.txt").exists())
        manifest = package.manifest(published)
        platform = {"win32": "windows", "darwin": "macos"}.get(sys.platform, "linux")
        self.assertTrue((published / manifest["native"][platform]).is_file())
        subprocess.run([OPTIONS.plugin_tests, "--content-package-tests", str(published),
                        OPTIONS.builtin_stage], check=True)
        host = self.root / "builtin host"
        host.mkdir()
        (host / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.16)\nproject(BuiltinPackageHost LANGUAGES CXX C)\n'
            f'include("{sdk.as_posix()}/cmake/ContentPackage.cmake")\n'
            f'minecraftc_add_content_package(sample ROOT "{source.as_posix()}" MODE BUILTIN)\n'
            'get_property(entries GLOBAL PROPERTY MINECRAFTC_CONTENT_ENTRIES)\n'
            'file(WRITE "${CMAKE_BINARY_DIR}/entries.txt" "${entries}")\n', encoding="utf-8")
        host_build = self.root / "builtin build"
        configure = ["cmake", "-S", str(host), "-B", str(host_build),
                     f"-DPython3_EXECUTABLE={sys.executable}"]
        subprocess.run(configure, check=True)
        first_identity = (host_build / "entries.txt").read_text(encoding="utf-8")
        code = source / "code/plugin.cpp"
        code.write_bytes(code.read_bytes().replace(b"\r\n", b"\n").replace(b"\n", b"\r\n"))
        subprocess.run(configure, check=True)
        self.assertEqual(first_identity, (host_build / "entries.txt").read_text(encoding="utf-8"),
                         "checkout line endings must not change builtin code identity")
        code.write_text(code.read_text(encoding="utf-8").replace("brick,4", "brick,5"), encoding="utf-8")
        subprocess.run(configure, check=True)
        self.assertNotEqual(first_identity, (host_build / "entries.txt").read_text(encoding="utf-8"),
                            "code-only builtin changes must change the runtime behavior identity")
        if OPTIONS.vulkan_smoke:
            result = subprocess.run([sys.executable, str(REPO / "tests/run_vulkan_test.py"),
                                     OPTIONS.vulkan_smoke, str(REPO / "assets"),
                                     "content_package", str(published)])
            if result.returncode == 77:
                VULKAN_SKIPPED = True
                self.skipTest("No Vulkan display available")
            self.assertEqual(result.returncode, 0)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--plugin-tests")
    parser.add_argument("--builtin-stage")
    parser.add_argument("--vulkan-smoke")
    OPTIONS, remaining = parser.parse_known_args()
    program = unittest.main(argv=[sys.argv[0]] + remaining, exit=False)
    sys.exit(77 if VULKAN_SKIPPED and program.result.wasSuccessful() else
             0 if program.result.wasSuccessful() else 1)
