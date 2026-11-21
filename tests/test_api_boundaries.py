#!/usr/bin/env python3
"""Mutation checks: real dependencies fail; comments/strings and adapters pass.

The scanner is a mandatory architecture guard. Its own regressions must exercise
the CLI on source fixtures, including paths and lexical forms missed historically.
"""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCANNER = Path(__file__).resolve().parents[1] / "tools/check_api_boundaries.py"


class BoundaryChecks(unittest.TestCase):
    def scan(self, files, baseline=None):
        with tempfile.TemporaryDirectory(prefix="api-") as directory:
            root = Path(directory)
            for name, source in files.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(source, encoding="utf-8")
            (root / "baseline.json").write_text(json.dumps(baseline or {}), encoding="utf-8")
            return subprocess.run([sys.executable, str(SCANNER), "--root", str(root),
                                   "--baseline", "baseline.json"], capture_output=True, text=True)

    def test_dependency_mutations(self):
        for source in ('#include <Windows.h>', '# include /* adapter bypass */ "SDL3/SDL.h"',
                       'PFN_vkCreateInstance create;', 'VkDevice device;', 'SDL_Init(0);',
                       'glDrawArrays(0, 0, 3);', '#include <GLES3/gl3.h>',
                       '#define OS_HEADER "WINDOWS.H"\n#include OS_HEADER',
                       '#define HEADER <vulkan/vulkan.h>\n#include HEADER',
                       'SDL_\\\nInit(0);',
                       "auto ticks = 1'000; SDL_Init(0); auto other = 2'000;"):
            for suffix in (".cpp", ".cppm", ".inl", ".inc"):
                with self.subTest(source=source, suffix=suffix):
                    result = self.scan({"src/app/example" + suffix: source})
                    self.assertNotEqual(result.returncode, 0, result.stdout)
                    self.assertIn("src/app/example" + suffix, result.stderr)

    def test_prose_is_not_dependency(self):
        result = self.scan({"src/app/example.cpp": '''
// SDL_Init(0); #include <windows.h>
/* VkDevice device; glDrawArrays(0, 0, 3); */
const char* message = "SDL_Init and VkDevice";
const char* shader = R"shader(vkCreateInstance(); "SDL_Quit();")shader";
const char quote = '\\'';
'''})
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_renderer_implementation_is_adapter_owned(self):
        header = '#include "renderer/backend/vulkan/VulkanRendererInternal.h"'
        for path in ("src/app/probe.h", "tests/smoke.cpp", "tests/probe.inc"):
            with self.subTest(path=path):
                result = self.scan({"src/empty.cpp": "", path: header})
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("forbidden vulkan_internal", result.stderr)
        result = self.scan({"src/renderer/backend/vulkan/diagnostic.cpp": header,
                            "tests/smoke.cpp": '#include "renderer/backend/vulkan/VulkanDiagnostics.h"'})
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_adapter_permissions_are_exact(self):
        result = self.scan({"src/platform/sdl/SdlWindow.cpp": "VkDevice device; SDL_Init(0);",
                            "src/platform/native/os.cpp": "#include <windows.h>",
                            "src/renderer/backend/vulkan/device.inl": "PFN_vkCreateInstance create;"})
        self.assertEqual(result.returncode, 0, result.stderr)
        for path, source in (("src/platform/sdl/SdlWindow.cpp.inc", "VkDevice device;"),
                             ("src/platform/native/os.cpp", "SDL_Init(0);"),
                             ("src/renderer/backend/vulkan/device.cpp", "glDrawArrays(0,0,3);")):
            with self.subTest(path=path):
                self.assertNotEqual(self.scan({path: source}).returncode, 0)

    def test_file_wide_exception_cannot_hide_new_call(self):
        result = self.scan({"src/app/example.cpp": "SDL_Init(0); SDL_Quit();"},
                           {"sdl": ["src/app/example.cpp"]})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("baseline exceptions are forbidden", result.stderr)

    def test_diagnostics_keep_physical_lines_after_splicing(self):
        result = self.scan({"src/app/example.cpp": "#define SUM(x) \\\n (x + 1)\n\nSDL_Init(0);"})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("src/app/example.cpp:4:", result.stderr)

    def test_wrong_root_cannot_report_success(self):
        result = self.scan({})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("source directory does not exist", result.stderr)


if __name__ == "__main__":
    unittest.main()
