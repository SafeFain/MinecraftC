#!/usr/bin/env python3
"""Regression checks for checked-in shader provenance, independent of glslc."""
import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("vulkan_shaders", ROOT / "tools/vulkan_shaders.py")
shaders = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shaders)


class ShaderAssetsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="minecraftc-shader-test-")
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        for name in shaders.SHADERS + shaders.INCLUDES + (shaders.SOURCE_MANIFEST,):
            shutil.copyfile(ROOT / "assets/shaders/vulkan" / name, self.directory / name)
        for name in shaders.SHADERS:
            shutil.copyfile(ROOT / "assets/shaders/vulkan" / (name + ".spv"),
                            self.directory / (name + ".spv"))

    def test_source_and_include_changes_are_detected(self):
        shaders.validate_source_manifest(self.directory)
        include = self.directory / shaders.INCLUDES[0]
        include.write_bytes(include.read_bytes() + b"\n// changed shared effect\n")
        with self.assertRaises(RuntimeError):
            shaders.validate_source_manifest(self.directory)

    def test_valid_magic_does_not_hide_changed_binary(self):
        binary = self.directory / (shaders.SHADERS[0] + ".spv")
        data = bytearray(binary.read_bytes())
        data[-1] ^= 1
        binary.write_bytes(data)
        shaders.validate(self.directory)  # passes the old magic/size-only check
        with self.assertRaises(RuntimeError):
            shaders.validate_source_manifest(self.directory)

    def test_windows_line_endings_preserve_source_identity(self):
        for checkout_newline in (b"\n", b"\r\n"):
            with self.subTest(checkout_newline=checkout_newline):
                for name in shaders.SHADERS + shaders.INCLUDES:
                    source = self.directory / name
                    normalized = source.read_bytes().replace(b"\r\n", b"\n")
                    source.write_bytes(normalized.replace(b"\n", checkout_newline))
                shaders.validate_source_manifest(self.directory)
                for name in shaders.SHADERS + shaders.INCLUDES:
                    source = self.directory / name
                    # Normalize first so an existing CRLF checkout stays CRLF.
                    normalized = source.read_bytes().replace(b"\r\n", b"\n")
                    source.write_bytes(normalized.replace(b"\n", b"\r\n"))
                shaders.validate_source_manifest(self.directory)

    @unittest.skipUnless(shutil.which("glslc"), "glslc is unavailable")
    def test_rehashed_source_with_stale_binary_is_rejected(self):
        source = self.directory / "basic_cube.frag"
        # A manifest alone is no proof of the source/binary relationship. With
        # the recorded compiler, regeneration must also match the actual module.
        # Use a guaranteed semantic change instead of relying on source formatting.
        source.write_text("#version 450\nlayout(location=0) out vec4 outColor;\n"
                          "void main(){outColor=vec4(0.123);}\n", encoding="utf-8")
        manifest = shaders.expected_source_manifest(self.directory)
        manifest["compiler"] = shaders.compiler_version(shutil.which("glslc"))
        (self.directory / shaders.SOURCE_MANIFEST).write_text(json.dumps(manifest),
                                                             encoding="utf-8")
        shaders.validate_source_manifest(self.directory)
        generated = self.directory / "new.spv"
        shaders.compile_shader(shutil.which("glslc"), source, generated)
        with self.assertRaises(RuntimeError):
            shaders.validate_compiled_shader(generated,
                self.directory / "basic_cube.frag.spv", matching_compiler=True)


if __name__ == "__main__":
    unittest.main()
