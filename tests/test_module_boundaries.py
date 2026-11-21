#!/usr/bin/env python3
"""Exercise the module boundary CLI with legal fixtures and injected violations."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


CHECKER = Path(__file__).resolve().parents[1] / "tools/check_module_boundaries.py"
CONTRACT = "src/renderer/UIRenderBackend.h"
FACTORY = "src/renderer/backend/vulkan/VulkanUIRenderer.h"
UI_SOURCE = "src/ui/UIRenderer.cpp"
UI_SMOKE = "tests/VulkanUiSmoke.cpp"
FILES = {
    CONTRACT: "#pragma once\n",
    FACTORY: '#include "renderer/UIRenderBackend.h"\n',
    UI_SOURCE: '#include "renderer/UIRenderBackend.h"\n',
    "src/app/Application.cpp": '#include "renderer/backend/vulkan/VulkanUIRenderer.h"\n',
    UI_SMOKE: '#include "renderer/backend/vulkan/VulkanUIRenderer.h"\n',
}
MODULES = (
    ("minecraftc_render_data", "4", CONTRACT, ""),
    ("minecraftc_render_vulkan", "5", FACTORY, "minecraftc_render_data"),
    ("minecraftc_ui", "6", UI_SOURCE, "minecraftc_render_data"),
    ("minecraftc", "8", "src/app/Application.cpp", "minecraftc_ui;minecraftc_render_vulkan"),
)
HEADERS = (
    (CONTRACT, "minecraftc_render_data", "src/ui/;src/renderer/backend/vulkan/"),
    (FACTORY, "minecraftc_render_vulkan", f"src/app/Application.cpp;{UI_SMOKE}"),
)


class ModuleBoundaryChecks(unittest.TestCase):
    def scan(self, files=None, modules=MODULES, headers=HEADERS):
        with tempfile.TemporaryDirectory(prefix="modules-") as directory:
            root = Path(directory)
            for name, source in {**FILES, **(files or {})}.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(source, encoding="utf-8")
            for filename, rows in (("modules.tsv", modules), ("headers.tsv", headers)):
                (root / filename).write_text(
                    "".join("\t".join(row) + "\n" for row in rows), encoding="utf-8")
            return subprocess.run(
                [sys.executable, "-B", str(CHECKER), str(root),
                 str(root / "modules.tsv"), str(root / "headers.tsv")],
                capture_output=True, text=True, timeout=15)

    def assert_rejected(self, result, diagnostic):
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn(diagnostic, result.stderr)

    def test_legal_contract_consumers_and_test_only_entry(self):
        result = self.scan()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("2 production sources", result.stdout)

    def test_forbidden_relative_macro_include(self):
        result = self.scan({UI_SOURCE: FILES[UI_SOURCE] +
            '#define UI_FACTORY "../renderer/backend/vulkan/VulkanUIRenderer.h"\n'
            '# include /* graphics composition belongs in the app */ UI_FACTORY\n'})
        self.assert_rejected(result, f"{UI_SOURCE}:3: include crosses the {FACTORY} boundary")

    def test_unlisted_test_cannot_use_private_header(self):
        path = "tests/UnlistedSmoke.inc"
        result = self.scan({path: FILES[UI_SMOKE]})
        self.assert_rejected(result, f"{path}:1: include crosses the {FACTORY} boundary")

    def test_local_header_with_same_name_and_commented_include(self):
        result = self.scan({"src/ui/VulkanUIRenderer.h": "#pragma once\n",
            UI_SOURCE: '#include "VulkanUIRenderer.h"\n/*\n' + FILES[UI_SMOKE] + '*/\n'})
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_upward_link(self):
        producer, layer, source, _ = MODULES[0]
        result = self.scan(modules=((producer, layer, source, "minecraftc_ui"), *MODULES[1:]))
        self.assert_rejected(result, f"{producer}: invalid dependency on minecraftc_ui")

    def test_transitive_ui_backend_link(self):
        ui, layer, source, _ = MODULES[2]
        bridge = ("minecraftc_bridge", "5", "", "minecraftc_render_vulkan")
        vulkan = ("minecraftc_render_vulkan", "4", FACTORY, "")
        result = self.scan(modules=(MODULES[0], vulkan, bridge,
            (ui, layer, source, "minecraftc_bridge"), MODULES[3]))
        self.assert_rejected(result, "UI links the Vulkan backend: "
            "minecraftc_ui -> minecraftc_bridge -> minecraftc_render_vulkan")

    def test_duplicate_production_source_ownership(self):
        result = self.scan(modules=(*MODULES, ("minecraftc_extra", "7", UI_SOURCE, "")))
        self.assert_rejected(result, f"{UI_SOURCE}: expected one production owner")

    def test_missing_or_duplicate_cmake_header_ownership(self):
        for modules in ((("minecraftc_render_data", "4", "", ""), *MODULES[1:]),
                        (*MODULES, ("minecraftc_extra", "7", CONTRACT, ""))):
            with self.subTest(modules=modules):
                self.assert_rejected(self.scan(modules=modules),
                    f"{CONTRACT}: expected one production owner minecraftc_render_data")


if __name__ == "__main__":
    unittest.main()
