import hashlib
import json
import struct
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

    def test_ios_app_icon_is_deterministic_opaque_and_complete(self):
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            a = Path(first) / "AppIcon.png"
            b = Path(second) / "AppIcon.png"
            tg.build_ios_app_icon(a, tg.DEFAULT_SEED)
            tg.build_ios_app_icon(b, tg.DEFAULT_SEED)
            self.assertEqual(a.read_bytes(), b.read_bytes())
            width, height, pixels = tg.read_generated_png(a)
            self.assertEqual((width, height), (1024, 1024))
            self.assertTrue(all(pixel[3] == 255 for pixel in pixels))
            self.assertGreater(len(set(pixels)), 16)

    def test_desktop_app_icons_are_deterministic_and_well_formed(self):
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            a, b = Path(first), Path(second)
            tg.build_desktop_app_icons(a, tg.DEFAULT_SEED)
            tg.build_desktop_app_icons(b, tg.DEFAULT_SEED)
            for name in ("minecraftc.png", "minecraftc.ico", "minecraftc.icns"):
                self.assertEqual((a / name).read_bytes(), (b / name).read_bytes())
            self.assertEqual((a / "minecraftc.ico").read_bytes()[:6],
                             b"\x00\x00\x01\x00\x01\x00")
            icns = (a / "minecraftc.icns").read_bytes()
            self.assertEqual(icns[:4], b"icns")
            self.assertEqual(struct.unpack(">I", icns[4:8])[0], len(icns))

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
                             set(range(len(tg.ENTITY_NAMES))))
            width, height, pixels = tg.read_generated_png(a / "entity_atlas.png")
            self.assertEqual((width, height), (64, 64))
            self.assertTrue(all(pixel[3] == 255 for pixel in pixels))
            for name in tg.ENTITY_NAMES:
                _, _, tile = tg.read_generated_png(a / "entities" / f"{name}.png")
                self.assertGreaterEqual(len(set(tile)), 2, name)

    def test_entity_skins_are_deterministic_semantic_and_face_specific(self):
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            a, b = Path(first), Path(second)
            tg.build_entity_skins(a, 314159)
            tg.build_entity_skins(b, 314159)
            metadata = json.loads((a / "entity_skins.json").read_text())
            self.assertEqual(metadata["layout"], tg.ENTITY_SKIN_LAYOUT)
            self.assertEqual(set(metadata["entities"]), set(tg.ENTITY_SKIN_NAMES))
            self.assertEqual(metadata["filter"], "nearest")
            for name in tg.ENTITY_SKIN_NAMES:
                path=a / "entity_skins" / f"{name}.png"
                self.assertEqual(path.read_bytes(),
                                 (b / "entity_skins" / f"{name}.png").read_bytes())
                width, height, pixels=tg.read_generated_png(path)
                self.assertEqual((width,height),(64,64))
                self.assertTrue(all(pixel[3]==255 for pixel in pixels))
                tiles=[]
                for index in range(16):
                    tx,ty=index%4,index//4
                    tiles.append(tuple(pixels[(ty*16+y)*64+tx*16+x]
                                       for y in range(16) for x in range(16)))
                # Quiet faces may match; semantic front/back separation is required below.
                self.assertNotEqual(tiles[tg.ENTITY_SKIN_LAYOUT["head_front"]],
                                    tiles[tg.ENTITY_SKIN_LAYOUT["head_back"]],name)
                front=tiles[tg.ENTITY_SKIN_LAYOUT["head_front"]]
                self.assertGreater(max(tg.luminance(p) for p in front)-
                                   min(tg.luminance(p) for p in front),35,name)
                for semantic in ("head_back","body_back"):
                    tile=tiles[tg.ENTITY_SKIN_LAYOUT[semantic]]
                    jumps=[]
                    for y in range(16):
                        for x in range(15):
                            jumps.append(abs(tg.luminance(tile[y*16+x])-
                                             tg.luminance(tile[y*16+x+1])))
                    for x in range(16):
                        for y in range(15):
                            jumps.append(abs(tg.luminance(tile[y*16+x])-
                                             tg.luminance(tile[(y+1)*16+x])))
                    self.assertLess(sum(jump>55 for jump in jumps)/len(jumps),.08,
                                    f"{name} {semantic} has abrupt base transitions")

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
                self.assertGreaterEqual(len(set(pixels)), 2, name)
                self.assertLessEqual(len(set(pixels)), 8)
                self.assertTrue(all(pixel[3] in (0, 255) for pixel in pixels))
                self.assertFalse(any(pixel[3] and pixel[:3] == (0, 0, 0)
                                     for pixel in pixels))
                self.assertEqual(hashlib.sha256((a / f"{name}.png").read_bytes()).digest(),
                                 hashlib.sha256((b / f"{name}.png").read_bytes()).digest())
            for name in ("grass_side", "aether_grass_side"):
                _, _, grass = tg.read_generated_png(a / f"{name}.png")
                grass_colors = set(tg.PALETTES[name][4:])
                self.assertTrue(all(grass[y * 16 + x] in grass_colors
                                    for y in range(5) for x in range(16)))
                self.assertTrue(all(grass[y * 16 + x] not in grass_colors
                                    for y in range(8, 16) for x in range(16)))

            # Toroidal generation must not fake tiling by copying opposing
            # borders. The seam metric instead compares wrap adjacency with
            # ordinary interior adjacency.
            for name in ("grass_top", "dirt", "stone", "sand"):
                _, _, pixels = tg.read_generated_png(a / f"{name}.png")
                self.assertNotEqual(pixels[:16], pixels[-16:])
                self.assertNotEqual(pixels[0::16], pixels[15::16])
                self.assertLessEqual(tg.structure_metrics(pixels)["seam_ratio"], 2.60)

    def test_quiet_planes_and_natural_material_frequency(self):
        self.assertEqual(tg.quantize([0.0]*256), [2]*256)
        self.assertEqual(tg.quantize([.001]*256), [2]*256)
        for seed in (1, 12345, tg.DEFAULT_SEED):
            for name in ("grass_top", "dirt", "stone", "sand", "oak_planks"):
                pixels=tg.generate_texture(name,seed)
                stats=tg._visual_color_stats(pixels)
                self.assertLess(stats["mean_neighbor_delta"], 9, name)
                self.assertLess(stats["dark_fraction"], .05, name)
                self.assertLessEqual(tg.structure_metrics(pixels)["seam_ratio"],2.60,name)
            for name in ("deepslate","obsidian","black_sand"):
                self.assertLess(tg._visual_color_stats(tg.generate_texture(name,seed))[
                    "oklab_lightness_mean"],.46,name)

    def test_leaf_clusters_preserve_bounded_binary_gaps(self):
        for seed in (1,7,12345,tg.DEFAULT_SEED):
            for name in tg.LEAF_NAMES:
                pixels=tg.generate_texture(name,seed)
                gaps=sum(p[3]==0 for p in pixels)
                self.assertGreaterEqual(gaps,24,name)
                self.assertLessEqual(gaps,64,name)
                self.assertTrue(all(p[3] in (0,255) for p in pixels))
                self.assertGreaterEqual(len({p for p in pixels if p[3]}),3,name)
                self.assertLessEqual(tg.structure_metrics(pixels)["seam_ratio"],2.60,name)

    def test_tool_parts_cannot_paint_outside_their_masks(self):
        definitions=tg.load_item_icon_definitions(self.item_definitions()[0])
        for template in tg.TOOL_TEMPLATES:
            masks=tg.tool_part_masks(template)
            self.assertFalse(masks["grip"] & masks["working_head"])
            self.assertFalse(masks["connector"] & masks["working_head"])
            self.assertLessEqual(masks["edge"],masks["working_head"])
            expected=masks["grip"] | masks["connector"] | masks["working_head"]
            for material in definitions["materials"]:
                pixels=tg.generate_item_sprite(template,material,definitions)
                self.assertEqual({i for i,p in enumerate(pixels) if p[3]},expected)
        heads=[frozenset(tg.tool_part_masks(t)["working_head"]) for t in tg.TOOL_TEMPLATES]
        self.assertEqual(len(set(heads)),5)

    def test_functional_faces_and_all_definition_references(self):
        definitions=tg.load_item_icon_definitions(self.item_definitions()[0])
        blocks=json.loads(self.item_definitions()[1].read_text())["blocks"]
        for block,faces in blocks.items():
            for field,material in faces.items():
                if field in {"all","top","bottom","side","front","back","left","right"}:
                    self.assertIn(material,tg.NAMES,block)
        for base in tg.FUNCTIONAL:
            faces=[tuple(tg.generate_texture(base+suffix,tg.DEFAULT_SEED))
                   for suffix in ("","_top","_side","_bottom")]
            self.assertNotEqual(faces[0],faces[1],base)
            self.assertNotEqual(faces[1],faces[2],base)
            self.assertNotEqual(faces[1],faces[3],base)
        foods=["bread","raw_beef","raw_porkchop","raw_chicken","raw_mutton","rotten_flesh"]
        sprites=[tuple(tg.generate_item_sprite(definitions["items"][n]["template"],
                    definitions["items"][n]["material"],definitions)) for n in foods]
        self.assertEqual(len(set(sprites)),len(foods))
        a=[(100,120,140,255)]*256; b=[(210,180,110,255)]*256
        self.assertNotEqual(tg.generate_block_item_icon(a,a),tg.generate_block_item_icon(a,a,b))

    def test_plant_art_has_roots_at_png_bottom(self):
        for name in ("torch","wheat_young","wheat_middle","wheat_mature","oak_sapling","flower"):
            pixels=tg.generate_texture(name,21)
            self.assertTrue(any(p[3] for p in pixels[-16:]),name)
        flower=tg.generate_texture("flower",21)
        petal=tg.PALETTES["flower"][6]
        rows=[i//16 for i,p in enumerate(flower) if p==petal]
        self.assertLess(max(rows),8)

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

    def test_bright_style_metadata_and_visual_report(self):
        root = Path(__file__).resolve().parents[1]
        style = json.loads((root / "assets/textures/definitions/style.json").read_text())
        self.assertEqual(style["id"], "bright-comfortable")
        self.assertEqual(style["generator_version"], tg.GENERATOR_VERSION)
        self.assertIn("semantic_palette", style)
        self.assertIn("dark_materials", style["exceptions"])
        texture_definitions = json.loads(
            (root / "assets/textures/definitions/textures.json").read_text())
        self.assertEqual(set(texture_definitions["family_specs"]), {
            "soil", "turf", "stone", "ore", "wood", "foliage", "sand_ice",
            "fluid_emissive", "constructed"})
        item_definitions = json.loads(
            (root / "assets/textures/definitions/item_icons.json").read_text())
        self.assertIn("parts", item_definitions["template_schema"])
        self.assertIn("spawn_egg", item_definitions["template_specs"])
        entity_definitions = json.loads(
            (root / "assets/textures/definitions/entity_styles.json").read_text())
        self.assertEqual(entity_definitions["cross_face"]["projection"], "cube-space")
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            tg.generate(output, 21)
            tg.build_atlas(output, 21)
            tg.build_visual_report(output, 21)
            atlas = json.loads((output / "atlas.json").read_text())
            report = json.loads((output / "visual_report.json").read_text())
            self.assertEqual(atlas["generator_version"], tg.GENERATOR_VERSION)
            self.assertEqual(atlas["style"], tg.STYLE_ID)
            self.assertEqual(report["style"], tg.STYLE_ID)
            self.assertEqual(set(report["textures"]), set(tg.NAMES))
            self.assertGreater(report["textures"]["grass_top"]["oklab_lightness_mean"], .45)

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
