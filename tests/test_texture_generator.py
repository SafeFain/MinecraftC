import hashlib
import json
import tempfile
import unittest
from pathlib import Path
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import texture_generator as tg


class TextureGeneratorTests(unittest.TestCase):
    def test_generation_is_deterministic_and_tileable(self):
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            a, b = Path(first), Path(second)
            tg.generate(a, 12345)
            tg.generate(b, 12345)
            tg.validate(a)
            for name in tg.NAMES:
                _, _, pixels = tg.read_generated_png(a / f"{name}.png")
                self.assertGreaterEqual(len(set(pixels)), 5)
                self.assertLessEqual(len(set(pixels)), 8)
                self.assertTrue(all(pixel[3] in (0, 255) for pixel in pixels))
                self.assertFalse(any(pixel[3] and pixel[:3] == (0, 0, 0)
                                     for pixel in pixels))
                self.assertEqual(hashlib.sha256((a / f"{name}.png").read_bytes()).digest(),
                                 hashlib.sha256((b / f"{name}.png").read_bytes()).digest())
            for name in tg.HIGH_CONTRAST_NAMES:
                self.assertGreaterEqual(tg.palette_contrast(tg.PALETTES[name]),
                                        tg.palette_contrast(tg.PALETTES["stone"]) * 1.15)
            _, _, grass = tg.read_generated_png(a / "grass_side.png")
            grass_colors = set(tg.PALETTES["grass_side"][4:])
            rows = [y for y in range(15) for x in range(15)
                    if grass[y * 16 + x] in grass_colors]
            self.assertLess(sum(rows) / len(rows), 7.0)

            # Toroidal generation must not fake tiling by copying opposing
            # borders. The seam metric instead compares wrap adjacency with
            # ordinary interior adjacency.
            for name in ("grass_top", "dirt", "stone", "sand"):
                _, _, pixels = tg.read_generated_png(a / f"{name}.png")
                self.assertNotEqual(pixels[:16], pixels[-16:])
                self.assertNotEqual(pixels[0::16], pixels[15::16])
                self.assertLessEqual(tg.structure_metrics(pixels)["seam_ratio"], 2.60)

    def test_natural_structure_has_no_cross_lines_or_rectangular_dominance(self):
        for seed in (1, 12345, tg.DEFAULT_SEED):
            for name in tg.NATURAL:
                pixels = tg.generate_texture(name, seed)
                metrics = tg.structure_metrics(pixels)
                self.assertLessEqual(metrics["longest_run"], 8, name)
                self.assertLessEqual(metrics["largest_component"], .34, name)
                self.assertLessEqual(metrics["largest_rectangularity"], .96, name)
                self.assertLessEqual(metrics["periodicity"], .76, name)
                self.assertLessEqual(metrics["center_cross"], .31, name)
            grass = tg.structure_metrics(tg.generate_texture("grass_top", seed))
            self.assertLessEqual(grass["center_cross"], .31)

    def test_ore_clusters_and_directional_exceptions(self):
        for seed in (7, tg.DEFAULT_SEED):
            for name in ("coal_ore", "copper_ore", "iron_ore", "gold_ore", "diamond_ore"):
                palette = tg.PALETTES[name]
                pixels = tg.generate_texture(name, seed)
                ore = set(palette[3:])
                cells = {(x, y) for y in range(16) for x in range(16)
                         if pixels[y * 16 + x] in ore}
                components = 0
                while cells:
                    components += 1
                    pending = [cells.pop()]
                    while pending:
                        for neighbor in tg.neighbors4(*pending.pop()):
                            if neighbor in cells:
                                cells.remove(neighbor)
                                pending.append(neighbor)
                self.assertIn(components, range(2, 5), name)
        # Directional materials are intentionally not subject to natural-block
        # straight-run thresholds, but remain seam-checked.
        self.assertIn("oak_planks", tg.DIRECTIONAL)
        self.assertIn("oak_log", tg.DIRECTIONAL)
        self.assertLessEqual(tg.structure_metrics(
            tg.generate_texture("oak_planks", tg.DEFAULT_SEED))["seam_ratio"], 2.60)

    def test_contact_sheet_and_per_material_local_seed(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            local = {"grass_top": 77}
            tg.generate(output, 10, local)
            self.assertEqual((output / "grass_top.png").read_bytes(),
                             tg.png_bytes(16, 16, tg.generate_texture("grass_top", 77)))
            baseline_stone = (output / "stone.png").read_bytes()
            tg.build_contact_sheet(output, 10, 3, local)
            self.assertTrue((output / "contact_sheet.png").exists())
            metadata = json.loads((output / "contact_sheet.json").read_text())
            self.assertEqual(len(metadata["candidate_seeds"]), 3)
            self.assertEqual(metadata["local_seeds"]["grass_top"], 77)
            self.assertEqual((output / "stone.png").read_bytes(), baseline_stone)

    def test_seed_changes_output_and_atlas_metadata_is_complete(self):
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            a, b = Path(first), Path(second)
            tg.generate(a, 1)
            tg.generate(b, 2)
            self.assertNotEqual((a / "stone.png").read_bytes(),
                                (b / "stone.png").read_bytes())
            tg.build_atlas(a, 1)
            metadata = __import__("json").loads((a / "atlas.json").read_text())
            self.assertEqual(set(metadata["textures"]), set(tg.NAMES))
            self.assertEqual(metadata["filter"], "nearest")

    def test_validator_rejects_non_tiling_edge(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            tg.generate(output, 9)
            width, height, pixels = tg.read_generated_png(output / "dirt.png")
            pixels[-1] = (255, 0, 255, 255)
            tg.write_png(output / "dirt.png", width, height, pixels)
            with self.assertRaises(ValueError):
                tg.validate(output)


if __name__ == "__main__":
    unittest.main()
