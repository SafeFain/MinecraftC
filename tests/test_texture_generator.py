import hashlib
import json
import struct
import tempfile
import copy
import zlib
from unittest.mock import patch
import unittest
from pathlib import Path
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import texture_generator as tg


class TextureGeneratorTests(unittest.TestCase):
    def test_stardew_vine_emission_follows_beads_not_color(self):
        from texture_recipes import generate_material, material_maps
        name = "hanging_cloud_vine"
        profile = tg.load_material_profiles()[name]
        result = generate_material(tg, name, tg.DEFAULT_SEED)
        properties = material_maps(tg, name, result, profile)[1]
        self.assertTrue(any(p[2] == 204 for p in properties))
        self.assertTrue(any(p[3] and role in (1,2,3)
                            for p,role in zip(result["pixels"],result["roles"])))
        for pixel, role, prop in zip(result["pixels"],result["roles"],properties):
            self.assertEqual(prop[2],204 if pixel[3] and role in (4,5,6) else 0)
        # Authoring a different palette must not move the emissive silhouette.
        recolored = copy.deepcopy(result)
        recolored["pixels"] = [(255,20,40,p[3]) for p in result["pixels"]]
        self.assertEqual(properties,material_maps(tg,name,recolored,profile)[1])
        tile = tg.generate_texture(name,tg.DEFAULT_SEED)
        self.assertTrue(tile[8][3] and tile[15*16+8][3])
        self.assertFalse(tile[0][3])
        profiles = json.loads((tg.DEFINITION_ROOT/"material_profiles.json").read_text())
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/"profiles.json"
            for invalid in ([], [True], [-1], [7], "4,5,6"):
                profiles["materials"][name]["emission_roles"] = invalid
                path.write_text(json.dumps(profiles))
                with self.assertRaisesRegex(ValueError,"emission_roles"):
                    tg.load_material_profiles(path)

    def test_heaven_v9_materials_and_plants(self):
        names=list(tg.HEAVEN_TEXTURES)
        self.assertEqual(tg.NAMES[216:232], names)
        self.assertEqual(len(names),16)
        fingerprints=set()
        with tempfile.TemporaryDirectory() as directory:
            for index,name in enumerate(names):
                pixels=tg.generate_texture(name,tg.DEFAULT_SEED)
                self.assertEqual(pixels,tg.generate_texture(name,tg.DEFAULT_SEED))
                self.assertEqual({p[3] for p in pixels},{0,255} if index>=10 else {255})
                fingerprints.add(tuple(pixels))
                path=Path(directory)/(name+'.png')
                tg.write_png(path,16,16,pixels)
                self.assertFalse(tg.validate_texture(path),name)
        self.assertEqual(len(fingerprints),16)

    def test_v16_ecology_materials_and_plants(self):
        names = [n for n in tg.NAMES if n in tg.ECOLOGY_BASES or n in tg.ECOLOGY_PLANTS]
        self.assertEqual(len(names),28)
        self.assertEqual(tg.NAMES[188:216],names)
        fingerprints=set()
        with tempfile.TemporaryDirectory() as directory:
            for name in names:
                pixels=tg.generate_texture(name,tg.DEFAULT_SEED)
                self.assertEqual(pixels,tg.generate_texture(name,tg.DEFAULT_SEED))
                fingerprints.add(tuple(pixels))
                if name in tg.ECOLOGY_PLANTS:
                    self.assertEqual({p[3] for p in pixels},{0,255})
                else:
                    self.assertEqual({p[3] for p in pixels},{255})
                path=Path(directory)/(name+'.png')
                tg.write_png(path,16,16,pixels)
                self.assertFalse(tg.validate_texture(path),name)
        self.assertEqual(len(fingerprints),28)

    def test_biome_materials_and_plants(self):
        names = list(tg.BIOME_BASES) + list(tg.BIOME_PLANTS)
        self.assertEqual(len(names), 24)
        self.assertEqual(tg.NAMES[161], "black_wool")
        self.assertEqual(tg.NAMES[162:186], names)
        with tempfile.TemporaryDirectory() as directory:
            fingerprints = set()
            for name in names:
                pixels = tg.generate_texture(name, tg.DEFAULT_SEED)
                self.assertEqual(pixels, tg.generate_texture(name, tg.DEFAULT_SEED))
                fingerprints.add(tuple(pixels))
                if name in tg.BIOME_PLANTS:
                    self.assertTrue(any(p[3] == 0 for p in pixels))
                    self.assertTrue(any(p[3] == 255 for p in pixels))
                else:
                    self.assertTrue(all(p[3] == 255 for p in pixels))
                path = Path(directory) / (name+".png")
                tg.write_png(path,16,16,pixels)
                self.assertFalse(tg.validate_texture(path),name)
            self.assertEqual(len(fingerprints),24)

    def test_decoration_materials_are_deterministic_distinct_and_tile_safe(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            fingerprints = set()
            for name in tg.DECORATION_BASES:
                pixels = tg.generate_texture(name, tg.DEFAULT_SEED)
                self.assertEqual(pixels, tg.generate_texture(name, tg.DEFAULT_SEED))
                self.assertTrue(all(p[3] == 255 for p in pixels))
                path = output / (name + ".png")
                tg.write_png(path, 16, 16, pixels)
                self.assertFalse(tg.validate_texture(path), name)
                fingerprints.add(tuple(pixels))
            self.assertEqual(len(fingerprints), 18)
        definitions = tg.load_item_icon_definitions(self.item_definitions()[0])
        order = list(definitions["items"])
        self.assertEqual(order[181], "starstep_scepter")
        self.assertEqual(order[182], "stone_bricks")
        self.assertEqual(order[207], "bone_meal")
        self.assertEqual(order[208], "rooted_dirt")
        self.assertEqual(order[231], "beach_grass")
        self.assertEqual(order[259], "cave_glowshroom")
        self.assertEqual(order[280], "roasted_glowshroom")
        self.assertEqual(order[281:286], ["fishing_rod", "raw_cod", "raw_salmon",
                                      "cooked_cod", "cooked_salmon"])

    def test_village_workstation_materials_preserve_slots(self):
        names=list(tg.VILLAGE_FUNCTIONAL)
        self.assertEqual(tg.NAMES[232:],names)
        definitions=tg.load_item_icon_definitions(self.item_definitions()[0])
        self.assertEqual(list(definitions["items"])[286:],names)
        fingerprints=set()
        with tempfile.TemporaryDirectory() as directory:
            for name in names:
                pixels=tg.generate_texture(name,tg.DEFAULT_SEED)
                self.assertEqual(pixels,tg.generate_texture(name,tg.DEFAULT_SEED))
                path=Path(directory)/(name+".png")
                tg.write_png(path,16,16,pixels)
                self.assertFalse(tg.validate_texture(path),name)
                fingerprints.add(tuple(pixels))
        self.assertEqual(len(fingerprints),6)

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

    def test_overworld_leaf_species_are_distinct_from_grass_and_jungle_bark(self):
        def mean_rgb(name):
            opaque = [pixel for pixel in tg.generate_texture(name, tg.DEFAULT_SEED)
                      if pixel[3]]
            return tuple(sum(pixel[channel] for pixel in opaque) / len(opaque)
                         for channel in range(3))

        def mean_oklab(name):
            return tg._srgb_to_oklab(mean_rgb(name))

        def distance(left, right):
            return sum((a - b) ** 2 for a, b in zip(left, right)) ** .5

        grass = mean_oklab("grass_top")
        oak = mean_oklab("leaves")
        birch = mean_oklab("birch_leaves")
        jungle = mean_oklab("jungle_leaves")
        jungle_log = mean_oklab("jungle_log")
        self.assertGreater(distance(oak, grass), .075)
        self.assertGreater(distance(birch, grass), .075)
        self.assertGreater(distance(jungle, grass), .075)
        self.assertGreater(distance(jungle, jungle_log), .16)
        # Birch stays green, but its warm olive anchor carries more red than
        # blue relative to oak instead of becoming a second grass-green tile.
        birch_rgb = mean_rgb("birch_leaves")
        oak_rgb = mean_rgb("leaves")
        self.assertGreater(birch_rgb[0] - birch_rgb[2],
                           oak_rgb[0] - oak_rgb[2])

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

    def test_fire_keeps_a_complete_upright_sprite_silhouette(self):
        for seed in (0,7,12345,tg.DEFAULT_SEED):
            pixels=tg.generate_texture("fire",seed)
            widths=[sum(p[3]>0 for p in pixels[y*16:(y+1)*16]) for y in range(16)]
            self.assertEqual(widths[0],0)
            self.assertGreater(widths[-1],widths[1])
            self.assertTrue(all(p[3]==0 for p in pixels[0::16]))
            self.assertTrue(all(p[3]==0 for p in pixels[15::16]))
            source=tg.generate_special("fire",seed,tg.generate_generic("fire",seed))
            expected=[tg.PALETTES["fire"][i] for i in source]
            self.assertEqual(pixels,[expected[(15-y)*16+x] for y in range(16) for x in range(16)])

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
        self.assertIn("parts", item_definitions["documentation"]["template_schema"])
        self.assertIn("spawn_egg", item_definitions["documentation"]["template_specs"])
        entity_definitions = json.loads(
            (root / "assets/textures/definitions/entity_styles.json").read_text())
        self.assertEqual(entity_definitions["documentation"]["cross_face"]["projection"], "cube-space")
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

    def test_all_seeded_materials_meet_palette_alpha_and_seam_contracts(self):
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory)
            for seed in (*range(32), tg.DEFAULT_SEED, 12345, -1, 2**64-1):
                for name in tg.NAMES:
                    path=output/f"{name}.png"
                    tg.write_png(path,16,16,tg.generate_texture(name,seed))
                    self.assertFalse(tg.validate_texture(path),f"seed {seed}: {name}")

    def test_complete_name_domains_and_polished_stone_identity(self):
        self.assertEqual(len(set(tg.name_hash(n) for n in tg.NAMES)),len(tg.NAMES))
        self.assertNotEqual(tg.sample(99,"polished_granite",2,4),tg.sample(99,"polished_basalt",2,4))
        names=("polished_granite","polished_basalt","polished_limestone","polished_tuff","smooth_sandstone")
        layouts=[tuple(tg.PALETTES[n].index(c) for c in tg.generate_texture(n,tg.DEFAULT_SEED)) for n in names]
        self.assertEqual(len(set(layouts)),len(names))
        tile=tg.generate_texture("grass_top",10)
        self.assertEqual(sorted(tile),sorted(tg.center_periodic_tile(tile)))

    def test_declared_configuration_changes_outputs_and_invalid_definitions_fail(self):
        original=copy.deepcopy(tg.STYLE_DEFINITION)
        baseline=tg._role_palette((100,120,140))
        for section,key,value in (("global","brightness",.7),("global","saturation",.5),
                                  ("global","contrast",1.5),("global","temperature",.2)):
            modified=copy.deepcopy(original);modified[section][key]=value
            with patch.object(tg,"STYLE_DEFINITION",modified):
                self.assertNotEqual(baseline,tg._role_palette((100,120,140)),key)
        modified=copy.deepcopy(original);modified["semantic_palette"]["shadow"]["lightness_bias"]=-.13
        with patch.object(tg,"STYLE_DEFINITION",modified):
            self.assertNotEqual(baseline,tg._role_palette((100,120,140)))
        field=tg.macro_field(99,"stone")
        modified=copy.deepcopy(tg.TEXTURE_DEFINITION)
        modified["family_specs"]["stone"]["field_gain"]=.5
        with patch.object(tg,"TEXTURE_DEFINITION",modified):
            self.assertNotEqual(field,tg.macro_field(99,"stone"))
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/"definition.json"
            path.write_text('{broken')
            for loader in (tg._load_style_definition,tg._load_texture_definition,tg._load_entity_style_definitions,tg.load_material_profiles):
                with self.assertRaises(ValueError):loader(path)
            path.write_text(json.dumps(dict(original,generator_version=0)))
            with self.assertRaises(ValueError):tg._load_style_definition(path)
            before=tg.load_item_icon_definitions(self.item_definitions()[0])
            data=json.loads(self.item_definitions()[0].read_text())
            data["materials"]["wood"]=[[220,40,190]]*4
            path.write_text(json.dumps(data));after=tg.load_item_icon_definitions(path)
            self.assertNotEqual(before["materials"]["wood"],after["materials"]["wood"])

    @staticmethod
    def png_fixture(width,height,color,depth,rows,mode=0,palette=None,transparency=None):
        def chunk(kind,payload):
            return struct.pack(">I",len(payload))+kind+payload+struct.pack(">I",zlib.crc32(kind+payload)&0xffffffff)
        channels={0:1,2:3,3:1,4:2,6:4}[color];bpp=max(1,(channels*depth+7)//8)
        raw=bytearray();previous=bytes(len(rows[0]))
        for row in rows:
            raw.append(mode)
            for i,value in enumerate(row):
                left=row[i-bpp] if i>=bpp else 0;above=previous[i];corner=previous[i-bpp] if i>=bpp else 0
                p=left+above-corner;ds=(abs(p-left),abs(p-above),abs(p-corner))
                pred=(0,left,above,(left+above)//2,(left,above,corner)[ds.index(min(ds))])[mode]
                raw.append((value-pred)&255)
            previous=row
        result=b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack(">IIBBBBB",width,height,depth,color,0,0,0))
        if palette is not None:result+=chunk(b"PLTE",bytes(c for p in palette for c in p))
        if transparency is not None:result+=chunk(b"tRNS",transparency)
        return result+chunk(b"IDAT",zlib.compress(raw))+chunk(b"IEND",b"")

    def test_png_standard_filters_rgb_gray_alpha_palette_and_corruption(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/"import.png"
            pixels=[(13,47,91,255),(19,63,72,0),(24,51,99,255),(28,83,15,255)]
            rows=[bytes(c for p in pixels[y*2:y*2+2] for c in p) for y in range(2)]
            for mode in range(5):
                path.write_bytes(self.png_fixture(2,2,6,8,rows,mode))
                self.assertEqual(tg.read_generated_png(path),(2,2,pixels),mode)
            path.write_bytes(self.png_fixture(2,1,2,8,[bytes((13,47,91,19,63,72))],transparency=struct.pack(">HHH",19,63,72)))
            self.assertEqual(tg.read_generated_png(path)[2],pixels[:2])
            path.write_bytes(self.png_fixture(2,1,4,8,[bytes((33,255,99,0))]))
            self.assertEqual(tg.read_generated_png(path)[2],[(33,33,33,255),(99,99,99,0)])
            path.write_bytes(self.png_fixture(2,1,0,1,[bytes((0x40,))]))
            self.assertEqual(tg.read_generated_png(path)[2],[(0,0,0,255),(255,255,255,255)])
            path.write_bytes(self.png_fixture(2,1,3,1,[bytes((0x40,))],palette=[(13,47,91),(19,63,72)],transparency=bytes((255,0))))
            self.assertEqual(tg.read_generated_png(path)[2],pixels[:2])
            valid=path.read_bytes()
            for damaged in (valid[:-5],valid[:35]+bytes((valid[35]^1,))+valid[36:],valid+b"trailing"):
                path.write_bytes(damaged)
                with self.assertRaises(ValueError):tg.read_generated_png(path)
            with self.assertRaises(ValueError):tg.png_bytes(16,16,[(1,2,3,255)])

    def test_all_import_sources_are_validated_and_fallback_is_explicit(self):
        item_defs,block_defs=self.item_definitions()
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory);tg.generate(output,99)
            overrides=output/"override";overrides.mkdir()
            for w,h,pixels in ((8,32,[(17,31,47,255)]*256),(16,16,[(0,0,0,0)]*256),
                               (16,16,[(17,31,47,128)]*256)):
                tg.write_png(overrides/"stick.png",w,h,pixels)
                with self.assertRaises(ValueError):tg.build_items_atlas(output,99,item_defs,block_defs,overrides)
            data=json.loads(item_defs.read_text());data["items"]["stick"]["template"]="unsupported_template"
            changed=output/"item_defs.json";changed.write_text(json.dumps(data))
            with self.assertRaises(ValueError):tg.build_items_atlas(output,99,changed,block_defs)
            legacy=output/"legacy";legacy.mkdir();pixels=[(17,31,47,255)]*256
            tg.write_png(legacy/"stick.png",16,16,pixels)
            tg.build_items_atlas(output,99,changed,block_defs,legacy_dir=legacy,allow_fallback=True)
            metadata=json.loads((output/"items_atlas.json").read_text())
            self.assertEqual(metadata["items"]["stick"]["source_kind"],"legacy")
            self.assertIn("stick",metadata["generation_failures"])
            self.assertEqual(tg.read_generated_png(output/"items/stick.png")[2],pixels)
            tg.build_items_atlas(output,99,changed,block_defs,allow_fallback=True)
            self.assertEqual(json.loads((output/"items_atlas.json").read_text())["items"]["stick"]["source_kind"],"missing")

    def test_compact_atlas_semantic_maps_provenance_and_preview_match(self):
        with tempfile.TemporaryDirectory() as directory:
            output=Path(directory);tg.generate(output,99,{"grass_top":77});tg.build_atlas(output,99,{"grass_top":77})
            metadata=json.loads((output/"atlas.json").read_text())
            self.assertEqual(metadata["grid_size"], __import__("math").ceil(__import__("math").sqrt(len(metadata["physical_tiles"]))))
            self.assertGreater(len(metadata["physical_tiles"]),188)
            self.assertEqual(metadata["textures"]["grass_top"]["effective_seed"],77)
            self.assertEqual([metadata["textures"][n]["index"] for n in tg.NAMES],list(range(len(tg.NAMES))))
            for filename in ("atlas.png","atlas_normal.png","atlas_property.png","atlas_height.png"):
                self.assertEqual(tg.read_generated_png(output/filename)[:2],(metadata["grid_size"]*16,metadata["grid_size"]*16))
            tg.build_items_atlas(output,99,*self.item_definitions())
            tg.write_png(output/"items/stale.png",16,16,[(11,22,33,255)]*256)
            tg.build_visual_report(output,99)
            report=json.loads((output/"visual_report.json").read_text())
            self.assertEqual(set(report["items"]),set(json.loads((output/"items_atlas.json").read_text())["items"]))
            self.assertNotIn("stale",report["items"])
            tg.build_block_preview(output,99)
            width,_,preview=tg.read_generated_png(output/"block_preview.png")
            # grass_top's original swatch is at second cell + (3,14), scaled 2x.
            pixels=tg.read_generated_png(output/"grass_top.png")[2]
            for y in range(16):
                for x in range(16):self.assertEqual(preview[(14+y*2)*width+132+3+x*2],pixels[y*16+x])
            with self.assertRaises(ValueError):tg.build_atlas(output,100)
            profiles=tg.load_material_profiles()
            self.assertLess(profiles["blue_ice"]["roughness"],profiles["shale"]["roughness"])
            self.assertGreater(profiles["skyroot_leaves"]["roughness"],.85)
            normals,properties,_=tg.generate_material_maps("torch",tg.generate_texture("torch",99),profiles["torch"])
            self.assertTrue(any(p[2]>0 for p in properties))
            for p,n,q in zip(tg.generate_texture("torch",99),normals,properties):
                if not p[3]:self.assertEqual(n,(128,128,255,255));self.assertEqual(q[2],0)
            stone=tg.generate_texture("stone",99)
            original=tg.generate_material_maps("stone",stone,profiles["stone"])
            # Geometry follows roles, so recoloring the same art cannot alter it.
            palette=[(c[2],c[0],c[1],c[3]) for c in tg.PALETTES["stone"]]
            recolored=[palette[tg.PALETTES["stone"].index(c)] for c in stone]
            with patch.dict(tg.PALETTES,{"stone":palette}):
                self.assertEqual(original,tg.generate_material_maps("stone",recolored,profiles["stone"]))

    def test_linear_mips_cutout_coverage_and_equal_luminance_hue_detection(self):
        tile=[(64,64,64,255),(192,192,192,255)]*128
        mip=tg.tile_mip_chain(tile)[1][1]
        self.assertEqual(mip[0],(146,146,146,255))
        tile=[(64,64,64,255),(255,0,255,0)]*128
        self.assertEqual(tg.tile_mip_chain(tile)[1][1][0],(64,64,64,127))
        for size,pixels in tg.tile_mip_chain(tg.generate_texture("leaves",99),True)[1:-1]:
            self.assertTrue(any(p[3]==0 for p in pixels));self.assertTrue(any(p[3]==255 for p in pixels))
        self.assertLess(tg.color_distance((255,0,0,255),(0,76,0,255)),1)
        self.assertGreater(tg.perceptual_distance((255,0,0,255),(0,76,0,255)),.2)

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
