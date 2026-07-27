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
    def item_definitions(self):
        root = Path(__file__).resolve().parents[1]
        return (root / "assets/textures/definitions/item_icons.json",
                root / "assets/textures/definitions/blocks.json")

    def test_entity_atlas_is_deterministic_and_complete(self):
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            a, b = Path(first), Path(second)
            tg.build_entity_atlas(a, 314159)
            tg.build_entity_atlas(b, 314159)
            self.assertEqual((a / "entity_atlas.png").read_bytes(),
                             (b / "entity_atlas.png").read_bytes())
            metadata = json.loads((a / "entity_atlas.json").read_text())
            self.assertEqual(tuple(metadata["entities"]), tuple(sorted(tg.ENTITY_NAMES)))
            self.assertEqual({entry["index"] for entry in metadata["entities"].values()},
                             set(range(9)))
            width, height, pixels = tg.read_generated_png(a / "entity_atlas.png")
            self.assertEqual((width, height), (48, 48))
            self.assertTrue(all(pixel[3] == 255 for pixel in pixels))
            for name in tg.ENTITY_NAMES:
                _, _, tile = tg.read_generated_png(a / "entities" / f"{name}.png")
                self.assertGreaterEqual(len(set(tile)), 4, name)

    def test_item_templates_palettes_alpha_and_bounds(self):
        item_defs, _ = self.item_definitions()
        definitions = tg.load_item_icon_definitions(item_defs)
        self.assertEqual(set(tg.GENERATOR_CATEGORIES),
                         {"block_texture", "item_sprite", "block_item_icon"})
        for material in ("wood", "stone", "copper", "iron", "gold"):
            self.assertGreaterEqual(len(definitions["materials"][material]), 4)
        for material in definitions["materials"]:
            for template in definitions["templates"]:
                pixels = tg.generate_item_sprite(template, material, definitions)
                self.assertEqual(len(pixels), 256)
                self.assertTrue(all(pixel[3] in (0, 255) for pixel in pixels))
                self.assertTrue(any(pixel[3] == 0 for pixel in pixels))
                self.assertFalse(tg.validate_item_sprite(pixels, template))
                opaque = [(x, y) for y in range(16) for x in range(16)
                          if pixels[y * 16 + x][3]]
                self.assertTrue(all(0 <= x < 16 and 0 <= y < 16 for x, y in opaque))

    def test_items_atlas_is_deterministic_complete_and_honors_overrides(self):
        item_defs, block_defs = self.item_definitions()
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second, tempfile.TemporaryDirectory() as overrides, tempfile.TemporaryDirectory() as legacy:
            a, b = Path(first), Path(second)
            for output in (a, b):
                tg.generate(output, 99)
                tg.build_items_atlas(output, 99, item_defs, block_defs,
                                     Path(overrides), Path(legacy))
            self.assertEqual((a / "items_atlas.png").read_bytes(),
                             (b / "items_atlas.png").read_bytes())
            metadata = json.loads((a / "items_atlas.json").read_text())
            definitions = tg.load_item_icon_definitions(item_defs)
            self.assertEqual(set(metadata["items"]), set(definitions["items"]))
            self.assertEqual(metadata["filter"], "nearest")
            self.assertEqual(metadata["priority"],
                             ["override", "generated", "legacy", "missing"])
            self.assertEqual(sorted(v["index"] for v in metadata["items"].values()),
                             list(range(len(definitions["items"]))))

            override_pixels = [(0, 0, 0, 0)] * 256
            override_pixels[0] = (17, 31, 47, 255)
            tg.write_png(Path(overrides) / "stick.png", 16, 16, override_pixels)
            tg.write_png(Path(legacy) / "stick.png", 16, 16,
                         [(91, 92, 93, 255)] * 256)
            tg.build_items_atlas(a, 99, item_defs, block_defs,
                                 Path(overrides), Path(legacy))
            metadata = json.loads((a / "items_atlas.json").read_text())
            self.assertEqual(metadata["items"]["stick"]["source_kind"], "override")
            width, _, atlas_pixels = tg.read_generated_png(a / "items_atlas.png")
            index = metadata["items"]["stick"]["index"]
            self.assertEqual(atlas_pixels[(index // 8 * 16) * width +
                                          (index % 8 * 16)], (17, 31, 47, 255))

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
