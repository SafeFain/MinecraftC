#!/usr/bin/env python3
"""Deterministic pixel materials, semantic sprites, validator and atlas builder.

V5 uses composable semantic recipes and sequences with compact semantic material atlases, strict PNG import and configurable
OKLCH palettes. Absolute field thresholds and sparse material features keep quiet
planes quiet. Functional blocks have explicit face art, tools have disjoint part
masks, and entity skins share their source with embedded GLB textures.
"""

import argparse
import hashlib
import json
import math
import struct
import sys
import zlib
from collections import Counter, deque
from pathlib import Path
from functools import lru_cache
from texture_png import read_png

SIZE = 16
GENERATOR_VERSION = 5
STYLE_ID = "bright-comfortable"
# Selected from the deterministic contact-sheet candidates. CMake's asset
# target relies on this default, so keep it aligned with committed atlas.json.
DEFAULT_SEED = 213785369
GENERATOR_CATEGORIES = ("block_texture", "item_sprite", "block_item_icon")
ENTITY_NAMES = ("cow", "pig", "sheep", "chicken", "zombie", "skeleton",
                "spider", "blastling", "item", "villager", "zombie_villager", "iron_golem")
ENTITY_SKIN_NAMES = tuple(name for name in ENTITY_NAMES if name != "item") + ("player",)
ENTITY_SKIN_SIZE = 64
ENTITY_SKIN_LAYOUT = {
    "head_front": 0, "head_back": 1, "head_left": 2, "head_right": 3,
    "head_top": 4, "head_bottom": 5,
    "body_front": 6, "body_back": 7, "body_left": 8, "body_right": 9,
    "body_top": 10, "body_bottom": 11,
    "limb_primary": 12, "limb_secondary": 13, "detail": 14,
    "fallback": 15,
}
ENTITY_PALETTES = {
    "cow": ((61,39,25,255),(82,49,29,255),(111,67,38,255),
            (151,105,68,255),(213,199,171,255),(238,229,207,255)),
    "pig": ((147,67,78,255),(180,89,101,255),(207,112,124,255),
            (226,139,149,255),(239,166,174,255),(248,192,198,255)),
    "sheep": ((133,126,112,255),(160,153,139,255),(185,181,168,255),
              (207,204,193,255),(226,224,215,255),(241,240,232,255)),
    "chicken": ((157,47,38,255),(211,66,43,255),(209,139,31,255),
                (232,184,64,255),(218,216,199,255),(245,244,229,255)),
    "zombie": ((32,67,44,255),(46,88,54,255),(64,108,66,255),
               (55,94,96,255),(65,116,123,255),(91,137,130,255)),
    "skeleton": ((92,84,69,255),(126,116,95,255),(159,149,124,255),
                 (190,181,154,255),(218,209,181,255),(239,233,207,255)),
    "spider": ((32,22,20,255),(48,30,26,255),(68,40,33,255),
               (92,52,39,255),(137,25,24,255),(213,42,31,255)),
    "blastling": ((29,69,30,255),(39,94,37,255),(53,119,43,255),
                  (70,146,48,255),(97,174,58,255),(132,204,76,255)),
    "item": ((91,55,21,255),(122,76,25,255),(158,104,31,255),
             (192,137,39,255),(223,174,58,255),(244,207,91,255)),
    "villager": ((62,39,27,255),(91,57,35,255),(126,78,45,255),
                  (159,104,62,255),(190,139,91,255),(220,177,128,255)),
    "zombie_villager": ((31,63,39,255),(43,82,48,255),(58,104,59,255),
                         (78,124,70,255),(109,145,86,255),(145,169,106,255)),
    "player": ((34,52,76,255),(48,72,101,255),(61,93,126,255),
               (88,126,154,255),(181,126,91,255),(229,177,132,255)),
}
NAMES = [
    "dirt", "grass_top", "grass_side", "stone", "oak_log", "oak_log_top",
    "leaves", "sand", "bedrock", "water", "snow", "oak_planks",
    "deepslate", "cactus_side", "cactus_top", "coal_ore", "iron_ore",
    "gold_ore", "diamond_ore", "lava", "ice", "gravel", "clay",
    "red_sand", "terracotta", "podzol_top", "moss", "tall_grass",
    "flower", "reeds", "birch_log", "birch_leaves", "spruce_log",
    "spruce_leaves", "jungle_log", "jungle_leaves", "acacia_log",
    "acacia_leaves", "cobblestone", "crafting_table", "furnace", "chest",
    "torch", "white_wool", "white_bed", "farmland", "wet_farmland",
    "wheat_young", "wheat_middle", "wheat_mature", "oak_sapling",
    "birch_sapling", "spruce_sapling", "jungle_sapling", "acacia_sapling",
    "snow_layer", "fire", "glass", "tnt", "obsidian", "dandelion",
    "blue_orchid", "allium", "oxeye_daisy", "sunflower_bottom",
    "sunflower_top", "cloud", "limestone", "basalt", "tuff",
    "coarse_dirt", "mud", "packed_ice", "black_sand", "granite",
    "aether_grass_top", "aether_grass_side", "aether_soil", "cloudstone",
    "sunstone", "skyroot_log", "skyroot_log_top", "skyroot_leaves",
    "star_crystal", "starflower", "cloud_bloom", "glowshroom",
    "dripstone_block", "pointed_dripstone", "calcite", "hanging_roots",
    "glow_fern", "resonant_crystal", "sulfur_crust",
    "emerald_ore", "deepslate_emerald_ore", "composter", "fletching_table",
    "loom", "cauldron", "blast_furnace", "smithing_table", "grindstone",
    "copper_ore",
]


# Existing names retain their atlas positions. Additional faces are appended.
VILLAGE_FUNCTIONAL = ("barrel", "lectern", "cartography_table", "brewing_stand", "smoker", "stonecutter")
FUNCTIONAL = ("crafting_table", "furnace", "chest", "composter",
              "fletching_table", "loom", "cauldron", "blast_furnace",
              "smithing_table", "grindstone", "white_bed", "tnt")
EXTRA_LOG_TOPS = ("birch_log_top", "spruce_log_top", "jungle_log_top", "acacia_log_top")
NAMES += list(EXTRA_LOG_TOPS) + [name + "_" + face for name in FUNCTIONAL
                               for face in ("top", "side", "bottom")]


# Crafted decoration is appended after all existing atlas slots.
DECORATION_BASES = {
    "stone_bricks": (128, 132, 132),
    "mossy_stone_bricks": (110, 131, 105),
    "cracked_stone_bricks": (120, 124, 124),
    "chiseled_stone_bricks": (128, 132, 132),
    "bricks": (174, 91, 67),
    "polished_granite": (142, 91, 77),
    "polished_basalt": (57, 60, 64),
    "polished_limestone": (184, 181, 163),
    "polished_tuff": (91, 104, 96),
    "deepslate_bricks": (58, 64, 73),
    "sandstone": (211, 185, 130),
    "cut_sandstone": (211, 185, 130),
    "smooth_sandstone": (219, 197, 149),
    "red_wool": (185, 53, 48),
    "yellow_wool": (225, 184, 34),
    "blue_wool": (58, 113, 192),
    "green_wool": (76, 131, 55),
    "black_wool": (44, 45, 49),
}
NAMES += list(DECORATION_BASES)

# Generation-v15 natural materials; existing slots are unchanged.
BIOME_BASES = {'rooted_dirt': (130, 94, 65), 'leaf_litter_soil': (151, 104, 52), 'peat': (76, 66, 48), 'silt': (145, 133, 96), 'dry_grass_block': (172, 158, 78), 'permafrost': (134, 139, 144), 'blue_ice': (86, 154, 215), 'shale': (105, 112, 121), 'red_sandstone': (181, 106, 61), 'ochre_terracotta': (184, 136, 61), 'white_terracotta': (211, 189, 160), 'volcanic_ash': (107, 107, 105), 'coral_rock': (195, 143, 126)}
BIOME_PLANTS = ('fern', 'dead_bush', 'dry_grass', 'brown_mushroom', 'red_mushroom', 'lavender', 'bellflower', 'alpine_flower', 'tropical_flower', 'cattail', 'beach_grass')
NAMES += list(BIOME_BASES) + list(BIOME_PLANTS) + ["dry_grass_side", "leaf_litter_side"]


# Generation-v16 ecology: append logical slots after all v15 materials.
ECOLOGY_BASES = {'andesite': (124, 128, 121), 'diorite': (199, 198, 185), 'gneiss': (124, 116, 128), 'marble': (219, 216, 200), 'laterite': (169, 85, 49), 'red_clay': (178, 104, 78), 'cracked_mud': (132, 118, 84), 'salt_crust': (225, 221, 202), 'cave_moss': (67, 112, 72), 'wet_limestone': (137, 158, 151), 'gypsum': (219, 208, 180), 'amethyst_block': (148, 110, 182), 'quartz_block': (220, 209, 198), 'iron_stained_rock': (150, 108, 77), 'sulfur_rock': (178, 157, 63)}
ECOLOGY_PLANTS = ('clover', 'heather', 'wild_mint', 'nettle', 'desert_flower', 'small_cactus', 'reed_flower', 'tundra_moss', 'fallen_twigs', 'jungle_fern', 'amethyst_cluster', 'quartz_cluster', 'cave_glowshroom')
NAMES += ['andesite', 'diorite', 'gneiss', 'marble', 'laterite', 'red_clay', 'cracked_mud', 'salt_crust', 'clover', 'heather', 'wild_mint', 'nettle', 'desert_flower', 'small_cactus', 'reed_flower', 'tundra_moss', 'fallen_twigs', 'jungle_fern', 'cave_moss', 'wet_limestone', 'gypsum', 'amethyst_block', 'quartz_block', 'iron_stained_rock', 'sulfur_rock', 'amethyst_cluster', 'quartz_cluster', 'cave_glowshroom']

# Heaven v9: append slots, using existing structural recipes with exclusive palettes.
HEAVEN_TEXTURES = {'moonstone': 'marble', 'skystone': 'andesite', 'aether_moss': 'cave_moss', 'glimmer_silt': 'silt', 'star_crystal_ore': 'diamond_ore', 'skyroot_planks': 'oak_planks', 'cloudstone_bricks': 'stone_bricks', 'sunstone_bricks': 'bricks', 'moonstone_bricks': 'stone_bricks', 'star_crystal_lamp': 'amethyst_block', 'sky_fern': 'fern', 'dawn_bell': 'bellflower', 'moonflower': 'alpine_flower', 'glimmer_reed': 'reed_flower', 'cloudberry_bush': 'wild_mint', 'hanging_cloud_vine': 'hanging_roots'}
HEAVEN_COLORS = {'moonstone': (213, 219, 235), 'skystone': (116, 153, 179), 'aether_moss': (109, 177, 157), 'glimmer_silt': (96, 136, 143), 'star_crystal_ore': (100, 178, 206), 'skyroot_planks': (191, 154, 106), 'cloudstone_bricks': (202, 217, 230), 'sunstone_bricks': (232, 194, 114), 'moonstone_bricks': (215, 223, 240), 'star_crystal_lamp': (133, 230, 242), 'sky_fern': (107, 182, 168), 'dawn_bell': (244, 199, 124), 'moonflower': (180, 187, 245), 'glimmer_reed': (123, 211, 195), 'cloudberry_bush': (135, 187, 166), 'hanging_cloud_vine': (125, 187, 204)}
NAMES += list(HEAVEN_TEXTURES)
NAMES += ['barrel', 'lectern', 'cartography_table', 'brewing_stand', 'smoker', 'stonecutter']
DOOR_WOODS = ('oak', 'birch', 'spruce', 'jungle', 'acacia', 'skyroot', 'iron')
VARIANT_PLANKS = ('birch_planks', 'spruce_planks', 'jungle_planks', 'acacia_planks')
DOOR_TEXTURES = tuple(w+"_door_"+h for w in DOOR_WOODS for h in ("bottom","top"))
NAMES += list(VARIANT_PLANKS) + list(DOOR_TEXTURES)

def _read_definition(path):
    try:
        data = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise ValueError(f"cannot load definition {path}: {error}") from error
    if not isinstance(data, dict):
        raise ValueError(f"definition {path} must be an object")
    return data


DEFINITION_ROOT = Path(__file__).resolve().parents[1] / "assets/textures/definitions"


def _load_texture_definition(path=None):
    data = _read_definition(path or DEFINITION_ROOT / "textures.json")
    if data.get("version") != 2 or data.get("generator_version") != GENERATOR_VERSION or data.get("style") != STYLE_ID:
        raise ValueError("texture definitions do not match generator version/style")
    families = data.get("families", {})
    specs = data.get("family_specs", {})
    for name in NAMES:
        if name not in families or families[name] not in specs:
            raise ValueError(f"texture definitions missing family/spec for {name}")
    for family, spec in specs.items():
        for key in ("field_gain", "radius_scale", "anchor_scale"):
            value = spec.get(key)
            if not isinstance(value, (int, float)) or not math.isfinite(value) or not .1 <= value <= 3:
                raise ValueError(f"invalid {family}.{key}; expected .1..3")
    return data


TEXTURE_DEFINITION = _load_texture_definition()
TEXTURE_FAMILIES = {name: TEXTURE_DEFINITION["families"][name] for name in NAMES}


def _load_style_definition(path=None):
    data = _read_definition(path or DEFINITION_ROOT / "style.json")
    if data.get("generator_version") != GENERATOR_VERSION or data.get("id") != STYLE_ID:
        raise ValueError("style definition does not match generator version/style")
    roles = ("shadow", "base_dark", "base", "base_light", "highlight", "accent")
    if data.get("palette_roles") != list(roles):
        raise ValueError("style requires the six ordered semantic palette roles")
    limits = {"brightness": (.1, 2), "contrast": (.1, 2), "saturation": (0, 2),
              "shadow_floor": (0, 1), "highlight_ceiling": (0, 1), "temperature": (-.5, .5)}
    for key, (lo, hi) in limits.items():
        value = data.get("global", {}).get(key)
        if not isinstance(value, (int, float)) or not math.isfinite(value) or not lo <= value <= hi:
            raise ValueError(f"invalid style global.{key}")
    if data["global"]["shadow_floor"] >= data["global"]["highlight_ceiling"]:
        raise ValueError("shadow floor must be below highlight ceiling")
    biases = []
    for role in roles:
        spec = data.get("semantic_palette", {}).get(role, {})
        for key, bounds in (("lightness_bias", (-.5, .5)), ("chroma_scale", (0, 2)), ("hue_shift", (-1, 1))):
            value = spec.get(key)
            if not isinstance(value, (int, float)) or not math.isfinite(value) or not bounds[0] <= value <= bounds[1]:
                raise ValueError(f"invalid semantic_palette.{role}.{key}")
        biases.append(spec["lightness_bias"])
    if biases != sorted(biases):
        raise ValueError("semantic palette lightness biases must be ordered")
    thresholds=data.get("quantization", {}).get("default", [])
    if data.get("quantization", {}).get("mode")!="absolute_thresholds" or len(thresholds)!=5 or any(
            not isinstance(v,(int,float)) or not math.isfinite(v) for v in thresholds) or any(
            a>=b for a,b in zip(thresholds,thresholds[1:])):
        raise ValueError("quantization requires five increasing finite absolute thresholds")
    return data


def _load_entity_style_definitions(path=None):
    data = _read_definition(path or DEFINITION_ROOT / "entity_styles.json")
    if data.get("generator_version") != GENERATOR_VERSION or data.get("style") != STYLE_ID:
        raise ValueError("entity style definitions do not match generator version/style")
    if any(name not in data.get("entities", {}) for name in ENTITY_SKIN_NAMES):
        raise ValueError("entity style definitions are incomplete")
    return data


STYLE_DEFINITION = _load_style_definition()
ENTITY_STYLE_DEFINITIONS = _load_entity_style_definitions()

PALETTES = {
    "dirt": [(76,51,34,255),(89,59,38,255),(104,69,43,255),(119,80,49,255),(132,91,57,255),(145,102,66,255)],
    "stone": [(82,86,87,255),(94,98,99,255),(107,111,111,255),(120,124,123,255),(134,137,135,255),(148,150,147,255)],
    "sand": [(174,163,122,255),(188,176,132,255),(201,189,143,255),(214,202,155,255),(226,215,168,255),(236,226,181,255)],
    "grass_top": [(43,91,42,255),(50,104,45,255),(59,117,49,255),(69,130,54,255),(81,143,61,255),(95,154,69,255)],
    "grass_side": [(76,53,36,255),(91,62,40,255),(106,73,46,255),(121,84,52,255),(55,113,49,255),(72,135,57,255)],
    "oak_log": [(62,45,30,255),(74,52,33,255),(87,60,36,255),(101,70,41,255),(116,81,48,255),(131,93,56,255)],
    "oak_log_top": [(82,57,34,255),(98,68,38,255),(115,80,44,255),(133,94,52,255),(151,109,61,255),(169,125,72,255)],
    "oak_planks": [(94,66,39,255),(111,78,43,255),(129,91,49,255),(148,106,57,255),(167,123,68,255),(186,141,80,255)],
    "leaves": [(0,0,0,0),(32,78,37,255),(39,91,40,255),(48,105,44,255),(59,120,49,255),(73,133,56,255),(88,145,65,255)],
    "coal_ore": [(86,90,91,255),(102,106,106,255),(119,123,122,255),(34,35,36,255),(47,48,49,255),(62,63,63,255)],
    "copper_ore": [(86,90,91,255),(102,106,106,255),(119,123,122,255),(121,68,49,255),(174,101,67,255),(211,146,99,255)],
    "iron_ore": [(86,90,91,255),(102,106,106,255),(119,123,122,255),(151,119,98,255),(177,143,116,255),(202,169,140,255)],
}
EXTRA_BASES = {
    "bedrock":(58,59,62), "water":(47,92,156), "snow":(218,226,231),
    "deepslate":(54,58,64), "cactus_side":(57,111,57), "cactus_top":(76,128,64),
    "gold_ore":(190,145,50), "diamond_ore":(54,174,169),
    "emerald_ore":(35,181,92), "deepslate_emerald_ore":(31,145,78),
    "composter":(122,79,39), "fletching_table":(168,140,85),
    "loom":(165,139,98), "cauldron":(71,75,77),
    "blast_furnace":(78,82,84), "smithing_table":(64,88,89),
    "grindstone":(119,117,108), "lava":(211,80,25),
    "ice":(142,187,205), "gravel":(103,100,98), "clay":(133,146,156),
    "red_sand":(172,87,48), "terracotta":(142,76,55), "podzol_top":(91,65,42),
    "moss":(67,108,52), "tall_grass":(59,124,52), "flower":(175,68,83),
    "reeds":(112,145,70), "birch_log":(177,169,141), "birch_leaves":(74,127,58),
    "spruce_log":(72,55,39), "spruce_leaves":(43,82,61), "jungle_log":(96,70,44),
    "jungle_leaves":(48,118,48), "acacia_log":(119,76,53), "acacia_leaves":(78,115,55),
    "cobblestone":(105,107,105), "crafting_table":(132,88,49), "furnace":(93,95,94),
    "chest":(145,98,47), "torch":(195,133,48), "white_wool":(205,205,199),
    "white_bed":(205,197,191), "farmland":(91,62,40), "wet_farmland":(61,44,35),
    "wheat_young":(70,116,47), "wheat_middle":(128,128,48), "wheat_mature":(190,151,57),
    "oak_sapling":(58,120,50), "birch_sapling":(79,133,61), "spruce_sapling":(45,88,63),
    "jungle_sapling":(48,127,47), "acacia_sapling":(82,119,54),
    "snow_layer":(218,226,231), "fire":(221,91,24),
    "glass":(180,214,224), "tnt":(184,45,36), "obsidian":(39,27,55),
    "dandelion":(225,184,34), "blue_orchid":(58,151,196),
    "allium":(151,86,180), "oxeye_daisy":(221,220,195),
    "sunflower_bottom":(82,133,46), "sunflower_top":(224,169,28),
    "cloud":(220,225,229),
    "limestone":(184,181,163), "basalt":(48,50,53), "tuff":(79,91,83),
    "coarse_dirt":(104,72,42), "mud":(61,57,54),
    "packed_ice":(91,153,195), "black_sand":(49,46,49),
    "granite":(142,91,77),
    "aether_grass_top":(90,175,104), "aether_grass_side":(74,132,73),
    "aether_soil":(116,82,52), "cloudstone":(156,183,194),
    "sunstone":(214,166,72), "skyroot_log":(132,96,56),
    "skyroot_log_top":(171,132,76), "skyroot_leaves":(90,164,82),
    "star_crystal":(65,184,222), "starflower":(150,117,226),
    "cloud_bloom":(235,240,250), "glowshroom":(128,224,199),
    "dripstone_block":(123,87,59), "pointed_dripstone":(134,96,66),
    "calcite":(210,209,194), "hanging_roots":(101,72,43),
    "glow_fern":(75,190,133), "resonant_crystal":(80,177,226),
    "sulfur_crust":(199,177,46),
}
TRANSPARENT = {"tall_grass","flower","reeds","torch","wheat_young","wheat_middle",
               "wheat_mature","oak_sapling","birch_sapling","spruce_sapling",
               "jungle_sapling","acacia_sapling","fire","glass","dandelion",
               "blue_orchid","allium","oxeye_daisy","sunflower_bottom",
               "sunflower_top", "starflower", "cloud_bloom", "glowshroom",
               "hanging_roots", "glow_fern", "resonant_crystal"}

TRANSPARENT.update(list(HEAVEN_TEXTURES)[10:])

def _srgb_to_linear(value):
    value = value / 255.0
    return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4


def _linear_to_srgb(value):
    value = max(0.0, min(1.0, value))
    encoded = 12.92 * value if value <= 0.0031308 else 1.055 * (value ** (1.0 / 2.4)) - 0.055
    return int(round(max(0.0, min(1.0, encoded)) * 255.0))


def _srgb_to_oklab(rgb):
    r, g, b = (_srgb_to_linear(c) for c in rgb)
    l = 0.4122214708*r + 0.5363325363*g + 0.0514459929*b
    m = 0.2119034982*r + 0.6806995451*g + 0.1073969566*b
    s = 0.0883024619*r + 0.2817188376*g + 0.6299787005*b
    l, m, s = math.copysign(abs(l) ** (1.0/3.0), l), math.copysign(abs(m) ** (1.0/3.0), m), math.copysign(abs(s) ** (1.0/3.0), s)
    return (0.2104542553*l + 0.7936177850*m - 0.0040720468*s,
            1.9779984951*l - 2.4285922050*m + 0.4505937099*s,
            0.0259040371*l + 0.7827717662*m - 0.8086757660*s)


def _oklab_to_srgb(lab):
    l, a, b = lab
    l_ = l + 0.3963377774*a + 0.2158037573*b
    m_ = l - 0.1055613458*a - 0.0638541728*b
    s_ = l - 0.0894841775*a - 1.2914855480*b
    l, m, s = l_**3, m_**3, s_**3
    return tuple(_linear_to_srgb(value) for value in (
        4.0767416621*l - 3.3077115913*m + 0.2309699292*s,
        -1.2684380046*l + 2.6097574011*m - 0.3413193965*s,
        -0.0041960863*l - 0.7034186147*m + 1.7076147010*s))


def _role_palette(base, transparent=False, shadow_floor=0.34, chroma_scale=0.92,
                  hue_shift=0.0, levels=6):
    """Build a role-based palette in OKLCH, with a soft shadow floor.

    The old generator subtracted equal RGB amounts, which made shadows lose
    hue and pushed ordinary materials toward black.  Keeping the conversion
    here dependency-free makes the style deterministic on every build host.
    """
    l, a, b = _srgb_to_oklab(base)
    knobs = STYLE_DEFINITION["global"]
    chroma = math.hypot(a, b) * chroma_scale * knobs["saturation"]
    hue = math.atan2(b, a) + hue_shift + knobs["temperature"]
    l *= knobs["brightness"]
    low = max(0.0, min(.90, shadow_floor + knobs["shadow_floor"] - .34))
    high = max(low + .09, knobs["highlight_ceiling"])
    # Shift the center rather than clipping every highlight to the same color.
    roles = [STYLE_DEFINITION["semantic_palette"][role]
             for role in STYLE_DEFINITION["palette_roles"]]
    span = (roles[-1]["lightness_bias"] - roles[0]["lightness_bias"]) * knobs["contrast"]
    center = max(low + span / 2, min(high - span / 2, l))
    colors = []
    for index in range(levels):
        position = index * 5 / max(1, levels - 1)
        first = min(4, int(position)); fraction = position - first
        role = {key: roles[first][key] * (1-fraction) + roles[first+1][key] * fraction
                for key in ("lightness_bias", "chroma_scale", "hue_shift")}
        lightness = max(low, min(high, center + role["lightness_bias"] * knobs["contrast"]))
        c = chroma * role["chroma_scale"]
        angle = hue + role["hue_shift"]
        rgb = _oklab_to_srgb((lightness, c * math.cos(angle), c * math.sin(angle)))
        colors.append(tuple(max(2, min(248, value)) for value in rgb) + (255,))

    return ([(0, 0, 0, 0)] + colors) if transparent else colors


def muted_palette(base, transparent=False):
    # Compatibility name retained for callers and old local definitions.  It
    # now uses the bright-comfortable role palette rather than RGB subtraction.
    return _role_palette(base, transparent)


_ENTITY_BASES = {
    "cow": (145, 101, 64), "pig": (211, 121, 132),
    "sheep": (197, 193, 181), "chicken": (222, 216, 197),
    "zombie": (62, 119, 82), "skeleton": (185, 179, 155),
    "spider": (72, 41, 36), "blastling": (75, 148, 60),
    "item": (174, 116, 39), "villager": (157, 105, 66),
    "zombie_villager": (72, 119, 73), "iron_golem": (185,187,168), "player": (76, 111, 145),
}
for _name, _base in _ENTITY_BASES.items():
    _entity_palette = _role_palette(_base, shadow_floor=0.30,
                                    chroma_scale=1.03, levels=6)
    ENTITY_PALETTES[_name] = tuple(_entity_palette)

# Bright-comfortable anchors.  The procedural structure remains seed-driven,
# while these anchors give each material family a stable, readable identity.
_BRIGHT_BASES = {
    "dirt": (143, 107, 72), "stone": (151, 155, 153),
    "sand": (225, 210, 163), "grass_top": (102, 161, 76),
    "grass_side": (102, 78, 48), "oak_log": (135, 100, 62),
    "oak_log_top": (179, 140, 87), "oak_planks": (181, 140, 88),
    # Species anchors deliberately stay away from the grass-top green. Oak is
    # a deeper forest green, birch keeps a warm olive/brown undertone, and
    # jungle foliage is the most saturated tropical green.
    "leaves": (54, 126, 55), "birch_leaves": (79, 112, 53),
    "jungle_leaves": (20, 175, 62), "coal_ore": (128, 132, 132),
    "copper_ore": (128, 132, 132), "iron_ore": (128, 132, 132),
}
for _name, _base in _BRIGHT_BASES.items():
    _floor = 0.30 if _name in {"coal_ore", "oak_log"} else 0.34 \
        if _name in {"leaves", "birch_leaves"} else 0.36
    _shift = 0.04 if _name in {"grass_top", "leaves"} else -0.035 \
        if _name == "birch_leaves" else 0.015 if _name == "jungle_leaves" else 0.0
    _chroma = 1.00 if _name == "jungle_leaves" else 0.96 \
        if _name == "birch_leaves" else 0.92
    PALETTES[_name] = _role_palette(
        _base, _name == "leaves" or _name.endswith("_leaves"), shadow_floor=_floor,
        chroma_scale=_chroma, hue_shift=_shift)

for _name, _base in EXTRA_BASES.items():
    if _name in _BRIGHT_BASES:
        continue
    PALETTES[_name] = muted_palette(_base, _name in TRANSPARENT or _name.endswith("_leaves"))

# Keep naturally dark materials dark; lift the midtones of ordinary surfaces.
for _name, _base in EXTRA_BASES.items():
    if _name not in _BRIGHT_BASES and _name not in {
            "deepslate", "black_sand", "basalt", "mud", "obsidian",
            "bedrock", "water", "lava"}:
        _l, _a, _b = _srgb_to_oklab(_base)
        PALETTES[_name] = _role_palette(_oklab_to_srgb((min(.88, _l+.055), _a, _b)),
            _name in TRANSPARENT or _name.endswith("_leaves"))
for _name, _base in zip(EXTRA_LOG_TOPS,
                       ((208,183,128), (158,119,77), (185,143,96), (196,126,79))):
    PALETTES[_name] = _role_palette(_base)
for _name in VILLAGE_FUNCTIONAL:
    PALETTES[_name]=_role_palette((145,113,78))
for _name in FUNCTIONAL + VILLAGE_FUNCTIONAL:
    for _face in ("top", "side", "bottom"):
        PALETTES[_name+"_"+_face] = PALETTES[_name]

for _name, _base in DECORATION_BASES.items():
    _floor = 0.24 if _name in {"black_wool", "polished_basalt", "deepslate_bricks"} else 0.34
    PALETTES[_name] = _role_palette(_base, shadow_floor=_floor)

for _name, _base in BIOME_BASES.items():
    PALETTES[_name] = _role_palette(_base)
for _name, _base in ECOLOGY_BASES.items():
    PALETTES[_name] = _role_palette(_base)
PALETTES["dry_grass_side"] = PALETTES["dirt"][:4] + PALETTES["dry_grass_block"][2:4]
PALETTES["leaf_litter_side"] = PALETTES["dirt"][:4] + PALETTES["leaf_litter_soil"][2:4]

PALETTES["mossy_stone_bricks"] = PALETTES["stone_bricks"][:4] + PALETTES["moss"][2:4]

# Grass-side tiles need two semantic materials in one six-role palette.  The
# first four roles are soil; the last two are living turf.  Keeping this
# explicit also prevents bright soil highlights from being mistaken for grass
# by the side-face generator.
for _side_name, _soil_name, _top_name in (
        ("grass_side", "dirt", "grass_top"),
        ("aether_grass_side", "aether_soil", "aether_grass_top")):
    _soil = PALETTES[_soil_name]
    _turf = PALETTES[_top_name]
    PALETTES[_side_name] = [_soil[1], _soil[2], _soil[3], _soil[4],
                            _turf[2], _turf[3]]
PALETTES["fire"] = [(0,0,0,0),(116,34,17,255),(164,48,17,255),(207,67,17,255),
                    (235,101,20,255),(245,151,35,255),(250,205,76,255)]
for _name, _ore in {
    "coal_ore": ("coal", (42, 45, 47)),
    "copper_ore": ("copper", (184, 104, 72)),
    "iron_ore": ("iron", (213, 179, 147)),
    "gold_ore": ("gold", (218, 164, 55)),
    "diamond_ore": ("diamond", (57, 190, 181)),
    "emerald_ore": ("emerald", (42, 202, 112)),
    "deepslate_emerald_ore": ("emerald", (42, 202, 112)),
}.items():
    _ore_palette = _role_palette(_ore[1], shadow_floor=0.26,
                                 chroma_scale=1.12, levels=6)
    PALETTES[_name] = PALETTES["stone"][2:5] + _ore_palette[3:]
PALETTES["coal_ore"] = PALETTES["stone"][2:5] + [
    (24, 27, 29, 255), (42, 45, 47, 255), (64, 67, 68, 255)]
PALETTES["copper_ore"] = PALETTES["stone"][2:5] + [
    (82, 51, 40, 255), (151, 83, 58, 255), (225, 143, 97, 255)]

PALETTES["deepslate_emerald_ore"] = PALETTES["deepslate"][2:5] + \
    PALETTES["deepslate_emerald_ore"][3:]
HIGH_CONTRAST_NAMES = {"coal_ore","copper_ore","iron_ore","gold_ore","diamond_ore",
                       "emerald_ore","deepslate_emerald_ore","fire"}
NATURAL = {"dirt","grass_top","stone","sand","bedrock","deepslate","gravel","clay",
           "red_sand","terracotta","podzol_top","moss","snow","snow_layer","cobblestone",
           "cloud","limestone","basalt","tuff","coarse_dirt","mud",
           "packed_ice","black_sand","granite", "aether_grass_top",
           "aether_soil", "cloudstone", "sunstone",
           "skyroot_log", "star_crystal", "dripstone_block",
           "pointed_dripstone", "calcite", "sulfur_crust"}
DIRECTIONAL = {"oak_planks","oak_log","birch_log","spruce_log","jungle_log","acacia_log",
               "skyroot_log",
               "farmland","wet_farmland","cactus_side","reeds",
               "grass_side","aether_grass_side"}
LEAF_NAMES = {"leaves","birch_leaves","spruce_leaves","jungle_leaves","acacia_leaves",
              "skyroot_leaves"}

DOOR_COLORS = {"oak":(181,140,88), "birch":(213,197,136),
    "spruce":(112,78,43), "jungle":(176,119,86), "acacia":(197,99,56),
    "skyroot":(191,154,106), "iron":(197,206,211)}
for _name in VARIANT_PLANKS:
    PALETTES[_name] = _role_palette(DOOR_COLORS[_name.split("_")[0]])
for _name in DOOR_TEXTURES:
    PALETTES[_name] = _role_palette(DOOR_COLORS[_name.split("_")[0]], transparent=True)
TRANSPARENT.update(DOOR_TEXTURES)
DIRECTIONAL.update(VARIANT_PLANKS)

def mix64(v):
    v=(v+0x9E3779B97F4A7C15)&0xffffffffffffffff
    v=((v^(v>>30))*0xBF58476D1CE4E5B9)&0xffffffffffffffff
    v=((v^(v>>27))*0x94D049BB133111EB)&0xffffffffffffffff
    return v^(v>>31)

@lru_cache(maxsize=4096)
def name_hash(name):
    # FNV-1a over every UTF-8 byte, followed by avalanche; never Python hash().
    value = 0xcbf29ce484222325
    for byte in name.encode("utf-8"):
        value = ((value ^ byte) * 0x100000001b3) & 0xffffffffffffffff
    return mix64(value)


def sample(seed,name,x,y,channel=0):
    return mix64(seed ^ name_hash(name) ^ (x*0x8DA6B343) ^
                 (y*0xD8163841) ^ (channel*0xA24BAED4963EE407))

def rand01(seed,name,x,y,channel=0): return sample(seed,name,x,y,channel)/0xffffffffffffffff
def wrap(v): return v%SIZE
def torus_delta(a,b):
    d=(a-b)%SIZE
    return d-SIZE if d>SIZE/2 else d
def neighbors4(x,y): return ((wrap(x-1),y),(wrap(x+1),y),(x,wrap(y-1)),(x,wrap(y+1)))
def neighbors8(x,y):
    return tuple((wrap(x+dx),wrap(y+dy)) for dy in (-1,0,1) for dx in (-1,0,1) if dx or dy)

def macro_field(seed,name,count=5):
    """Irregular radial influence on a torus; no enlarged low-res rectangles."""
    family = TEXTURE_FAMILIES.get(name)
    spec = TEXTURE_DEFINITION["family_specs"].get(family, {})
    count = max(1, round(count * spec.get("anchor_scale", 1.0)))
    anchors=[]
    for i in range(count):
        h=sample(seed,name,i,31,1)
        anchors.append((h%SIZE,(h>>8)%SIZE,(1.8+((h>>16)%25)/10.0)*spec.get("radius_scale", 1.0),((h>>24)%201-100)/100.0))
    field=[]
    for y in range(SIZE):
        for x in range(SIZE):
            value=0.0; weight=0.0
            for ax,ay,r,tone in anchors:
                dx,dy=torus_delta(x,ax),torus_delta(y,ay)
                # Skewed radial kernels break axis symmetry without block scaling.
                dist=math.sqrt((dx+0.23*dy)**2+(dy-0.17*dx)**2)
                w=max(0.0,1.0-dist/r)**2
                value+=tone*w; weight+=w
            field.append(value/max(0.45,weight)*spec.get("field_gain", 1.0))
    return field

def grow_blob(seed,name,anchor,size,channel=0,elongation=None):
    cells={anchor}; frontier=[anchor]
    while len(cells)<size:
        choices=[]
        for x,y in frontier:
            # Four-connected growth guarantees that a visual cluster remains a
            # single cluster under the validator; diagonal steps may still be
            # introduced by a material's later detail pass.
            for nx,ny in neighbors4(x,y):
                if (nx,ny) not in cells:
                    dx,dy=torus_delta(nx,anchor[0]),torus_delta(ny,anchor[1])
                    penalty=abs(dx)+abs(dy)
                    if elongation:
                        penalty=abs(dx)*elongation[0]+abs(dy)*elongation[1]
                    score=rand01(seed,name,nx,ny,channel)-penalty*0.035
                    choices.append((score,nx,ny))
        if not choices: break
        _,nx,ny=max(choices)
        cells.add((nx,ny)); frontier.append((nx,ny))
        if len(frontier)>max(4,size//2): frontier.pop(0)
    return cells

def quantize(values, levels=6, thresholds=None):
    """Absolute thresholds preserve quiet planes; never stretch tiny noise."""
    if thresholds is None:
        thresholds = tuple(STYLE_DEFINITION.get("quantization", {}).get(
            "default", (-.65, -.28, .08, .42, .78)))
    if levels != len(thresholds)+1:
        raise ValueError("quantizer thresholds must match palette size")
    return [sum(value > threshold for threshold in thresholds) for value in values]


def add_sparse_micro(indices,seed,name,amount=18,low=0,high=5):
    # Accents occur as adjacent pairs/turns, never independent white noise.
    for i in range(amount//2):
        h=sample(seed,name,i,77,3); x=h%SIZE; y=(h>>8)%SIZE
        dx,dy=((1,0),(1,1),(0,1),(-1,1))[((h>>16)&3)]
        delta=1 if (h>>20)&1 else -1
        for step in range(2+(1 if i%7==0 else 0)):
            p=wrap(y+dy*step)*SIZE+wrap(x+dx*step)
            indices[p]=max(low,min(high,indices[p]+delta))

def generate_dirt(name,seed):
    values=macro_field(seed,name,6)
    # Pebbly soil clods: compact irregular components at several scales.
    for i in range(6):
        h=sample(seed,name,i,41); blob=grow_blob(seed,name,(h%SIZE,(h>>8)%SIZE),3+(h>>16)%7,i)
        tone=(-0.30,0.23,0.40)[i%3]
        for x,y in blob: values[y*SIZE+x]+=tone
    out=quantize(values); add_sparse_micro(out,seed,name,6)
    return out

def generate_grass(seed):
    values=[v*0.55 for v in macro_field(seed,"grass_top",6)]
    # 3-6 toroidal tufts, with bent 2-4 pixel blades in varied directions.
    tuft_count=3+sample(seed,"grass_top",4,8)%4
    for i in range(tuft_count):
        h=sample(seed,"grass_top",i,53); anchor=(h%SIZE,(h>>8)%SIZE)
        blob=grow_blob(seed,"grass_top",anchor,7+(h>>16)%9,i)
        for x,y in blob:
            dx,dy=torus_delta(x,anchor[0]),torus_delta(y,anchor[1])
            values[y*SIZE+x]+=0.38-0.025*(abs(dx)+abs(dy))+(-0.18 if dx+dy>2 else 0.10)
        direction=((1,-1),(1,0),(-1,-1),(0,-1),(1,1),(-1,0))[i%6]
        length=2+(h>>24)%3; x,y=anchor
        for step in range(length):
            if step==2 and ((h>>29)&1): direction=(direction[0],-direction[1])
            x,y=wrap(x+direction[0]),wrap(y+direction[1])
            values[y*SIZE+x]+=0.48-0.07*step
    out=quantize(values); add_sparse_micro(out,seed,"grass_top",4)
    return out

def generate_stone(name,seed):
    values=[v*.35 for v in macro_field(seed,name,5)]
    if name in {"clay", "terracotta", "cloudstone"}:
        return quantize(values, thresholds=(-.7,-.3,.025,.3,.7))
    for i in range(6 if name != "gravel" else 9):
        h=sample(seed,name,i,61)
        blob=grow_blob(seed,name,(h%SIZE,(h>>8)%SIZE),10+(h>>16)%15,i)
        for x,y in blob:
            values[y*SIZE+x] += (-.30,.24,.43)[i%3]
    if name in {"deepslate", "basalt", "limestone"}:
        for y in range(SIZE):
            for x in range(SIZE):
                values[y*SIZE+x] += .15*math.sin(y*math.tau/8 + .4*math.sin(x*math.tau/16))
    if name == "cobblestone":
        # Offset masonry cells, with thin mortar instead of random fractures.
        return [1 if y%8==0 or (x+(y//8)*4)%8==0 else
                3+(1 if y%8==1 else 0)-(1 if y%8==7 else 0)
                for y in range(SIZE) for x in range(SIZE)]
    return quantize(values, thresholds=(-.65,-.26,.025,.34,.72))


def generate_sand(name,seed):
    values=[v*.18 for v in macro_field(seed,name,6)]
    out=quantize(values, thresholds=(-.6,-.25,.018,.28,.6))
    add_sparse_micro(out,seed,name,4)
    return out


def generate_wood_side(name,seed):
    phase=(sample(seed,name,1,1)%628)/100.0
    out=[]
    for y in range(SIZE):
        for x in range(SIZE):
            bend=.5*math.sin(y*math.tau/SIZE+phase)
            fiber=math.sin((x+bend)*math.tau/8+phase)
            out.append(2 if fiber<-.65 else 4 if fiber>.8 else 3)
    if name == "birch_log":
        for i in range(4):
            h=sample(seed,name,i,71); x=h%SIZE; y=(h>>8)%SIZE
            for step in range(2+(h>>16)%3): out[y*SIZE+wrap(x+step)]=0
    if name != "birch_log":
        for i in range(4):
            h=sample(seed,name,i,73); x=h%SIZE; y=(h>>8)%SIZE
            for step in range(3+(h>>16)%3):
                out[wrap(y+step)*SIZE+wrap(x+(step//3))]=2
    return out


def generate_log_top(name,seed):
    h=sample(seed,name,2,4); cx=7+(h%3-1)*.35; cy=7+((h>>8)%3-1)*.35
    out=[]
    for y in range(SIZE):
        for x in range(SIZE):
            radius=math.sqrt((x-cx)**2+((y-cy)*.95)**2)
            if x in (0,15) or y in (0,15): index=1
            else: index=2 if radius%3<.65 else 4 if radius%3<1.2 else 3
            out.append(index)
    return out


def generate_planks(name,seed):
    phase=sample(seed,name,0,0)%16
    out=[]
    for y in range(SIZE):
        for x in range(SIZE):
            joint=(x+(8 if y>=8 else 0))%16==4
            grain=math.sin((x+phase)*math.tau/16 + .5*math.sin(y*math.tau/8))
            out.append(1 if y%8==4 or joint else 4 if y%8==5 else
                       2 if grain<-.8 and y%4==3 else 3)
    for i in range(5):
        h=sample(seed,name,i,79); x=h%SIZE; y=(h>>8)%SIZE
        for step in range(3+(h>>16)%4):
            p=y*SIZE+wrap(x+step)
            if out[p]==3: out[p]=2 if i%2 else 4
    return out

def generate_leaves(name,seed):
    # Overlapping leaf groups have lit tips and shaded undersides. Keep the
    # spaces between them clustered so cutout alpha survives minification.
    out=[3 if v<.1 else 4 for v in macro_field(seed,name,6)]
    elongation=(1.8,.65) if name=="spruce_leaves" else (.8,1.2)
    for i in range(11):
        h=sample(seed,name,i,81)
        blob=grow_blob(seed,name,(h%SIZE,(h>>8)%SIZE),7+(h>>16)%9,i,elongation)
        for x,y in blob:
            upper=(x,wrap(y-1)) not in blob
            lower=(wrap(x+1),y) not in blob or (x,wrap(y+1)) not in blob
            out[y*SIZE+x]=4 if upper else 2 if lower else 3
    holes=set()
    for i in range(4):
        h=sample(seed,name,i,83)
        holes |= grow_blob(seed,name,(h%SIZE,(h>>8)%SIZE),10+(h>>16)%4,i)
    return [0 if (x,y) in holes else 1+out[y*SIZE+x]
            for y in range(SIZE) for x in range(SIZE)]

def ore_clusters(name,seed):
    count=2+sample(seed,name,7,13)%3; clusters=[]; occupied=set()
    for i in range(count):
        h=sample(seed,name,i,89); anchor=(h%SIZE,(h>>8)%SIZE)
        for attempt in range(16):
            blob=grow_blob(seed,name,anchor,5+(h>>16)%8,i, (1.0,1.15))
            if not any(n in occupied for p in blob for n in neighbors4(*p)):
                break
            anchor=(wrap(anchor[0]+3),wrap(anchor[1]+5))
        occupied|=blob; clusters.append((anchor,blob))
    return clusters

def generate_ore(name,seed):
    base=generate_stone("stone",seed^sample(seed,name,0,0))
    out=[min(2,i//2) for i in base]
    for anchor,blob in ore_clusters(name,seed):
        for x,y in blob:
            dx,dy=torus_delta(x,anchor[0]),torus_delta(y,anchor[1])
            out[y*SIZE+x]=5 if dx+dy<0 else (4 if (x+y)%3 else 3)
    return out

def generate_generic(name,seed):
    if name in {"dirt","podzol_top","coarse_dirt","mud","aether_soil"}: return generate_dirt(name,seed)
    if name in {"grass_top","moss","aether_grass_top"}: return generate_grass(seed^sample(seed,name,0,0))
    if name in {"stone","bedrock","deepslate","clay","terracotta","cobblestone",
                "limestone","basalt","tuff","granite","gravel","cloudstone",
                "dripstone_block","pointed_dripstone","calcite","sulfur_crust"}: return generate_stone(name,seed)
    if name in {"sand","red_sand","black_sand","snow","snow_layer","packed_ice"}: return generate_sand(name,seed)
    if name.endswith("_ore"): return generate_ore(name,seed)
    if name in LEAF_NAMES: return generate_leaves(name,seed)
    if name.endswith("_log"): return generate_wood_side(name,seed)
    if name.endswith("_log_top"): return generate_log_top(name,seed)
    if name.endswith("_planks"): return generate_planks(name,seed)
    field=quantize(macro_field(seed,name,6))
    add_sparse_micro(field,seed,name,2)
    return field

PLANTS={"tall_grass","flower","reeds","torch","wheat_young","wheat_middle","wheat_mature",
        "oak_sapling","birch_sapling","spruce_sapling","jungle_sapling","acacia_sapling",
        "dandelion","blue_orchid","allium","oxeye_daisy","sunflower_bottom","sunflower_top",
        "starflower","cloud_bloom","glowshroom","hanging_roots",
        "glow_fern","resonant_crystal"}

PLANTS.update(BIOME_PLANTS)
PLANTS.update(ECOLOGY_PLANTS)
PLANTS.update(list(HEAVEN_TEXTURES)[10:])

def generate_special(name,seed,indices):
    if name in {"grass_side", "aether_grass_side"}:
        soil_name = "dirt" if name == "grass_side" else "aether_soil"
        # Compress the soil generator into the four soil-only palette roles;
        # indices 4/5 are reserved exclusively for the turf cap below.
        indices=[max(0,min(3,value-1)) for value in generate_dirt(soil_name,seed)]
        # Five solid rows make the grass cap unmistakable at gameplay scale;
        # the lower 1-3 rows form a deterministic, irregular rooted edge.
        heights=[5+round(math.sin(x*math.tau/16+seed%7)) for x in range(SIZE)]
        turf=generate_grass(seed)
        for x in range(SIZE):
            for y in range(heights[x]+1): indices[y*SIZE+x]=4+(turf[y*SIZE+x]>2)
    elif name=="fire":
        indices=[0]*256
        for y in range(15):
            center=7+round(math.sin((y+seed%9)*.9)); half=max(1,5-y//3)
            for x in range(center-half,center+half+1): indices[y*SIZE+wrap(x)]=1+min(5,y//3)
    elif name in {"water","lava","ice"}:
        indices=quantize([.28*math.sin(y*math.tau/8 + .65*math.sin(x*math.tau/16+seed%7))
                          for y in range(SIZE) for x in range(SIZE)],
                         thresholds=(-.8,-.4,-.08,.18,.6))
    elif name in {"farmland","wet_farmland"}:
        base=generate_dirt(name,seed); indices=[0 if (x+round(math.sin(y*.7)))%5==0 else max(1,v) for y in range(SIZE) for x,v in enumerate(base[y*SIZE:(y+1)*SIZE])]
    elif name=="white_wool":
        indices=quantize(macro_field(seed,name,5), thresholds=(-.7,-.4,-.05,.4,.7))
    elif name in {"cactus_side","reeds"}:
        indices=[1+((x+round(math.sin(y*.7)))%5) for y in range(SIZE) for x in range(SIZE)]
    elif name=="glass":
        indices=[]
        for y in range(SIZE):
            for x in range(SIZE):
                border=x in (0,15) or y in (0,15)
                glint=(x-y) in (-1,0,1) and 3<=x<=7
                indices.append(3+(x+y)%3 if border else 2 if glint else 0)
    return indices

def functional_palette(base):
    wood = base in {"crafting_table","chest","composter","fletching_table","loom"}
    anchor = (179,137,83) if wood else (142,148,147)
    if base == "smithing_table": anchor=(100,115,118)
    if base == "tnt": anchor=(195,76,55)
    if base == "white_bed": anchor=(214,221,211)
    palette = _role_palette(anchor)
    # Palette entries describe structure: cavity, seam, base, light, rim, accent.
    palette[0] = (65,49,34,255) if wood else (53,61,63,255)
    palette += [(223,211,176,255), (168,118,55,255)]
    return palette


def generate_functional_texture(name, seed):
    base = next(base for base in FUNCTIONAL + VILLAGE_FUNCTIONAL if name == base or name.startswith(base+"_"))
    face = name[len(base)+1:] if name != base else "front"
    palette=PALETTES[name]
    wood=base in {"crafting_table","chest","composter","fletching_table","loom"}
    tile=[palette[3]]*256
    def rect(x0,y0,x1,y1,index):
        _paint_rect(tile,x0,y0,x1,y1,palette[index])
    def line(x0,y0,x1,y1,index):
        _line(tile,x0,y0,x1,y1,palette[index])
    # All outside edges match; semantic marks remain inside a tile.
    rect(0,0,16,1,2); rect(0,15,16,16,2)
    rect(0,0,1,16,2); rect(15,0,16,16,2)
    if face == "bottom":
        rect(2,2,14,14,2); rect(3,3,13,13,3)
        if wood: line(7,3,7,12,1)
        return tile
    if base == "crafting_table":
        if face == "top":
            rect(2,2,14,14,6)
            for v in (2,6,10,13): line(v,2,v,13,1); line(2,v,13,v,1)
        else:
            rect(2,2,14,5,4); rect(2,6,4,14,1); rect(12,6,14,14,1)
            line(6,7,6,12,7); line(5,7,9,7,0)
            line(10,9,10,12,7); line(9,8,11,8,5)
    elif base in {"furnace","blast_furnace"}:
        if face == "front":
            rect(2,3,14,7,0); rect(3,3,13,4,1)
            rect(2,9,14,14,0); rect(3,13,13,14,4)
            if base == "blast_furnace":
                for x in (4,7,10): rect(x,3,x+1,7,4); rect(x,9,x+1,13,2)
            else: rect(4,10,12,11,1)
        elif face == "top":
            rect(3,3,13,13,2); rect(4,4,12,12,3)
            if base == "blast_furnace":
                for x in (5,8,11): line(x,4,x,11,0)
        else:
            line(1,7,14,7,1); line(7,1,7,6,2); line(10,8,10,14,2)
    elif base == "chest":
        rect(1,1,15,3,1); rect(1,13,15,15,1)
        rect(1,3,3,13,1); rect(13,3,15,13,1)
        if face != "top":
            line(3,6,12,6,0)
            if face == "front": rect(7,5,9,10,6); rect(7,7,9,9,7)
        else: line(7,3,7,12,2)
    elif base in {"composter","cauldron"}:
        if face == "top":
            rect(2,2,14,14,5); rect(4,4,12,12,0); rect(5,5,11,11,1)
        else:
            rect(2,2,14,4,5); rect(3,12,13,14,1)
            if base == "composter":
                for x in (4,8,12): line(x,4,x,11,1)
                line(2,9,13,9,7)
            else: rect(3,4,13,12,2); rect(4,4,6,11,3)
    elif base == "fletching_table":
        if face == "top":
            rect(2,2,14,14,6)
            line(4,11,11,4,7); line(9,4,12,4,0); line(12,4,12,7,0)
            line(3,9,5,11,4); line(4,8,6,10,4)
        else:
            rect(2,2,14,4,6)
            for x in (5,10): line(x,6,x,12,7); line(x-1,6,x+1,6,0)
    elif base == "loom":
        if face == "top":
            for x in (3,6,9,12): line(x,2,x,13,6)
            line(2,5,13,5,1); line(2,11,13,11,1)
        else:
            rect(2,2,14,14,1); rect(4,3,12,12,6)
            for x in (5,7,9,11): line(x,3,x,11,4)
            rect(4,9,12,12,7); line(2,12,13,12,5)
    elif base == "smithing_table":
        if face == "top":
            rect(2,2,14,14,1); rect(3,3,13,13,2)
            line(4,5,10,11,5); line(9,4,11,6,4)
        else:
            rect(2,3,14,6,0); rect(3,7,13,13,7)
            line(5,8,5,11,0); line(9,8,11,8,5); line(10,8,10,11,1)
    elif base in {"barrel", "lectern", "cartography_table", "brewing_stand", "smoker", "stonecutter"}:
        for y in range(SIZE):
            for x in range(SIZE):
                # Distinct work surfaces: hoops, book leaves, chart grid, bottles,
                # smoking vent and the stone-cutting wheel.
                motif = ((x in (2,13)) if base == "barrel" else
                         (y in (3,4,10,11) and 3 <= x <= 12) if base == "lectern" else
                         (x % 5 == 0 or y % 5 == 0) if base == "cartography_table" else
                         (x in (4,8,12) and 4 <= y <= 12) if base == "brewing_stand" else
                         (4 <= x <= 11 and y in (6,8,10)) if base == "smoker" else
                         (x-8)**2+(y-8)**2 <= 20)
                if motif: tile[y*SIZE+x] = palette[0]
    elif base == "grindstone":
        if face == "top":
            rect(4,1,12,15,2); rect(5,1,7,15,4)
        else:
            for y in range(3,13):
                for x in range(3,13):
                    r=(x-7.5)**2+(y-7.5)**2
                    if r<25: tile[y*16+x]=palette[4 if r>14 else 3]
            rect(7,7,9,9,0); rect(2,12,5,14,7); rect(11,12,14,14,7)
    elif base == "white_bed":
        # Pillow is already separate geometry. The mattress is plain linen.
        if face == "top":
            line(2,3,2,12,4); line(13,3,13,12,2)
        else: line(2,5,13,5,4); line(2,11,13,11,2)
    elif base == "tnt":
        if face == "top":
            for x in (3,7,11):
                rect(x,3,x+2,13,1); rect(x,3,x+1,13,4)
            rect(7,6,9,9,0); line(8,6,10,4,6)
        else:
            for x in (3,7,11): line(x,2,x,13,1)
            rect(1,5,15,11,6)
            # Hand-drawn 3x5 TNT label fits the native pixel grid.
            for ox,letter in ((2,"T"),(6,"N"),(10,"T")):
                for y,row in enumerate({"T":("111","010","010","010","010"),
                                        "N":("101","111","111","111","101")}[letter]):
                    for x,v in enumerate(row):
                        if v=="1": tile[(5+y)*16+ox+x]=palette[0]
    return tile


def plant_palette(name):
    palette=[(0,0,0,0),(65,111,44,255),(94,147,57,255),(132,175,77,255),
             (179,143,65,255),(227,194,101,255),(235,224,184,255),(177,100,77,255)]
    flower_colors={"flower":(219,102,125),"dandelion":(245,206,72),
                   "blue_orchid":(104,180,228),"allium":(191,135,214),
                   "oxeye_daisy":(238,237,220),"starflower":(167,213,239),
                   "cloud_bloom":(234,225,243),"sunflower_top":(242,195,53),
                   "glowshroom":(145,210,203)}
    if name in flower_colors: palette[6]=flower_colors[name]+(255,)
    if name in BIOME_PLANTS:
        if name in {"dead_bush", "dry_grass"}:
            palette[1:4] = [(110,85,42,255),(157,126,61,255),(196,177,98,255)]
        colors = {"red_mushroom":(203,70,54), "brown_mushroom":(145,104,71),
                  "lavender":(167,125,201), "bellflower":(231,232,221),
                  "alpine_flower":(169,197,224), "tropical_flower":(231,134,77)}
        if name in colors: palette[6] = colors[name] + (255,)
    if name in ECOLOGY_PLANTS:
        colors = {"heather":(192,133,182), "wild_mint":(179,210,181),
            "desert_flower":(240,168,79), "reed_flower":(212,193,153),
            "clover":(225,227,195), "cave_glowshroom":(133,226,183)}
        if name in colors: palette[6] = colors[name] + (255,)
        if name == "wild_mint": palette[1:4] = [(48,112,85,255),(75,160,121,255),(121,190,143,255)]
        if name == "tundra_moss": palette[1:4] = [(86,104,65,255),(139,155,99,255),(184,192,133,255)]
        if name == "jungle_fern": palette[1:4] = [(25,91,50,255),(33,140,69,255),(76,178,100,255)]
        if name == "fallen_twigs": palette[1:4] = [(90,64,41,255),(142,103,64,255),(185,143,96,255)]
        if name in {"amethyst_cluster", "quartz_cluster"}:
            base = (163,119,198) if name == "amethyst_cluster" else (217,205,184)
            palette = [(0,0,0,0)] + _role_palette(base)
    return palette


for _name in FUNCTIONAL + VILLAGE_FUNCTIONAL:
    for _suffix in ("", "_top", "_side", "_bottom"):
        PALETTES[_name+_suffix]=functional_palette(_name)
for _name in PLANTS:
    PALETTES[_name]=plant_palette(_name)


def generate_plant_texture(name,seed):
    palette=PALETTES[name]
    image=[palette[0]]*256
    def line(x0,y0,x1,y1,index,width=1): _line(image,x0,y0,x1,y1,palette[index],width)
    def rect(x0,y0,x1,y1,index): _paint_rect(image,x0,y0,x1,y1,palette[index])
    # Draw growth bottom-up, then return PNG top-left rows for the atlas loader.
    if name in ECOLOGY_PLANTS:
        if name in {"amethyst_cluster", "quartz_cluster"}:
            for x,height in ((3,7),(7,14),(11,10)):
                rect(x,0,x+3,height-2,2); line(x+1,0,x+1,height-1,4)
                line(x,height-2,x+1,height,5); line(x+1,height,x+2,height-2,3)
        elif name == "cave_glowshroom":
            for x,h in ((5,8),(10,12)):
                rect(x,0,x+2,h-3,3); rect(x-3,h-3,x+4,h,6)
                rect(x-1,h,x+2,h+1,6); line(x-2,h-2,x+2,h-2,2)
        elif name == "small_cactus":
            rect(6,0,10,12,2); rect(7,1,8,12,3); rect(3,4,6,8,2)
            rect(11,6,13,10,2); line(10,6,12,6,1)
            for x,y in ((6,3),(9,7),(7,10)): rect(x,y,x+1,y+1,5)
        elif name == "fallen_twigs":
            line(2,1,13,5,1,2); line(6,3,5,7,2); line(9,4,12,8,3)
            line(4,6,11,1,2); line(3,2,12,5,3)
        elif name in {"jungle_fern", "nettle"}:
            line(8,0,8,14,1)
            for y,half in ((3,5),(6,6),(9,4),(12,2)):
                line(8,y,8-half,y+2,3,2); line(8,y,8+half,y+2,2,2)
                if name == "jungle_fern":
                    line(8-half,y+2,8-half+1,y+4,2); line(8+half,y+2,8+half-1,y+4,3)
        elif name == "tundra_moss":
            for x,h in ((2,3),(5,5),(8,4),(11,6),(14,3)):
                line(x,0,x,h,2); line(x,h-1,x-2,h+1,3); line(x,h-1,x+1,h,1)
        elif name == "reed_flower":
            for x,h in ((4,10),(8,14),(12,11)):
                line(x,0,x,h-2,2); line(x,h-3,x-2,h,6); line(x,h-3,x+2,h,6)
        elif name == "heather":
            for x,h in ((4,7),(7,12),(11,9)):
                line(8,0,x,h,2)
                for y in range(h-4,h+1,2): rect(x-1,y,x+2,y+1,6)
        elif name == "clover":
            for x,h in ((4,5),(8,8),(12,6)):
                line(x,0,x,h,1); rect(x-2,h-1,x,h+1,3)
                rect(x+1,h-1,x+3,h+1,2); rect(x,h+1,x+2,h+3,3)
            rect(6,8,8,10,6)
        elif name == "wild_mint":
            for x,h in ((5,9),(10,13)):
                line(x,0,x,h,1)
                for y in range(3,h,3): line(x-3,y+2,x+3,y+1,3,2)
                rect(x-1,h,x+2,h+2,6)
        else: # desert flower: low succulent leaves and warm petals
            line(8,0,8,8,2); line(8,2,3,4,3,2); line(8,2,12,3,2,2)
            for x,y in ((5,8),(9,8),(7,11)): rect(x,y,x+3,y+2,6)
            rect(7,8,10,10,5)
        return [image[(15-y)*16+x] for y in range(16) for x in range(16)]
    if name in BIOME_PLANTS:
        if name == "fern":
            line(8,0,8,13,2)
            for y,half in ((3,5),(6,4),(9,3),(12,1)):
                line(8,y,8-half,y+2,3); line(8,y,8+half,y+2,2)
        elif name in {"red_mushroom", "brown_mushroom"}:
            rect(7,0,9,6,6); rect(3,5,13,8,6); rect(5,8,11,10,6)
            rect(4,5,12,6,7)
            if name == "red_mushroom":
                for x,y in ((5,7),(9,8),(11,6)): rect(x,y,x+1,y+1,5)
        elif name == "dead_bush":
            line(8,0,8,10,1,2); line(8,4,3,8,2); line(8,5,12,10,2)
            line(4,7,4,11,3); line(11,8,14,8,3)
        elif name == "cattail":
            for x,height in ((5,11),(10,14)):
                line(x,0,x,height-3,2); rect(x-1,height-4,x+1,height,7)
            line(7,0,3,7,3); line(8,0,12,9,2)
        elif name in {"dry_grass", "beach_grass"}:
            for x,height in ((3,6),(5,10),(9,12),(12,8)):
                line(7,0,x,height,2); line(x,height-3,x-1,height,3)
        elif name == "lavender":
            for x,height in ((5,10),(8,14),(11,11)):
                line(x,0,x,height,2)
                for y in range(height-4,height+1,2): rect(x-1,y,x+1,y+1,6)
        else:
            line(8,0,8,10,2); line(8,3,4,6,3); line(8,5,12,7,2)
            if name == "bellflower":
                line(8,10,11,11,2); rect(10,8,13,11,6); rect(9,8,14,9,6)
            else:
                for cx,cy in ((6,11),(10,11),(8,13)):
                    rect(cx-1,cy-1,cx+2,cy+2,6)
                rect(7,10,9,12,5)
        return [image[(15-y)*16+x] for y in range(16) for x in range(16)]
    if name == "torch":
        rect(7,0,9,10,4); rect(8,0,9,9,7); rect(6,10,10,14,5); rect(7,11,9,15,6)
    elif name in {"wheat_young","wheat_middle","wheat_mature"}:
        height={"wheat_young":5,"wheat_middle":10,"wheat_mature":14}[name]
        for x in (4,8,12):
            line(x,0,x-1,height-1,2 if height<14 else 4)
            for y in range(3,height,3):
                line(x-1,y,x-3,y+2,3 if height<14 else 5)
                line(x-1,y,x+1,y+2,2 if height<14 else 6)
    elif name.endswith("sapling"):
        line(8,0,8,7,4,2)
        if name=="spruce_sapling":
            for y in range(5,15):
                half=max(0,(15-y)//3); line(8-half,y,8+half,y,2+(y%3==0))
        else:
            for cx,cy in ((5,9),(10,10),(8,13)):
                for x,y in grow_blob(seed,name,(cx,cy),12,cy): image[y*16+x]=palette[2+(y>cy)]
            line(8,5,5,8,1); line(8,6,11,9,1)
    elif name in {"flower","dandelion","blue_orchid","allium","oxeye_daisy",
                   "starflower","cloud_bloom","sunflower_top","glowshroom",
                   "glow_fern","resonant_crystal"}:
        line(8,0,8,10,2); line(8,3,5,5,3); line(8,5,11,7,2)
        if name=="glowshroom":
            rect(7,0,9,8,5); rect(3,7,13,10,6); rect(5,10,11,12,6)
        else:
            cx,cy=8,12
            radius=3 if name in {"sunflower_top","allium","cloud_bloom"} else 2
            for y in range(cy-radius, min(16,cy+radius+1)):
                for x in range(cx-radius,cx+radius+1):
                    if abs(x-cx)+abs(y-cy)<=radius+1: image[y*16+x]=palette[6]
            rect(7,11,9,13,4 if name=="sunflower_top" else 5)
    elif name in {"reeds","sunflower_bottom"}:
        for x in ((4,8,12) if name=="reeds" else (8,)):
            line(x,0,x,15,2,2)
            for y in (4,9,14): line(x,y,x+1,y,3)
        line(8,6,4,9,3); line(8,9,12,12,2)
    else:
        for x,height in ((3,8),(6,12),(9,14),(12,10)):
            line(8,0,x,height,2); line(x,height-3,x-1,height,3)
    return [image[(15-y)*16+x] for y in range(16) for x in range(16)]


def resolve_seed(seed,name,local_seeds=None):
    return int((local_seeds or {}).get(name,seed))

TURF_SIDE_NAMES = {"grass_side", "aether_grass_side", "dry_grass_side", "leaf_litter_side"}


@lru_cache(maxsize=4096)
def _pixel_lab(pixel):
    return _srgb_to_oklab(pixel[:3])


def perceptual_distance(a, b):
    if not a[3] or not b[3]:
        return abs(a[3]-b[3]) / 255.0
    return math.sqrt(sum((x-y)**2 for x,y in zip(_pixel_lab(a),_pixel_lab(b))))


def seam_metrics(pixels):
    """Independent axes, perceptual RGB and alpha; no hidden hue discontinuities."""
    result={}
    for axis in ("x", "y"):
        for label,distance in (("luminance",color_distance), ("perceptual",perceptual_distance),
                               ("alpha",lambda a,b: abs(a[3]-b[3])/255.0)):
            cuts=[]
            for cut in range(SIZE):
                pairs=((pixels[t*SIZE+cut],pixels[t*SIZE+wrap(cut-1)]) for t in range(SIZE))                     if axis=="x" else ((pixels[cut*SIZE+t],pixels[wrap(cut-1)*SIZE+t]) for t in range(SIZE))
                cuts.append(sum(distance(a,b) for a,b in pairs)/SIZE)
            floor=1.0 if label=="luminance" else .005
            interior=sum(cuts[1:])/15
            result[f"{label}_{axis}"]={"seam":cuts[0],"interior":interior,
                                          "ratio":cuts[0]/max(floor,interior)}
    return result


def center_periodic_tile(pixels, axes=("x", "y")):
    """Choose repeat cuts with ordinary color and alpha adjacency.

    Translate the entire torus; directional caps may translate horizontally
    only. This retains pixel histograms, cluster counts and plant orientation.
    """
    origins={"x":0,"y":0}
    for axis in axes:
        measures=[]
        for distance,floor in ((color_distance,1.0),(perceptual_distance,.005),
                               (lambda a,b:abs(a[3]-b[3])/255.0,.005)):
            cuts=[]
            for cut in range(SIZE):
                pairs=((pixels[t*SIZE+cut],pixels[t*SIZE+wrap(cut-1)]) for t in range(SIZE))                     if axis=="x" else ((pixels[cut*SIZE+t],pixels[wrap(cut-1)*SIZE+t]) for t in range(SIZE))
                cuts.append(math.fsum(distance(a,b) for a,b in pairs)/SIZE)
            measures.append([value/max(floor,(math.fsum(cuts)-value)/15) for value in cuts])
        # Python 3.12 changed sum(float). Stable reductions and explicit
        # near-tie rounding keep periodic origins identical across interpreters.
        origins[axis]=min(range(SIZE),key=lambda cut:(round(max(0,max(m[cut] for m in measures)-2.4)*100 +
                                                    math.fsum(abs(m[cut]-1) for m in measures),12),cut))
    sx,sy=origins["x"],origins["y"]
    return [pixels[wrap(y+sy)*SIZE+wrap(x+sx)] for y in range(SIZE) for x in range(SIZE)]


def generate_decoration_texture(name, seed):
    """Periodic masonry and fabric, with coordinate-local deterministic detail."""
    palette = PALETTES[name]
    pixels = []
    for y in range(SIZE):
        for x in range(SIZE):
            h = sample(seed, name, x, y)
            shade = 2 + int(h % 19 == 0) - int(h % 23 == 0)
            if name.endswith("_wool"):
                phase = sample(seed,name,0,0) % 4
                shade = 2 + int((x + 2*y + phase) % 4 == 0) - int((2*x + y + phase) % 7 == 0)
            elif name.startswith("polished_") or name == "smooth_sandstone":
                # Quiet polished planes with sparse connected mineral flecks.
                if name == "polished_basalt":
                    shade = 2 + int((y + sample(seed,name,x//4,0)%4) % 8 == 0)
                elif name == "polished_limestone":
                    shade = 2 + int((x + round(2*math.sin(y*math.tau/16))) % 13 == 0)
                elif name == "polished_granite":
                    shade = 2 + int(sample(seed,name,x//2,y//2) % 13 == 0)
                elif name == "polished_tuff":
                    shade = 2 + int(sample(seed,name,x//3,y//2) % 11 == 0)
                else:
                    shade = 2 + int((x//4,y//4) == (sample(seed,name,0,0)%4,sample(seed,name,1,0)%4))
            elif name == "chiseled_stone_bricks":
                if x in (1, 14) or y in (1, 14): shade = 1
                if (x in (4, 11) and 4 <= y <= 11) or (y in (4, 11) and 4 <= x <= 11): shade = 0
                if (x in (5, 10) and 5 <= y <= 10) or (y in (5, 10) and 5 <= x <= 10): shade = 3
            elif name == "cut_sandstone":
                if x in (0, 15) or y in (0, 15): shade = 1
                elif x == 1 or y == 1: shade = 3
            elif name == "sandstone":
                if y % 8 == 0: shade = 1
                elif y % 8 == 1: shade = 3
            else:
                height = 4 if name == "bricks" else 8
                width = 8
                joint = y % height == 0 or (x + (4 if (y//height) % 2 else 0)) % width == 0
                if joint: shade = 0
                elif y % height == 1: shade = 3
                if name == "cracked_stone_bricks" and (x - y//2) % 8 == 3: shade = 0
                if name == "mossy_stone_bricks" and (sample(seed,name,x//3,y//3) % 5 == 0):
                    pixels.append(palette[4 + int(shade >= 2)])
                    continue
            pixels.append(palette[shade])
    return center_periodic_tile(pixels) if name.startswith("polished_") or name == "smooth_sandstone" else pixels


def generate_biome_texture(name, seed):
    if name in {"dry_grass_side", "leaf_litter_side"}:
        palette = PALETTES[name]
        indices = generate_dirt("dirt", seed)
        return [palette[4 + int((x//3)%2 == 0)] if y < 5 else palette[min(3, indices[y*16+x])]
                for y in range(16) for x in range(16)]
    palette = PALETTES[name]
    indices = quantize(macro_field(seed, name, 6))
    if name in {"rooted_dirt", "permafrost", "shale", "red_sandstone"}:
        for x in range(16):
            y = (round(2*math.sin(x*math.tau/16)) + 6) % 16
            indices[y*16+x] = 3 if name == "permafrost" else 1
    if name == "leaf_litter_soil":
        for i in range(7):
            h = sample(seed,name,i,17)
            for x,y in grow_blob(seed,name,(h%16,(h>>8)%16),4,i):
                indices[y*16+x] = 4 if i%2 else 1
    if name == "rooted_dirt":
        for x in range(16): indices[((x//3+10)%16)*16+x] = 4
    if name == "blue_ice":
        for x in range(16): indices[((x+3)%16)*16+x] = 4
    if name == "coral_rock":
        for i in range(6):
            h = sample(seed,name,i,22)
            for x,y in grow_blob(seed,name,(h%16,(h>>8)%16),3,i): indices[y*16+x] = 1
    return center_periodic_tile([palette[max(0,min(5,int(i)))] for i in indices])


def generate_ecology_texture(name, seed):
    palette = PALETTES[name]
    indices = quantize(macro_field(seed, name, 6))
    for y in range(16):
        for x in range(16):
            i = y*16+x
            wave = round(2*math.sin(x*math.tau/16))
            if name in {"gneiss", "wet_limestone", "gypsum"}:
                if (y+wave) % (4 if name == "gneiss" else 8) == 0: indices[i] = 1
                elif (y+wave) % 8 == 1: indices[i] = 4
            elif name == "marble":
                if (x+round(3*math.sin(y*math.tau/16))) % 13 == 0: indices[i] = 1
            elif name in {"cracked_mud", "salt_crust"}:
                if (x+wave)%8 == 0 or (y+round(2*math.sin(x*math.tau/8)))%8 == 0: indices[i] = 1
                elif name == "salt_crust": indices[i] = min(4,indices[i]+1)
            elif name in {"amethyst_block", "quartz_block"}:
                facet = (x+y//2)%8
                if facet < 2: indices[i] = 4
                elif facet == 7: indices[i] = 1
            elif name == "diorite":
                if sample(seed,name,x//2,y//2)%4 == 0: indices[i] = 1
            elif name == "andesite":
                if sample(seed,name,x//3,y//2)%9 == 0: indices[i] = 4
            elif name in {"laterite", "red_clay", "iron_stained_rock"}:
                if (y+wave)%9 == 0: indices[i] = 1
            elif name == "sulfur_rock":
                if sample(seed,name,x//3,y//3)%4 == 0: indices[i] = 4
            elif name == "cave_moss":
                if sample(seed,name,x//2,y//2)%7 == 0: indices[i] = 1
    return center_periodic_tile([palette[max(0,min(5,int(i)))] for i in indices])


# Mineral pixels use three stone roles followed by three cyan crystal roles.
class SemanticColor(tuple):
    """A palette choice carries its structural role through drawing/translation."""
    def __new__(cls, rgba, role):
        value = super().__new__(cls, rgba)
        value.role = role
        return value


for _name, _base in HEAVEN_TEXTURES.items():
    PALETTES[_name] = list(PALETTES[_base])
    _roles = _role_palette(HEAVEN_COLORS[_name])
    if _base in PLANTS:
        PALETTES[_name][1:7] = _roles
    else:
        PALETTES[_name] = _roles

PALETTES["star_crystal_ore"] = list(PALETTES["diamond_ore"])
PALETTES["star_crystal_ore"][3:] = _role_palette((104,205,227))[3:]
PALETTES["cloudberry_bush"][6] = (147,169,229,255)
PALETTES["hanging_cloud_vine"] = [(0,0,0,0), (48,94,100,255),
    (76,139,146,255), (117,183,177,255), (106,211,223,255),
    (183,242,244,255), (239,255,246,255)]

for _name, _palette in PALETTES.items():
    PALETTES[_name] = [SemanticColor(color, role) for role, color in enumerate(_palette)]
STRUCTURE_PALETTES = dict(PALETTES)


def generate_texture(name,seed,local_seeds=None):
    local=resolve_seed(seed,name,local_seeds)
    if name in VARIANT_PLANKS:
        palette=PALETTES[name]
        return [palette[i] for i in generate_planks(name,local)]
    if name in DOOR_TEXTURES:
        palette=PALETTES[name]; wood=name.split("_")[0]; top=name.endswith("_top")
        style=DOOR_WOODS.index(wood)
        pixels=[]
        for y in range(16):
            for x in range(16):
                border=x<2 or x>13 or y<2 or y>13
                role=2 if border else 3+(x//3+style)%2
                if x in (2,13) or y in (2,13): role=1
                if not border and y in (7,8): role=2
                # Different lattices/panels for each available wood family.
                if top and wood in ("oak","birch","acacia","skyroot","iron"):
                    window=3<=x<=12 and 3<=y<=11
                    if wood=="oak": window=window and x not in (7,8) and y not in (7,8)
                    elif wood=="birch": window=5<=x<=10 and 3<=y<=9
                    elif wood=="acacia": window=3<=x<=12 and y in (4,5,9,10)
                    elif wood=="skyroot": window=abs(x-7.5)+abs(y-7)<=4
                    elif wood=="iron": window=x in (4,5,10,11) and 3<=y<=6
                    if window: role=0
                if wood=="spruce" and x in (4,8,12):role=1
                if wood=="jungle" and not border and (x+y)%8==0:role=2
                if not top and x in (11,12) and y==4:role=5
                pixels.append(palette[role])
        return pixels
    if name == "hanging_cloud_vine":
        palette = PALETTES[name]
        pixels = [palette[0]] * (SIZE*SIZE)
        # PNG rows run downwards: all tendrils hang from the top, and the
        # central strand continues across vertically stacked vine blocks.
        for y in range(SIZE): pixels[y*SIZE+8] = palette[2]
        for x, y in ((7,2), (6,3), (5,3), (4,3), (3,4), (3,5),
                     (3,6), (9,6), (10,7), (11,7), (12,8), (12,9)):
            pixels[y*SIZE+x] = palette[1]
        for x, y in ((5,1), (6,2), (10,4), (11,3), (6,9), (7,10)):
            pixels[y*SIZE+x] = palette[3]
        # Cyan rims, milk-white hearts and a pointed tip read as stardew,
        # rather than flowers or ordinary fruit. Roles 4..6 alone emit.
        for x, y in ((3,7), (12,10), (8,12)):
            pixels[y*SIZE+x] = palette[4]
            for dy in (1,2):
                for dx in (-1,0,1):
                    pixels[(y+dy)*SIZE+x+dx] = palette[5 if dx == 0 else 4]
            pixels[(y+1)*SIZE+x] = palette[6]
            pixels[(y+3)*SIZE+x] = palette[5]
        return pixels
    if name in HEAVEN_TEXTURES:
        base = HEAVEN_TEXTURES[name]
        original = generate_texture(base, local)
        source_palette = PALETTES[base]
        target_palette = PALETTES[name]
        pixels = [target_palette[min(range(len(source_palette)),
            key=lambda i: sum((p[c]-source_palette[i][c])**2 for c in range(4)))]
            for p in original]
        if name == "cloudberry_bush":
            for px, py in ((5, 5), (9, 7), (7, 10)):
                _paint_rect(pixels, px, py, px+1, py+1, PALETTES[name][6])
        return pixels
    if name in ECOLOGY_BASES:
        pixels=generate_ecology_texture(name,local)
    elif name in BIOME_BASES or name in {"dry_grass_side", "leaf_litter_side"}:
        pixels=generate_biome_texture(name,local)
    elif name in DECORATION_BASES:
        pixels=generate_decoration_texture(name,local)
    elif name in FUNCTIONAL + VILLAGE_FUNCTIONAL or any(name == base+"_"+face for base in FUNCTIONAL for face in ("top","side","bottom")):
        return generate_functional_texture(name,local)
    elif name in PLANTS:
        return generate_plant_texture(name,local)
    else:
        indices=generate_special(name,local,generate_generic(name,local))
        palette=PALETTES[name]
        pixels=[palette[max(0,min(len(palette)-1,int(i)))] for i in indices]
    if name == "fire":
        # Flames are upright sprites, not repeating surfaces. Preserve the
        # complete silhouette and convert bottom-up drawing to PNG row order.
        return [pixels[(SIZE-1-y)*SIZE+x] for y in range(SIZE) for x in range(SIZE)]
    if name in TURF_SIDE_NAMES:
        return center_periodic_tile(pixels, ("x",))
    if name.endswith("_log_top"):
        return pixels
    return center_periodic_tile(pixels)

def generate_entity_texture(name,seed):
    """Generate a wrapping material swatch, not a face portrait."""
    if name not in ENTITY_PALETTES: raise ValueError(f"unknown entity material '{name}'")
    indices=_entity_wrapping_indices(name,"body",seed)
    palette=_entity_part_palette(name,"body")
    return [palette[index] for index in indices]

def _entity_part_palette(name, part):
    base=ENTITY_PALETTES[name]
    overrides={
        ("cow","head"): ((47,31,23,255),(68,43,29,255),(92,57,35,255),
                          (126,82,52,255),(190,154,112,255),(232,211,178,255)),
        ("sheep","head"): ((62,55,48,255),(84,76,65,255),(111,101,86,255),
                            (143,132,113,255),(181,172,151,255),(218,211,190,255)),
        ("zombie","body"): ((25,67,70,255),(31,82,85,255),(38,98,99,255),
                             (47,116,113,255),(61,135,126,255),(79,153,140,255)),
        ("zombie","secondary"): ((42,32,63,255),(55,41,82,255),(68,51,101,255),
                                  (82,63,119,255),(99,78,138,255),(119,97,157,255)),
        ("villager","body"): ((45,30,23,255),(67,43,30,255),(92,58,37,255),
                                 (121,76,46,255),(154,103,67,255),(190,142,99,255)),
        ("zombie_villager","body"): ((25,46,31,255),(33,63,39,255),(45,82,49,255),
                                        (58,103,59,255),(77,126,73,255),(103,151,91,255)),
        ("chicken","primary"): ((111,70,17,255),(145,91,20,255),(180,114,24,255),
                                 (211,143,31,255),(235,174,48,255),(247,202,78,255)),
        ("chicken","head"): ((169,166,151,255),(187,185,170,255),(205,203,188,255),
                              (220,218,204,255),(235,233,220,255),(248,247,237,255)),
        ("chicken","body"): ((166,164,150,255),(184,182,168,255),(202,200,186,255),
                              (218,216,202,255),(234,232,219,255),(247,246,236,255)),
        ("chicken","secondary"): ((158,157,145,255),(178,177,164,255),(198,197,184,255),
                                   (216,215,202,255),(233,232,220,255),(247,246,237,255)),
    }
    if name=="player":
        anchor = (222,170,126) if part=="head" else (83,128,165) if part in {"body","primary"} else (76,87,116)
        return tuple(_role_palette(anchor))
    selected = overrides.get((name,part), base)
    lifted=[]
    for color in selected:
        l,a,b=_srgb_to_oklab(color[:3])
        rgb=_oklab_to_srgb((max(0.30,min(0.93,(.65*l+.35*_srgb_to_oklab(selected[3][:3])[0])+.055)),a*1.01,b*1.01))
        lifted.append(tuple(max(2,min(248,c)) for c in rgb)+(255,))
    if name=="cow" and part != "head":
        lifted[4]=(203,186,157,255); lifted[5]=(216,202,178,255)
    return tuple(lifted)

def _cube_coordinate(face,x,y):
    a=2*x-15; b=15-2*y
    return {
        "front":(a,b,-15), "back":(-a,b,15),
        "left":(-15,b,-a), "right":(15,b,a),
        "top":(a,15,-b), "bottom":(a,-15,b),
    }[face]

def _entity_surface_indices(name,part,face,seed):
    """Sample a continuous cube-space field so adjacent cuboid faces agree."""
    salt=sample(seed,name,len(part),sum(ord(c) for c in part))
    p0=(salt&255)/19.0; p1=((salt>>8)&255)/23.0; p2=((salt>>16)&255)/29.0
    result=[]
    for y in range(SIZE):
        for x in range(SIZE):
            cx,cy,cz=_cube_coordinate(face,x,y)
            value=(math.sin((cx+p0)/5.4)+math.sin((cy+p1)/6.2)+
                   math.sin((cz+p2)/4.8)+.55*math.sin((cx+cy-cz+p0)/7.3))
            # Keep the base material calm; species markings below provide the
            # readable shapes.  The former high-amplitude field made every mob
            # look like it had giant circular camouflage spots.
            index=max(2,min(4,int(round(3.0+value*.20))))
            marking=(math.sin((cx+p0)*.24)+math.sin((cy+p1)*.21)+
                     math.sin((cz+p2)*.27))
            if name=="cow" and part in {"body","primary","secondary"}:
                if marking>1.08: index=4
                elif marking<-.96: index=1
            elif name=="pig" and part=="body":
                if cy < -4: index=max(1,index-1)
                if marking>1.38: index=4
            elif name=="sheep" and part in {"body","primary","secondary"}:
                index=max(3,min(5,index+1))
                if marking>1.45: index=2
            elif name=="chicken" and part in {"body","primary","secondary"}:
                index=max(3,min(5,index+1))
                if cx < -4 and cy>0: index=2
            elif name=="zombie" and part=="body":
                index=4 if cy>0 else 2
                if marking>1.48: index=5
            elif name in {"villager","zombie_villager"} and part=="body":
                index=4 if cy>1 else 3
            elif name=="skeleton" and part in {"body","primary","secondary"}:
                index=max(2,min(5,index+1))
                if abs(cx)<2 or abs(cz)<2: index=1
            elif name=="spider" and part in {"body","primary","secondary"}:
                index=max(0,min(3,index-1))
                if marking>1.52: index=4
            elif name=="blastling" and part in {"body","primary","secondary"}:
                index=max(1,min(5,index))
                if marking>1.42: index=5
            elif name=="player" and part=="body":
                index=4 if cy>0 else 2
            result.append(index)
    return result

def _entity_wrapping_indices(name,part,seed):
    values=macro_field(seed,name+"_"+part,5)
    indices=quantize(values)
    if name=="cow":
        for i in range(3):
            h=sample(seed,name,i,137)
            for x,y in grow_blob(seed,name,(h%16,(h>>8)%16),16,20+i): indices[y*16+x]=4
    elif name in {"sheep","chicken"}:
        indices=[min(5,max(3,v+1)) for v in indices]
    elif name=="spider": indices=[max(1,min(3,v-1)) for v in indices]
    return indices


def _paint_rect(tile,x0,y0,x1,y1,color):
    for y in range(max(0,y0),min(SIZE,y1)):
        for x in range(max(0,x0),min(SIZE,x1)): tile[y*SIZE+x]=color

def _paint_entity_face(name,tile):
    dark=(34,29,27,255); blackish=(27,30,27,255)
    if name=="cow":
        _paint_rect(tile,3,4,5,6,dark); _paint_rect(tile,11,4,13,6,dark)
        _paint_rect(tile,4,8,12,14,(216,181,139,255))
        _paint_rect(tile,5,10,7,12,(66,43,34,255)); _paint_rect(tile,9,10,11,12,(66,43,34,255))
    elif name=="pig":
        _paint_rect(tile,3,4,5,6,dark); _paint_rect(tile,11,4,13,6,dark)
        _paint_rect(tile,4,8,12,14,(239,166,174,255))
        _paint_rect(tile,5,10,7,12,(116,54,67,255)); _paint_rect(tile,9,10,11,12,(116,54,67,255))
    elif name=="sheep":
        _paint_rect(tile,3,5,5,7,dark); _paint_rect(tile,11,5,13,7,dark)
        _paint_rect(tile,6,10,10,12,(66,57,49,255))
    elif name=="chicken":
        _paint_rect(tile,3,4,5,6,dark); _paint_rect(tile,11,4,13,6,dark)
        _paint_rect(tile,5,7,11,10,(232,166,47,255)); _paint_rect(tile,6,10,10,13,(190,50,38,255))
    elif name=="zombie":
        _paint_rect(tile,3,4,6,7,(26,40,29,255)); _paint_rect(tile,10,4,13,7,(26,40,29,255))
        _paint_rect(tile,5,10,11,12,(31,55,38,255)); _paint_rect(tile,7,9,9,10,(43,73,48,255))
    elif name=="iron_golem":
        _paint_rect(tile,3,6,6,8,(149,48,41,255))
        _paint_rect(tile,10,6,13,8,(149,48,41,255))
        _paint_rect(tile,7,8,9,13,(145,145,128,255))
    elif name in {"villager","zombie_villager"}:
        eye=(30,25,22,255) if name=="villager" else (18,32,20,255)
        nose=(126,78,48,255) if name=="villager" else (51,91,53,255)
        _paint_rect(tile,3,4,6,7,eye); _paint_rect(tile,10,4,13,7,eye)
        _paint_rect(tile,6,7,10,13,nose)
        _paint_rect(tile,4,2,7,3,eye); _paint_rect(tile,9,2,12,3,eye)
    elif name=="skeleton":
        _paint_rect(tile,2,3,6,8,blackish); _paint_rect(tile,10,3,14,8,blackish)
        _paint_rect(tile,7,7,9,10,(48,44,37,255)); _paint_rect(tile,5,12,11,13,(72,65,54,255))
    elif name=="spider":
        red=(222,54,43,255); bright=(248,86,63,255)
        for x,y in ((3,5),(6,4),(10,4),(13,5),(5,8),(11,8)):
            _paint_rect(tile,x-1,y-1,x+1,y+1,red)
            tile[(y-1)*SIZE+x-1]=bright
    elif name=="blastling":
        _paint_rect(tile,3,3,6,8,blackish); _paint_rect(tile,10,3,13,8,blackish)
        _paint_rect(tile,6,8,10,11,blackish); _paint_rect(tile,4,10,7,14,blackish)
        _paint_rect(tile,9,10,12,14,blackish); _paint_rect(tile,7,12,9,15,blackish)
    elif name=="player":
        _paint_rect(tile,3,5,5,7,(34,30,26,255)); _paint_rect(tile,11,5,13,7,(34,30,26,255))
        _paint_rect(tile,6,10,10,11,(128,72,54,255))

def _paint_entity_body(name,tile):
    if name in {"cow","pig","sheep","chicken","spider","blastling"}:
        # Species patterns already wrap over the cuboid; do not stamp a second
        # face-like rectangle on the chest.
        return
    elif name=="iron_golem":
        _paint_rect(tile,3,1,5,14,(71,109,56,255))
        _paint_rect(tile,5,6,8,8,(102,132,71,255))
        _paint_rect(tile,2,12,14,13,(139,141,126,255))
    elif name=="skeleton":
        bone=(205,197,171,255); shadow=(82,75,62,255)
        _paint_rect(tile,7,2,9,14,bone)
        for y in (4,7,10):
            _paint_rect(tile,3,y,7,y+1,shadow); _paint_rect(tile,9,y,13,y+1,shadow)
    elif name=="zombie":
        _paint_rect(tile,6,1,10,3,(28,70,72,255))
    elif name in {"villager","zombie_villager"}:
        palette=_entity_part_palette(name,"body")
        _paint_rect(tile,7,2,9,15,palette[2])
        _paint_rect(tile,5,1,11,3,palette[1])
        _paint_rect(tile,2,12,14,13,palette[3])
    elif name=="player":
        _paint_rect(tile,6,1,10,3,(188,151,116,255))
        _paint_rect(tile,2,12,14,14,(73,111,147,255))

def generate_entity_skin(name,seed):
    """Generate one original 64x64 semantic skin atlas for a runtime mob."""
    if name not in ENTITY_SKIN_NAMES: raise ValueError(f"unknown entity skin '{name}'")
    atlas=[(0,0,0,255)]*(ENTITY_SKIN_SIZE*ENTITY_SKIN_SIZE)
    for semantic,index in ENTITY_SKIN_LAYOUT.items():
        part,_,face=semantic.partition("_")
        if semantic.startswith("limb_primary"):
            part="primary"; indices=_entity_wrapping_indices(name,part,seed)
        elif semantic.startswith("limb_secondary"):
            part="secondary"; indices=_entity_wrapping_indices(name,part,seed)
        elif semantic in {"detail","fallback"}:
            part=semantic; indices=_entity_wrapping_indices(name,part,seed)
        else:
            indices=_entity_surface_indices(name,part,face,seed)
        palette=_entity_part_palette(name,part)
        tile=[palette[i] for i in indices]
        if name=="player" and part=="head":
            hair=(89,65,46,255)
            if face in {"back","top"}: tile=[hair]*256
            elif face != "bottom": _paint_rect(tile,0,0,16,3,hair)
        if semantic=="head_front": _paint_entity_face(name,tile)
        elif semantic=="body_front": _paint_entity_body(name,tile)
        if semantic in {"limb_primary","limb_secondary"}:
            if name in {"cow","sheep","pig"}:
                _paint_rect(tile,0,14,16,16,(92,76,60,255))
            elif semantic=="limb_primary" and name in {"player","zombie","zombie_villager"}:
                hand=_entity_part_palette(name,"head")[3]
                _paint_rect(tile,0,11,16,16,hand)
        tx,ty=index%4,index//4
        for y in range(SIZE):
            start=(ty*SIZE+y)*ENTITY_SKIN_SIZE+tx*SIZE
            atlas[start:start+SIZE]=tile[y*SIZE:(y+1)*SIZE]
    return atlas

def build_entity_skins(output,seed):
    skin_dir=output/"entity_skins"; skin_dir.mkdir(parents=True,exist_ok=True)
    metadata={"version":1,"generator_version":GENERATOR_VERSION,"style":STYLE_ID,
              "width":ENTITY_SKIN_SIZE,"height":ENTITY_SKIN_SIZE,
              "tile_size":SIZE,"columns":4,"rows":4,"filter":"nearest",
              "seed":seed,"layout":ENTITY_SKIN_LAYOUT,
              "style_definition":"definitions/entity_styles.json","entities":{}}
    for name in ENTITY_SKIN_NAMES:
        pixels=generate_entity_skin(name,seed)
        # Compression level 0 (stored deflate blocks) keeps these embedded
        # skins byte-identical across zlib versions and operating systems;
        # entity GLBs embed the exact same bytes in their glTF buffers.
        path=skin_dir/f"{name}.png"; write_png(path,ENTITY_SKIN_SIZE,ENTITY_SKIN_SIZE,pixels,0)
        metadata["entities"][name]={"source":f"entity_skins/{name}.png",
                                     "features":ENTITY_STYLE_DEFINITIONS["entities"][name].get("documentation", {}).get("motifs", [])}
    (output/"entity_skins.json").write_text(
        json.dumps(metadata,indent=2,sort_keys=True)+"\n",encoding="utf-8")

def png_bytes(width,height,pixels,compression_level=9):
    if width <= 0 or height <= 0 or len(pixels) != width*height:
        raise ValueError("PNG dimensions do not match pixel count")
    if any(len(pixel) != 4 or any(type(c) is not int or not 0 <= c <= 255 for c in pixel) for pixel in pixels):
        raise ValueError("PNG pixels require four uint8 channels")
    raw=bytearray()
    for y in range(height):
        raw.append(0)
        for pixel in pixels[y*width:(y+1)*width]: raw.extend(pixel)
    def chunk(kind,data):
        return struct.pack(">I",len(data))+kind+data+struct.pack(">I",zlib.crc32(kind+data)&0xffffffff)
    header=struct.pack(">IIBBBBB",width,height,8,6,0,0,0)
    return b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",header)+chunk(b"IDAT",zlib.compress(bytes(raw),compression_level))+chunk(b"IEND",b"")

def write_png(path,width,height,pixels,compression_level=9):
    path.parent.mkdir(parents=True,exist_ok=True); path.write_bytes(png_bytes(width,height,pixels,compression_level))

def load_item_icon_definitions(path):
    data=_read_definition(path)
    if not all(isinstance(data.get(key),dict) for key in ("templates","materials","items")):
        raise ValueError("item definitions require templates/materials/items objects")
    if data.get("generator_version") != GENERATOR_VERSION or data.get("style") != STYLE_ID:
        raise ValueError("item definitions do not match generator version/style")
    if data.get("version") not in (1, 2):
        raise ValueError("item icon definitions require version 1 or 2")
    for category in data.get("generator_categories",[]):
        if category not in GENERATOR_CATEGORIES: raise ValueError(f"unknown generator category '{category}'")
    required={"sword","pickaxe","axe","shovel","hoe","stick","ingot","gem","coal","torch"}
    missing=required-set(data.get("templates",{}))
    if missing: raise ValueError("missing item templates: "+", ".join(sorted(missing)))
    for name in ("wood","stone","copper","iron","gold"):
        shades=data.get("materials",{}).get(name)
        if not isinstance(shades,list) or len(shades)<4:
            raise ValueError(f"material '{name}' requires at least four colors")
        for color in shades:
            if len(color)!=3 or any(not isinstance(c,int) or c<0 or c>255 for c in color):
                raise ValueError(f"invalid RGB color in material '{name}'")
    processing = data.get("palette_processing", {})
    for key,default,lo,hi in (("shadow_floor",.30,0,.8),("chroma_scale",1.02,0,2)):
        value=processing.get(key,default)
        if not isinstance(value,(int,float)) or not math.isfinite(value) or not lo<=value<=hi:
            raise ValueError(f"invalid item palette_processing.{key}")
    for material, shades in data["materials"].items():
        if not isinstance(shades, list) or len(shades) < 4 or any(
                not isinstance(color, list) or len(color) != 3 or
                any(type(c) is not int or not 0 <= c <= 255 for c in color) for color in shades):
            raise ValueError(f"invalid RGB material {material}")
        # Authored midtones drive the result; JSON edits are never replaced by
        # a hardcoded material-name table. Roles provide consistent shading.
        anchor = tuple(round(sum(color[c] for color in shades)/len(shades)) for c in range(3))
        palette = _role_palette(anchor, shadow_floor=processing.get("shadow_floor", .30),
                                chroma_scale=processing.get("chroma_scale", 1.02), levels=4)
        data["materials"][material] = [list(color[:3]) for color in palette]
    data["generator_version"] = GENERATOR_VERSION
    data["style"] = STYLE_ID
    return data

def _rgba(rgb): return tuple(rgb)+(255,)
def _put(canvas,x,y,color):
    if 0<=x<SIZE and 0<=y<SIZE: canvas[y*SIZE+x]=color
def _line(canvas,x0,y0,x1,y1,color,width=1):
    dx=abs(x1-x0); sx=1 if x0<x1 else -1; dy=-abs(y1-y0); sy=1 if y0<y1 else -1; err=dx+dy
    while True:
        for oy in range(-(width//2),width-width//2):
            for ox in range(-(width//2),width-width//2): _put(canvas,x0+ox,y0+oy,color)
        if x0==x1 and y0==y1: break
        twice=2*err
        if twice>=dy: err+=dy; x0+=sx
        if twice<=dx: err+=dx; y0+=sy

TOOL_TEMPLATES = {"sword", "pickaxe", "axe", "shovel", "hoe"}


def tool_part_masks(template):
    """Material-independent coverage; all surface accents are clipped to parts."""
    def stroke(points, width=1):
        canvas=[(0,0,0,0)]*256
        for a,b in zip(points,points[1:]): _line(canvas,*a,*b,(1,1,1,255),width)
        return {i for i,c in enumerate(canvas) if c[3]}
    heads={
        "sword": ((7,8),(8,6),(12,2),(14,1),(14,3),(10,7),(9,9)),
        "pickaxe": ((4,4),(6,2),(10,2),(13,4),(14,7),(12,6),(10,4),(7,4)),
        "axe": ((8,3),(11,2),(14,3),(14,6),(12,8),(10,7),(10,5),(8,5)),
        "shovel": ((9,4),(12,2),(14,3),(14,5),(12,7),(10,7)),
        "hoe": ((6,3),(10,2),(13,3),(14,6),(12,6),(11,4),(7,5)),
    }
    polygon=heads[template]; head=set()
    # Pixel-center polygon fill, including boundary strokes.
    for y in range(16):
        for x in range(16):
            inside=False
            for (ax,ay),(bx,by) in zip(polygon,polygon[1:]+polygon[:1]):
                if (ay>y+.5)!=(by>y+.5) and x+.5 < (bx-ax)*(y+.5-ay)/(by-ay)+ax:
                    inside=not inside
            if inside: head.add(y*16+x)
    head |= stroke(polygon+polygon[:1])
    grip=stroke(((3,13),(9,7)),2)-head
    connector=stroke(((6,7),(10,11)),1) if template=="sword" else stroke(((8,7),(10,5)),2)
    connector -= head
    grip -= connector
    edge={i for i in head if i%16==0 or i-1 not in head or i-16 not in head}
    return {"grip":grip,"connector":connector,"working_head":head,"edge":edge}


def generate_tool_sprite(template,material,definitions):
    masks=tool_part_masks(template)
    shades=[_rgba(c) for c in definitions["materials"][material]]
    handle=[_rgba(c) for c in definitions["handle_palette"]]
    image=[(0,0,0,0)]*256
    for part in ("grip","connector","working_head"):
        cells=masks[part]
        palette=handle if part=="grip" else shades
        for i in cells:
            x,y=i%16,i//16
            lower=i+16 not in cells
            image[i]=palette[0 if lower else 2]
            if part=="grip":
                if i-1 not in cells: image[i]=handle[2]
            elif i in masks["edge"]: image[i]=palette[3]
            elif material=="stone" and (x+2*y)%7==0: image[i]=palette[1]
            elif material=="wood" and (x-y)%5==0: image[i]=palette[1]
    return image


def generate_item_sprite(template,material,definitions):
    """Generate crisp binary-alpha sprites; coordinates use PNG top-left origin."""
    if template not in definitions["templates"]: raise ValueError(f"unknown item template '{template}'")
    if material not in definitions["materials"]: raise ValueError(f"unknown item material '{material}'")
    shades=[_rgba(c) for c in definitions["materials"][material]]
    outline=shades[0]
    handle=[_rgba(c) for c in definitions["handle_palette"]]
    image=[(0,0,0,0)]*(SIZE*SIZE)
    if template in TOOL_TEMPLATES:
        return generate_tool_sprite(template,material,definitions)
    if template in ("door","button"):
        if template=="door":
            for y in range(1,15):
                for x in range(4,12):
                    image[y*SIZE+x]=shades[0 if x in (4,11) or y in (1,14,8) else 2]
            for y in range(3,7):
                for x in (6,7,8,9):image[y*SIZE+x]=(0,0,0,0)
            image[10*SIZE+10]=shades[-1]
        else:
            _paint_rect(image,3,6,12,10,shades[0])
            _paint_rect(image,4,6,11,8,shades[2])
            _paint_rect(image,4,6,11,6,shades[-1])
        return image
    if template=="stick":
        _line(image,4,13,12,3,outline,3); _line(image,4,13,12,3,handle[1],1); _line(image,9,6,12,3,handle[2],1)
    elif template in {"clay_ball", "brick", "dye"}:
        if template == "clay_ball":
            rows = ((4,6,9),(5,4,11),(6,3,12),(7,3,12),(8,3,12),(9,4,11),(10,5,10))
        elif template == "brick":
            rows = ((5,5,11),(6,3,12),(7,3,12),(8,3,12),(9,3,11),(10,5,10))
        else:
            rows = ((6,7,8),(7,5,10),(8,4,11),(9,3,12),(10,3,12),(11,5,10))
        for y,left,right in rows:
            for x in range(left,right+1):
                _put(image,x,y,outline if x in (left,right) else shades[2 if x+y<16 else 1])
        _line(image,6,6,9,6,shades[3],1)
        if template == "brick": _line(image,4,8,10,8,shades[0],1)
    elif template=="ingot":
        for y,left,right in ((5,6,10),(6,4,12),(7,3,12),(8,3,11),(9,4,10),(10,5,9)):
            for x in range(left,right+1): _put(image,x,y,outline)
        for y,left,right in ((6,6,10),(7,5,11),(8,4,10),(9,5,9)):
            for x in range(left,right+1): _put(image,x,y,shades[2 if x+y<15 else 1])
        _line(image,6,6,10,6,shades[3],1)
    elif template=="gem":
        rows=((3,7,8),(4,5,10),(5,4,11),(6,3,12),(7,3,12),(8,4,11),(9,5,10),(10,7,8))
        for y,left,right in rows:
            for x in range(left,right+1): _put(image,x,y,outline if x in (left,right) else shades[2 if x+y<15 else 1])
        _line(image,6,5,9,4,shades[3],1)
    elif template=="coal":
        rows=((4,6,9),(5,4,11),(6,3,12),(7,3,12),(8,4,12),(9,5,11),(10,6,9))
        for y,left,right in rows:
            for x in range(left,right+1): _put(image,x,y,outline if x in (left,right) else shades[1+(x+y)%2])
        _put(image,6,5,shades[3]); _put(image,5,6,shades[2])
    elif template=="torch":
        _line(image,6,13,9,5,outline,4); _line(image,6,13,9,5,handle[1],2)
        for x,y,c in ((8,5,shades[1]),(9,5,shades[2]),(8,4,shades[2]),(9,4,shades[3]),(10,4,shades[2]),(9,3,shades[3])): _put(image,x,y,c)
    elif template=="scepter":
        _line(image,3,14,10,5,outline,4); _line(image,3,14,10,5,handle[1],2)
        for x,y,c in ((9,5,shades[1]),(10,4,shades[2]),(11,3,shades[3]),
                      (12,4,shades[2]),(11,5,shades[1]),(10,3,shades[3])):
            _put(image,x,y,c)
        _put(image,4,13,handle[3]); _put(image,6,10,shades[2])
    elif template=="string":
        for points in (((5,4),(10,3),(12,6),(10,10),(5,11),(3,8),(5,5),(9,5),(10,7),(8,9),(6,8)),):
            for a,b in zip(points,points[1:]): _line(image,*a,*b,shades[2])
        _line(image,10,10,12,13,shades[3])
    elif template=="fishing_rod":
        _line(image,3,14,11,2,outline,3)
        _line(image,3,14,11,2,handle[2],1)
        _line(image,11,2,13,11,shades[3],1)
        _line(image,3,12,5,9,handle[0],3)
        _put(image,13,12,(230,56,43,255)); _put(image,13,11,(240,235,218,255))
    elif template=="fish":
        for y,left,right in ((5,9,11),(6,7,12),(7,5,13),(8,4,13),(9,5,12),(10,7,11)):
            for x in range(left,right+1):
                _put(image,x,y,outline if x in (left,right) else shades[2 if y<9 else 1])
        _line(image,3,6,3,10,outline,2); _line(image,3,8,6,8,shades[1],2)
        _line(image,8,5,10,4,shades[2],1); _line(image,8,10,9,11,shades[1],1)
        _put(image,12,7,(24,26,25,255)); _put(image,11,6,shades[3])
    elif template=="bow":
        points=((5,2),(8,3),(10,5),(11,8),(10,11),(8,13),(5,14))
        for a,b in zip(points,points[1:]): _line(image,*a,*b,handle[2],2)
        _line(image,5,2,5,14,shades[3]); _line(image,11,7,11,9,handle[0],2)
    elif template in {"feather","bone","arrow"}:
        _line(image,3,13,12,3,outline,3); _line(image,3,13,12,3,shades[2],1)
        if template=="feather":
            for x,y in ((5,11),(4,10),(7,9),(6,8),(9,7),(8,6)): _put(image,x,y,shades[3])
        elif template=="bone":
            for x,y in ((2,12),(3,14),(11,2),(13,3)): _put(image,x,y,shades[3])
        else:
            _line(image,10,5,13,2,shades[3],2); _line(image,3,13,3,10,handle[2],1)
    elif template in {"seeds","wheat"}:
        _line(image,7,13,8,4,shades[1],1)
        for x,y in ((6,11),(9,10),(6,8),(9,7),(7,5)):
            _put(image,x,y,shades[2]); _put(image,x+(1 if x<8 else -1),y-1,shades[3])
        if template=="wheat": _line(image,8,8,11,5,shades[2],1)
    elif template in {"food","beef","porkchop","drumstick","mutton","bread","rotten_flesh","leather"}:
        rows={
            "bread":((5,5,10),(6,3,12),(7,2,13),(8,2,13),(9,3,12),(10,4,11)),
            "porkchop":((3,7,10),(4,5,12),(5,4,13),(6,4,13),(7,5,12),(8,6,11),(9,5,9),(10,4,7)),
            "drumstick":((3,9,12),(4,7,13),(5,7,13),(6,7,12),(7,6,11),(8,5,8),(9,4,6),(10,3,5),(11,3,4)),
            "mutton":((3,5,7),(4,4,9),(5,3,10),(6,3,11),(7,4,12),(8,5,12),(9,6,11),(10,8,10)),
            "rotten_flesh":((4,4,8),(5,3,10),(6,4,11),(7,3,12),(8,5,12),(9,6,11),(10,5,9),(11,6,8)),
            "leather":((3,4,6),(3,10,12),(4,4,12),(5,5,11),(6,4,12),(7,3,13),(8,4,12),(9,5,11),(10,4,12),(11,4,6),(11,10,12)),
        }.get(template, ((4,6,9),(5,4,11),(6,3,12),(7,3,12),(8,3,11),(9,4,10),(10,5,9),(11,6,8)))
        for y,left,right in rows:
            for x in range(left,right+1): _put(image,x,y,outline if x in (left,right) else shades[2 if y<8 else 1])
        if template=="bread":
            for x in (5,8,11):
                for y in (6,7):
                    if image[y*16+x][3]: _put(image,x,y,shades[3])
        elif template=="drumstick":
            _line(image,3,11,5,9,(235,225,192,255),2)
        else:
            for x,y in ((6,5),(7,5),(8,5)):
                if image[y*16+x][3]: _put(image,x,y,shades[3])
    elif template=="spawn_egg":
        rows=((2,7,8),(3,5,10),(4,4,11),(5,3,12),(6,3,12),(7,3,12),
              (8,4,11),(9,4,11),(10,5,10),(11,6,9),(12,7,8))
        for y,left,right in rows:
            for x in range(left,right+1):
                _put(image,x,y,outline if x in (left,right) else shades[2])
        for x,y in ((6,4),(10,5),(5,7),(9,8),(7,10)):
            _put(image,x,y,shades[0]); _put(image,x+1,y,shades[1])
        _put(image,7,3,shades[3]); _put(image,8,3,shades[3])
    elif template=="shield":
        for y,left,right in ((2,5,10),(3,4,11),(4,4,11),(5,4,11),(6,4,11),(7,5,10),(8,5,10),(9,6,9),(10,7,8)):
            for x in range(left,right+1): _put(image,x,y,outline if x in (left,right) else shades[1+(x>7)])
        _line(image,5,3,9,3,shades[3],1)
    elif template in {"helmet","chestplate","leggings","boots"}:
        if template=="helmet": rows=((4,5,10),(5,4,11),(6,4,11),(7,4,11),(8,4,6),(8,9,11))
        elif template=="chestplate": rows=((3,3,6),(3,9,12),(4,3,12),(5,4,11),(6,4,11),(7,4,11),(8,4,11),(9,5,10),(10,5,10),(11,5,10))
        elif template=="leggings": rows=((3,4,11),(4,4,11),(5,5,10),(6,5,10),(7,5,7),(7,9,11),(8,5,7),(8,9,11),(9,5,7),(9,9,11),(10,5,7),(10,9,11),(11,5,7),(11,9,11))
        else: rows=((7,4,6),(7,9,11),(8,4,6),(8,9,11),(9,4,7),(9,8,11),(10,4,7),(10,8,11),(11,4,7),(11,8,11))
        for y,left,right in rows:
            for x in range(left,right+1): _put(image,x,y,outline if x in (left,right) else shades[2])
        _line(image,5,4,9,4,shades[3],1)
    elif template=="flint":
        for x,y in ((8,3),(7,4),(8,4),(6,5),(7,5),(8,5),(5,6),(6,6),(7,6),(5,7),(6,7),(5,8),(4,9)):
            _put(image,x,y,shades[3] if x+y<12 else shades[1])
    elif template=="flint_and_steel":
        _line(image,4,12,10,5,outline,4); _line(image,4,12,10,5,shades[1],2)
        _line(image,9,5,13,8,shades[3],2); _line(image,13,8,10,11,shades[2],2)
        _put(image,5,11,handle[2]); _put(image,6,10,handle[3])
    elif template=="gunpowder":
        for x,y in ((5,6),(7,5),(9,6),(6,8),(8,8),(10,9),(5,10),(8,11)):
            _put(image,x,y,shades[1+(x+y)%3])
            if (x+y)%2: _put(image,x+1,y,shades[2])
    return image

def generate_block_item_icon(top_pixels,side_pixels,front_pixels=None):
    """Compose a small nearest-neighbor isometric cube from top and side tiles."""
    front_pixels = front_pixels if front_pixels is not None else side_pixels
    image=[(0,0,0,0)]*(SIZE*SIZE)
    for y in range(4):
        for x in range(8):
            color=top_pixels[min(15,y*4)*SIZE+min(15,x*2)]
            _put(image,4+x-y,2+x//2+y,color)
            _put(image,5+x-y,2+x//2+y,color)
    for y in range(8):
        for x in range(6):
            color=side_pixels[min(15,y*2)*SIZE+min(15,x*3)]
            _put(image,2+x,6+y+x//3,front_pixels[min(15,y*2)*SIZE+min(15,x*3)])
            # Apply the isometric face shade in linear light.  Multiplying
            # encoded RGB made warm materials disproportionately muddy.
            shade=tuple(_linear_to_srgb(_srgb_to_linear(channel) * 0.76)
                        for channel in color[:3]) + (color[3],)
            _put(image,8+x,6+y-x//3,shade)
    return image

def validate_item_sprite(pixels,name="item"):
    errors=[]
    if len(pixels)!=SIZE*SIZE: errors.append(f"{name}: dimensions must be 16x16")
    if any(p[3] not in (0,255) for p in pixels): errors.append(f"{name}: alpha values must be 0 or 255")
    opaque=[p for p in pixels if p[3]]
    if not opaque: errors.append(f"{name}: icon is empty")
    if sum(p[:3]==(0,0,0) for p in opaque)>max(2,len(opaque)//12): errors.append(f"{name}: excessive pure-black outline")
    return errors

def _read_item_tile(path, name):
    width, height, pixels = read_generated_png(path)
    if (width, height) != (SIZE, SIZE):
        raise ValueError(f"{name}: imported icon must be 16x16, got {width}x{height}")
    errors = validate_item_sprite(pixels, name)
    if errors:
        raise ValueError("\n".join(errors))
    return pixels


def build_items_atlas(output,seed,definitions_path,block_definitions_path,override_dir=None,legacy_dir=None,allow_fallback=False):
    definitions=load_item_icon_definitions(definitions_path)
    blocks=_read_definition(block_definitions_path)["blocks"]
    item_dir=output/"items"; item_dir.mkdir(parents=True,exist_ok=True)
    resolved=[]
    failures={}
    # Definition insertion order is the stable atlas slot order; append new items.
    for name,spec in definitions["items"].items():
        override=Path(override_dir)/f"{name}.png" if override_dir else None
        legacy=Path(legacy_dir)/f"{name}.png" if legacy_dir else None
        source_kind="generated"; source=f"items/{name}.png"
        if override and override.exists():
            pixels=_read_item_tile(override,name)
            source_kind="override"; source=f"override/{name}.png"
        else:
            try:
                category=spec["generator"]
                if category=="item_sprite":
                    pixels=generate_item_sprite(spec["template"],spec["material"],definitions)
                elif category=="block_item_icon":
                    block=blocks[spec["block"]]
                    top=block.get("top",block.get("all")); side=block.get("side",block.get("all",top))
                    top_pixels=_read_item_tile(output/f"{top}.png",name)
                    side_pixels=_read_item_tile(output/f"{side}.png",name)
                    front_pixels=_read_item_tile(output/f"{block.get('front',side)}.png",name)
                    pixels=top_pixels if top in PLANTS else generate_block_item_icon(top_pixels,side_pixels,front_pixels)
                else:
                    raise ValueError(f"item '{name}' has invalid generator '{category}'")
                errors=validate_item_sprite(pixels,name)
                if errors: raise ValueError("\n".join(errors))
            except (OSError,ValueError,KeyError) as error:
                if not allow_fallback:
                    raise ValueError(f"cannot generate item {name}: {error}") from error
                failures[name]=str(error)
                if legacy and legacy.exists():
                    pixels=_read_item_tile(legacy,name)
                    source_kind="legacy"; source=f"legacy/{name}.png"
                else:
                    source_kind="missing"; source=None
                    pixels=[(214,71,173,255) if (x//4+y//4)%2 else (73,43,78,255)
                            for y in range(SIZE) for x in range(SIZE)]
        # Final images, including authored imports, feed all previews/reports.
        write_png(item_dir/f"{name}.png",SIZE,SIZE,pixels)
        resolved.append((name,spec,source_kind,source,pixels))
    columns=8; rows=max(1,math.ceil(len(resolved)/columns)); atlas=[(0,0,0,0)]*(columns*SIZE*rows*SIZE)
    metadata={"version":1,"generator_version":GENERATOR_VERSION,"style":STYLE_ID,
              "tile_size":SIZE,"columns":columns,"rows":rows,"filter":"nearest",
              "seed":seed,"priority":["override","generated","legacy","missing"],
              "fallback_enabled":allow_fallback,"generation_failures":failures,
              "template_schema":definitions.get("documentation", {}).get("template_schema", {}),"items":{}}
    for index,(name,spec,kind,source,pixels) in enumerate(resolved):
        tx,ty=index%columns,index//columns
        for y in range(SIZE):
            begin=(ty*SIZE+y)*columns*SIZE+tx*SIZE
            atlas[begin:begin+SIZE]=pixels[y*SIZE:(y+1)*SIZE]
        metadata["items"][name]={"index":index,"x":tx,"y":ty,"generator":spec["generator"],"source_kind":kind,"source":source}
    write_png(output/"items_atlas.png",columns*SIZE,rows*SIZE,atlas)
    (output/"items_atlas.json").write_text(json.dumps(metadata,indent=2,sort_keys=True)+"\n",encoding="utf-8")


def read_generated_png(path):
    return read_png(Path(path))


def luminance(c): return c[0]*.2126+c[1]*.7152+c[2]*.0722
def palette_contrast(palette):
    values=[luminance(c) for c in palette if c[3]==255]; return max(values)-min(values)
def color_distance(a,b): return abs(luminance(a)-luminance(b))

def connected_components(pixels,color):
    pending={(x,y) for y in range(SIZE) for x in range(SIZE) if pixels[y*SIZE+x]==color}; result=[]
    while pending:
        start=pending.pop(); comp={start}; queue=[start]
        while queue:
            for n in neighbors4(*queue.pop()):
                if n in pending: pending.remove(n); comp.add(n); queue.append(n)
        result.append(comp)
    return result

def structure_metrics(pixels):
    lum=[luminance(c) if c[3] else 0 for c in pixels]
    runs=[]
    for y in range(SIZE):
        run=1
        for x in range(1,SIZE):
            if color_distance(pixels[y*SIZE+x],pixels[y*SIZE+x-1])<4: run+=1
            else: runs.append(run); run=1
        runs.append(run)
    for x in range(SIZE):
        run=1
        for y in range(1,SIZE):
            if color_distance(pixels[y*SIZE+x],pixels[(y-1)*SIZE+x])<4: run+=1
            else: runs.append(run); run=1
        runs.append(run)
    comps=[]
    for color in set(pixels):
        for comp in connected_components(pixels,color):
            xs=[p[0] for p in comp]; ys=[p[1] for p in comp]
            area=len(comp); box=(max(xs)-min(xs)+1)*(max(ys)-min(ys)+1)
            comps.append((area,area/box if box else 0,(max(xs)-min(xs)+1)/(max(ys)-min(ys)+1)))
    def correlation(dx,dy):
        mean=sum(lum)/len(lum); a=[]; b=[]
        for y in range(SIZE):
            for x in range(SIZE): a.append(lum[y*SIZE+x]-mean); b.append(lum[wrap(y+dy)*SIZE+wrap(x+dx)]-mean)
        den=math.sqrt(sum(v*v for v in a)*sum(v*v for v in b)); return sum(x*y for x,y in zip(a,b))/den if den else 0
    internal=[]; seam=[]
    for y in range(SIZE):
        for x in range(SIZE-1): internal.append(color_distance(pixels[y*SIZE+x],pixels[y*SIZE+x+1]))
        seam.append(color_distance(pixels[y*SIZE+15],pixels[y*SIZE]))
    for x in range(SIZE):
        for y in range(SIZE-1): internal.append(color_distance(pixels[y*SIZE+x],pixels[(y+1)*SIZE+x]))
        seam.append(color_distance(pixels[15*SIZE+x],pixels[x]))
    center_rows=[sum(lum[y*SIZE+x] for x in range(SIZE))/SIZE for y in range(6,10)]
    center_cols=[sum(lum[y*SIZE+x] for y in range(SIZE))/SIZE for x in range(6,10)]
    outer=sum(lum)/256
    return {"longest_run":max(runs),"largest_component":max(a for a,_,_ in comps)/256,
            "largest_rectangularity":max(r for a,r,_ in comps if a>=12) if any(a>=12 for a,_,_ in comps) else 0,
            "periodicity":max(abs(correlation(s,0)) for s in (2,4,8)),
            "seam_ratio":(sum(seam)/len(seam))/max(1,sum(internal)/len(internal)),
            "center_cross":max(abs(sum(center_rows)/4-outer),abs(sum(center_cols)/4-outer))/max(1,palette_contrast(list(set(pixels)))),
            "transitions":sum(pixels[y*SIZE+x]!=pixels[y*SIZE+wrap(x+1)] for y in range(SIZE) for x in range(SIZE))+sum(pixels[y*SIZE+x]!=pixels[wrap(y+1)*SIZE+x] for y in range(SIZE) for x in range(SIZE))}

def validate_texture(path,pixels=None):
    width,height,pixels=read_generated_png(path) if pixels is None else (SIZE,SIZE,pixels)
    errors=[]; name=path.stem
    def fail(rule,value,limit): errors.append(f"{name}: {rule}: detected {value}, allowed {limit}")
    if (width,height)!=(SIZE,SIZE): fail("dimensions",f"{width}x{height}","16x16"); return errors
    declared=list(PALETTES.get(name,[]))
    if getattr(sys.modules[__name__],"RECIPE_DOCUMENT",None) is not None and name in NAMES:
        from texture_recipes import load_recipes
        document,resolved=load_recipes(sys.modules[__name__])
        spec=document["materials"][name]
        declared=[tuple(c) for c in spec.get("palette",declared)]
        declared += [tuple(layer["color"]) for layer in resolved[spec["recipe"]] if "color" in layer]
    colors=set(pixels); palette_limit=len(set(declared)) or 16
    if len(colors)>palette_limit: fail("palette size",len(colors),f"<= {palette_limit}")
    if name in PALETTES and not colors.issubset(set(declared)):
        fail("palette membership", "unknown color", "declared colors only")
    if any(c[3] not in (0,255) for c in pixels): fail("alpha values","non-binary","0 or 255")
    if any(c[3] and c[:3]==(0,0,0) for c in pixels): fail("opaque black outline",1,0)
    if name.endswith("_ore"):
        background=_srgb_to_oklab(PALETTES[name][1][:3])
        distances=[math.sqrt(sum((a-b)**2 for a,b in zip(background,_srgb_to_oklab(c[:3]))))
                   for c in PALETTES[name][3:]]
        if max(distances)<.10: fail("ore/background perceptual contrast",max(distances),">= .10 OKLab")
    metrics=structure_metrics(pixels)
    if name in {"grass_side", "aether_grass_side", "dry_grass_side", "leaf_litter_side"}:
        # A side-face turf cap is intentionally directional: its soil bottom
        # must not wrap vertically into its green top.  Horizontal repetition
        # still follows the normal seamless-tile contract used by greedy quads.
        internal=[color_distance(pixels[y*SIZE+x],pixels[y*SIZE+x+1])
                  for y in range(SIZE) for x in range(SIZE-1)]
        seam=[color_distance(pixels[y*SIZE+SIZE-1],pixels[y*SIZE])
              for y in range(SIZE)]
        horizontal_ratio=(sum(seam)/len(seam))/max(1,sum(internal)/len(internal))
        if horizontal_ratio>2.60:
            fail("horizontal toroidal seam discontinuity",f"{horizontal_ratio:.2f}","<= 2.60x interior")
    elif metrics["seam_ratio"]>2.60:
        fail("toroidal seam discontinuity",f"{metrics['seam_ratio']:.2f}","<= 2.60x interior")
    axes=("x",) if name in TURF_SIDE_NAMES else ("x","y")
    if name not in PLANTS | {"fire"}:
        checks=seam_metrics(pixels)
        for axis in axes:
            if checks[f"perceptual_{axis}"]["ratio"] > 2.60:
                fail(f"{axis} perceptual seam",f"{checks[f'perceptual_{axis}']['ratio']:.2f}","<= 2.60x interior")
            if name in LEAF_NAMES and checks[f"alpha_{axis}"]["ratio"] > 2.60:
                fail(f"{axis} alpha seam",f"{checks[f'alpha_{axis}']['ratio']:.2f}","<= 2.60x interior")
    # Large calm planes are intentional in v4. Seam and palette checks above
    # still reject broken boundaries; frequency metrics are reported for review.
    if name in {"grass_side", "aether_grass_side", "dry_grass_side", "leaf_litter_side"}:
        grass=set(PALETTES[name][4:]); rows=[y for y in range(SIZE) for x in range(SIZE) if pixels[y*SIZE+x] in grass]
        top_coverage=sum(pixels[y*SIZE+x] in grass for y in range(5) for x in range(SIZE))
        if top_coverage != 5*SIZE: fail("grass-side solid turf cap",top_coverage,f"= {5*SIZE}")
        if not rows or sum(rows)/len(rows)>=4: fail("grass-side top layer mean row",f"{sum(rows)/len(rows) if rows else 99:.2f}","< 4")
    if name.endswith("_ore"):
        ore=set(PALETTES[name][3:]); cells={(x,y) for y in range(SIZE) for x in range(SIZE) if pixels[y*SIZE+x] in ore}; count=0
        while cells:
            count+=1; queue=[cells.pop()]
            while queue:
                for n in neighbors4(*queue.pop()):
                    if n in cells: cells.remove(n); queue.append(n)
        if not 2<=count<=4: fail("connected ore clusters",count,"2..4")
    return errors

def generate(output,seed,local_seeds=None):
    output.mkdir(parents=True,exist_ok=True)
    sources={}
    for name in NAMES:
        from texture_recipes import generate_material
        result=generate_material(sys.modules[__name__],name,seed,local_seeds=local_seeds)
        pixels=result["pixels"]
        result["pixel_sha256"]=hashlib.sha256(bytes(c for p in pixels for c in p)).hexdigest()
        (output/f"{name}.semantic.json").write_text(json.dumps({k:v for k,v in result.items() if k!="pixels"},sort_keys=True)+"\n")
        write_png(output/f"{name}.png",SIZE,SIZE,pixels)
        sources[name]={"effective_seed":resolve_seed(seed,name,local_seeds),
                       "sha256":hashlib.sha256(bytes(c for p in pixels for c in p)).hexdigest()}
    manifest={"generator_version":GENERATOR_VERSION,"seed":seed,"materials":sources}
    (output/"generation.json").write_text(json.dumps(manifest,indent=2,sort_keys=True)+"\n")


def validate(output):
    errors=[]
    for name in NAMES:
        path=output/f"{name}.png"
        try:
            errors.extend([f"missing texture: {path}"] if not path.exists() else validate_texture(path))
        except (OSError,ValueError) as error:
            errors.append(str(error))
    if errors: raise ValueError("\n".join(errors))


def load_material_profiles(path=None):
    data=_read_definition(path or DEFINITION_ROOT/"material_profiles.json")
    if data.get("version")!=1 or data.get("generator_version")!=GENERATOR_VERSION or data.get("style")!=STYLE_ID:
        raise ValueError("material profiles do not match generator version/style")
    patterns={"clods","tufts","flakes","mineral","fibers","leaf","grains","flat","panels","crystal","weave"}
    if set(data.get("materials", {}))-set(NAMES):
        raise ValueError("material profile contains unknown material")
    profiles={}
    for name in NAMES:
        profile=dict(data.get("families", {}).get(TEXTURE_FAMILIES[name], {}))
        profile.update(data.get("materials", {}).get(name, {}))
        if profile.get("pattern") not in patterns:
            raise ValueError(f"unknown material geometry pattern for {name}")
        for key in ("roughness","metallic","emission","height_strength"):
            value=profile.get(key)
            if not isinstance(value,(float,int)) or not math.isfinite(value) or not 0<=value<=1:
                raise ValueError(f"invalid material profile {name}.{key}")
        if "emission_roles" in profile and (not isinstance(profile["emission_roles"],list) or
            not profile["emission_roles"] or any(type(role) is not int or
                not 0<=role<len(PALETTES[name]) for role in profile["emission_roles"])):
            raise ValueError(f"invalid material profile {name}.emission_roles")
        profiles[name]=profile
    return profiles


def generate_material_maps(name,pixels,profile,roles=None,masks=None):
    """Derive geometry from material roles, never treating hue as elevation.

    R/G/B/A property channels are roughness/metallic/emission/height, linear.
    Cutout air has flat normals and no emission; its RGB never forms a crater.
    """
    palette=PALETTES[name]
    pattern=profile["pattern"]
    heights=[]
    for y in range(SIZE):
        for x in range(SIZE):
            pixel=pixels[y*SIZE+x]
            role=roles[y*SIZE+x] if roles is not None else getattr(pixel,"role",palette.index(pixel) if pixel in palette else 2)
            normalized=role/max(1,len(palette)-1)
            if pattern=="flat": h=.5
            elif pattern=="weave": h=.5+.10*math.sin(x*math.tau/4)*math.sin(y*math.tau/4)
            elif pattern=="fibers": h=.5+.22*(normalized-.5)
            elif pattern=="mineral": h=.42+.10*min(role,2)/2+.12*(role>=3)
            elif pattern in {"leaf","tufts"}: h=.5+.18*(normalized-.5)
            elif pattern=="crystal": h=.5+.08*(normalized-.5)
            elif pattern=="grains": h=.5+.12*(normalized-.5)
            elif pattern=="panels": h=.5+.24*(normalized-.5)
            else: h=.5+.30*(normalized-.5)
            if masks:
                for mask, overrides in masks.get("properties", []):
                    if mask[y*SIZE+x]: h=overrides.get("height",h)
            heights.append(h if pixel[3] else .5)
    normals=[];properties=[];height_pixels=[]
    def neighbor(x,y,center):
        x=wrap(x)
        y=max(0,min(SIZE-1,y)) if name in TURF_SIDE_NAMES or name in PLANTS else wrap(y)
        index=y*SIZE+x
        return heights[index] if pixels[index][3] else center
    byte=lambda value:max(0,min(255,round(value*255)))
    for y in range(SIZE):
        for x in range(SIZE):
            i=y*SIZE+x; h=heights[i]; visible=bool(pixels[i][3])
            strength=profile["height_strength"]*4 if visible else 0
            nx=(neighbor(x-1,y,h)-neighbor(x+1,y,h))*strength
            ny=(neighbor(x,y+1,h)-neighbor(x,y-1,h))*strength
            length=math.sqrt(nx*nx+ny*ny+1)
            normals.append((byte(.5+.5*nx/length),byte(.5+.5*ny/length),byte(.5+.5/length),255))
            mineral=pattern=="mineral" and (roles[i]>=3 if roles is not None else pixels[i] in palette[3:])
            roughness=profile["roughness"] if pattern!="mineral" or mineral else .80
            metallic=profile["metallic"] if pattern!="mineral" or mineral else 0
            if masks:
                for mask, overrides in masks.get("properties", []):
                    if mask[i]:
                        roughness=overrides.get("roughness",roughness)
                        metallic=overrides.get("metallic",metallic)
            emission=profile["emission"]
            if "emission_roles" in profile:
                role = roles[i] if roles is not None else getattr(pixels[i],"role",palette.index(pixels[i]) if pixels[i] in palette else 2)
                if role not in profile["emission_roles"]: emission=0
            if masks:
                for mask, overrides in masks.get("properties", []):
                    if mask[i]: emission=overrides.get("emission",emission)
            properties.append((byte(roughness),byte(metallic),byte(emission) if visible else 0,byte(h)))
            height_pixels.append((byte(h),byte(h),byte(h),255))
    return normals,properties,height_pixels


def build_atlas(output,seed,local_seeds=None):
    from texture_recipes import build_sequence_atlas
    return build_sequence_atlas(sys.modules[__name__],output,seed,local_seeds)


def build_entity_atlas(output,seed):
    columns=math.ceil(math.sqrt(len(ENTITY_NAMES))); rows=columns
    atlas=[(0,0,0,255)]*(columns*SIZE*rows*SIZE)
    metadata={"version":1,"generator_version":GENERATOR_VERSION,"style":STYLE_ID,
              "tile_size":SIZE,"columns":columns,"rows":rows,
              "filter":"nearest","seed":seed,
              "style_definition":"definitions/entity_styles.json","entities":{}}
    entity_dir=output/"entities"; entity_dir.mkdir(parents=True,exist_ok=True)
    for index,name in enumerate(ENTITY_NAMES):
        pixels=generate_entity_texture(name,seed)
        write_png(entity_dir/f"{name}.png",SIZE,SIZE,pixels)
        tx,ty=index%columns,index//columns
        for y in range(SIZE):
            begin=(ty*SIZE+y)*columns*SIZE+tx*SIZE
            atlas[begin:begin+SIZE]=pixels[y*SIZE:(y+1)*SIZE]
        metadata["entities"][name]={"index":index,"x":tx,"y":ty,
                                    "source":f"entities/{name}.png",
                                    "features":ENTITY_STYLE_DEFINITIONS["entities"].get(name, {}).get("documentation", {}).get("motifs", [])}
    write_png(output/"entity_atlas.png",columns*SIZE,rows*SIZE,atlas)
    (output/"entity_atlas.json").write_text(
        json.dumps(metadata,indent=2,sort_keys=True)+"\n",encoding="utf-8")

def build_app_icon(output,seed):
    """Build an opaque, nearest-filtered grass cube application icon."""
    size=64
    canvas=[]
    for y in range(size):
        shade=27+(y*18)//(size-1)
        canvas.extend([(shade,shade+8,shade+14,255)]*size)
    top=generate_texture("grass_top",seed)
    side=generate_texture("grass_side",seed)
    dirt=generate_texture("dirt",seed)
    for y in range(20,57):
        inset=(y-20)//2
        for x in range(8+inset,33):
            u=((x-(8+inset))*15)//max(1,32-(8+inset))
            v=((y-20)*15)//36
            canvas[y*size+x]=side[v*SIZE+u]
        for x in range(33,57-inset):
            u=((x-33)*15)//max(1,(56-inset)-33)
            v=((y-20)*15)//36
            color=dirt[v*SIZE+u]
            canvas[y*size+x]=(max(1,color[0]-12),max(1,color[1]-12),
                              max(1,color[2]-12),255)
    for y in range(8,35):
        half_width=min(y-8,34-y)*2
        if half_width<0: continue
        left=32-half_width; right=32+half_width
        for x in range(left,right+1):
            u=((x-left)*15)//max(1,right-left)
            v=((y-8)*15)//26
            canvas[y*size+x]=top[v*SIZE+u]
    scale=16
    pixels=[]
    for color in canvas:
        pixels.extend([color]*scale)
    scaled=[]
    row=size*scale
    for y in range(size):
        source=pixels[y*row:(y+1)*row]
        for _ in range(scale): scaled.extend(source)
    output.parent.mkdir(parents=True,exist_ok=True)
    write_png(output,size*scale,size*scale,scaled)

def build_ios_app_icon(output,seed):
    build_app_icon(output,seed)

def resize_nearest(pixels,width,height,size):
    return [pixels[(y*height//size)*width+(x*width//size)]
            for y in range(size) for x in range(size)]

def build_desktop_app_icons(output,seed):
    """Build Linux PNG, Windows ICO, and macOS ICNS from one icon image."""
    output.mkdir(parents=True,exist_ok=True)
    source=output/"minecraftc.png"
    build_app_icon(source,seed)
    width,height,pixels=read_generated_png(source)

    icon256=png_bytes(256,256,resize_nearest(pixels,width,height,256))
    ico_header=struct.pack("<HHH",0,1,1)
    ico_entry=struct.pack("<BBBBHHII",0,0,0,0,1,32,len(icon256),22)
    (output/"minecraftc.ico").write_bytes(ico_header+ico_entry+icon256)

    icns_chunks=[]
    for kind,size in ((b"ic08",256),(b"ic09",512),(b"ic10",1024)):
        data=png_bytes(size,size,resize_nearest(pixels,width,height,size))
        icns_chunks.append(kind+struct.pack(">I",len(data)+8)+data)
    body=b"".join(icns_chunks)
    (output/"minecraftc.icns").write_bytes(b"icns"+struct.pack(">I",len(body)+8)+body)

# Compact 3x5 bitmap glyphs keep contact sheets dependency-free.
FONT={c:bits for c,bits in {
"0":"111101101101111","1":"010110010010111","2":"111001111100111","3":"111001111001111",
"4":"101101111001001","5":"111100111001111","6":"111100111101111","7":"111001010010010",
"8":"111101111101111","9":"111101111001111","a":"010101111101101","b":"100100110101110",
"c":"011100100100011","d":"001001011101011","e":"010101111100011","f":"011100110100100",
"g":"011100101101010","h":"100100110101101","i":"010000110010111","j":"001001001101010",
"k":"100101110101101","l":"100100100100111","m":"101111111101101","n":"110101101101101",
"o":"010101101101010","p":"110101110100100","q":"010101101011001","r":"110101110101101",
"s":"011100010001110","t":"111010010010010","u":"101101101101111","v":"101101101101010",
"w":"101101111111101","x":"101101010101101","y":"101101010010010","z":"111001010100111",
"_":"000000000000111","-":"000000111000000",":":"000010000010000"}.items()}

def blit(canvas,width,x0,y0,tile,tw,th,scale=1):
    for y in range(th):
        for x in range(tw):
            color=tile[y*tw+x]
            for sy in range(scale):
                start=(y0+y*scale+sy)*width+x0+x*scale
                canvas[start:start+scale]=[color]*scale

def draw_text(canvas,width,x,y,label):
    for char in label.lower():
        bits=FONT.get(char,"000000000000000")
        for gy in range(5):
            for gx in range(3):
                if bits[gy*3+gx]=="1": canvas[(y+gy)*width+x+gx]=(235,235,228,255)
        x+=4

def build_contact_sheet(output,seed,count,local_seeds=None):
    # One readable grid per candidate, rather than a many-metre vertical strip.
    columns=8; cell_w=144; cell_h=100
    width=columns*cell_w; height=math.ceil(len(NAMES)/columns)*cell_h
    candidate_seeds=[seed if col==0 else mix64(seed+col*0x9e37)&0x7fffffff
                     for col in range(count)]
    pages=[]
    for candidate_index,candidate in enumerate(candidate_seeds):
        canvas=[(25,27,29,255)]*(width*height)
        for index,name in enumerate(NAMES):
            x=(index%columns)*cell_w; y=(index//columns)*cell_h
            from texture_recipes import generate_material
            source=output/f"{name}.png"
            pixels=read_generated_png(source)[2] if candidate_index==0 and source.exists() else generate_material(
                sys.modules[__name__],name,candidate,local_seeds if candidate_index==0 else None)["pixels"]
            draw_text(canvas,width,x+3,y+4,name[:34])
            blit(canvas,width,x+4,y+20,pixels,SIZE,SIZE,3)
            blit(canvas,width,x+4,y+76,pixels,SIZE,SIZE)
            for ty in range(4):
                for tx in range(4):
                    blit(canvas,width,x+72+tx*SIZE,y+20+ty*SIZE,pixels,SIZE,SIZE)
        page="contact_sheet.png" if candidate_index==0 else f"contact_sheet_{candidate_index}.png"
        write_png(output/page,width,height,canvas); pages.append(page)
    (output/"contact_sheet.json").write_text(json.dumps({"base_seed":seed,
        "candidate_seeds":candidate_seeds,"pages":pages,"local_seeds":local_seeds or {},
        "note":"development preview; excluded from atlas"},indent=2,sort_keys=True)+"\n")


def _shade_preview(pixels, multiplier, tint=(1.0, 1.0, 1.0)):
    """Apply an approximate game-light profile in linear RGB for previews."""
    shaded=[]
    for color in pixels:
        if not color[3]:
            shaded.append(color)
            continue
        rgb=tuple(_linear_to_srgb(_srgb_to_linear(channel) * multiplier * tint[index])
                  for index, channel in enumerate(color[:3]))
        shaded.append(rgb + (color[3],))
    return shaded


def build_block_preview(output, seed):
    """Build a readable block board with tile, repeat, and lighting samples."""
    names=("dirt", "grass_top", "grass_side", "stone", "oak_planks", "sand",
           "snow", "white_wool", "deepslate", "obsidian", "water", "lava",
           "crafting_table", "furnace", "chest", "cloudstone")
    profiles=(
        ("noon", 1.00, (1.00, 1.00, 1.00)),
        ("dusk", 0.78, (1.05, 0.86, 0.72)),
        ("cave", 0.54, (0.84, 0.91, 1.08)),
    )
    columns=4; cell_w=132; cell_h=78; rows=math.ceil(len(names)/columns)
    wall_h=112; width=columns*cell_w; height=rows*cell_h+wall_h
    canvas=[(24,27,31,255)]*(width*height)
    for index,name in enumerate(names):
        tx,ty=index%columns,index//columns; x0=tx*cell_w; y0=ty*cell_h
        draw_text(canvas,width,x0+3,y0+2,name[:16])
        pixels=read_generated_png(output/f"{name}.png")[2]
        # Original tile and an 8x8 repeat share a cell, then three small
        # lighting swatches make the palette readable under game conditions.
        blit(canvas,width,x0+3,y0+14,pixels,SIZE,SIZE,2)
        for profile_index,(_,multiplier,tint) in enumerate(profiles):
            sample_pixels=_shade_preview(pixels,multiplier,tint)
            blit(canvas,width,x0+40+profile_index*18,y0+14,sample_pixels,SIZE,SIZE,1)
        for repeat_y in range(2):
            for repeat_x in range(2):
                blit(canvas,width,x0+40+repeat_x*16,y0+48+repeat_y*16,
                     pixels,SIZE,SIZE,1)
    base_y=rows*cell_h
    draw_text(canvas,width,4,base_y+3,"grass_combo")
    grass_top=read_generated_png(output/"grass_top.png")[2]
    grass_side=read_generated_png(output/"grass_side.png")[2]
    blit(canvas,width,4,base_y+18,grass_top,SIZE,SIZE,2)
    blit(canvas,width,40,base_y+18,grass_side,SIZE,SIZE,2)
    draw_text(canvas,width,78,base_y+3,"wall")
    wall=("stone", "oak_planks", "bricks" if "bricks" in NAMES else "cobblestone",
          "sand", "white_wool", "crafting_table")
    for index,name in enumerate(wall):
        pixels=read_generated_png(output/f"{name}.png")[2]
        wx=78+(index%3)*32; wy=base_y+18+(index//3)*32
        blit(canvas,width,wx,wy,pixels,SIZE,SIZE,2)
    draw_text(canvas,width,188,base_y+3,"light")
    for profile_index,(label,multiplier,tint) in enumerate(profiles):
        draw_text(canvas,width,188+profile_index*42,base_y+18,label)
        blit(canvas,width,188+profile_index*42,base_y+31,
             _shade_preview(grass_top,multiplier,tint),SIZE,SIZE,1)
    write_png(output/"block_preview.png",width,height,canvas)


def _final_item_paths(output):
    metadata=output/"items_atlas.json"
    if not metadata.exists():
        return []
    names=_read_definition(metadata)["items"]
    return [output/"items"/f"{name}.png" for name in sorted(names)]


def _item_contact_sheet(output):
    paths=_final_item_paths(output)
    if not paths:
        return
    columns=8; cell_w=72; cell_h=48; label_h=10
    rows=math.ceil(len(paths)/columns)
    canvas=[(28,30,33,255)]*(columns*cell_w*rows*cell_h)
    for index,path in enumerate(paths):
        tx,ty=index%columns,index//columns; x0=tx*cell_w; y0=ty*cell_h
        _,_,pixels=read_generated_png(path)
        blit(canvas,columns*cell_w,x0+20,y0+label_h,pixels,SIZE,SIZE,2)
        draw_text(canvas,columns*cell_w,x0+2,y0+2,path.stem[:10])
    write_png(output/"items_contact_sheet.png",columns*cell_w,rows*cell_h,canvas)


def _entity_contact_sheet(output):
    paths=sorted((output/"entity_skins").glob("*.png"))
    if not paths:
        return
    columns=3; cell_w=136; cell_h=150
    rows=math.ceil(len(paths)/columns)
    canvas=[(28,30,33,255)]*(columns*cell_w*rows*cell_h)
    for index,path in enumerate(paths):
        tx,ty=index%columns,index//columns; x0=tx*cell_w; y0=ty*cell_h
        _,_,pixels=read_generated_png(path)
        blit(canvas,columns*cell_w,x0+4,y0+14,pixels,64,64,2)
        draw_text(canvas,columns*cell_w,x0+4,y0+4,path.stem)
    write_png(output/"entity_contact_sheet.png",columns*cell_w,rows*cell_h,canvas)


def _entity_semantic_preview(output):
    """Show full skins alongside the semantic head/body/limb tiles."""
    paths=sorted((output/"entity_skins").glob("*.png"))
    if not paths:
        return
    columns=3; cell_w=220; cell_h=112; rows=math.ceil(len(paths)/columns)
    canvas=[(28,30,33,255)]*(columns*cell_w*rows*cell_h)
    for index,path in enumerate(paths):
        tx,ty=index%columns,index//columns; x0=tx*cell_w; y0=ty*cell_h
        _,_,pixels=read_generated_png(path)
        draw_text(canvas,columns*cell_w,x0+3,y0+3,path.stem)
        blit(canvas,columns*cell_w,x0+3,y0+16,pixels,64,64,1)
        for offset,semantic in enumerate(("head_front", "body_front", "limb_primary")):
            tile_index=ENTITY_SKIN_LAYOUT[semantic]; sx=tile_index%4; sy=tile_index//4
            tile=[pixels[(sy*16+y)*64+sx*16:(sy*16+y)*64+sx*16+16]
                  for y in range(16)]
            flat=[color for row in tile for color in row]
            blit(canvas,columns*cell_w,x0+76+offset*38,y0+30,flat,SIZE,SIZE,2)
    write_png(output/"entity_semantic_preview.png",columns*cell_w,rows*cell_h,canvas)


def _visual_color_stats(pixels):
    opaque=[p for p in pixels if p[3]]
    if not opaque:
        return {"opaque":0,"alpha_ratio":0.0}
    labs=[_srgb_to_oklab(p[:3]) for p in opaque]
    lightness=sorted(l[0] for l in labs)
    chroma=[math.sqrt(l[1]*l[1]+l[2]*l[2]) for l in labs]
    def percentile(values, fraction):
        return values[min(len(values)-1,max(0,int(round((len(values)-1)*fraction))))]
    jumps=[]
    if len(pixels)==256:
        for y in range(16):
            for x in range(16):
                p=pixels[y*16+x]
                for nx,ny in ((wrap(x+1),y),(x,wrap(y+1))):
                    q=pixels[ny*16+nx]
                    if p[3] and q[3]: jumps.append(color_distance(p,q))
    return {
        "mean_neighbor_delta":round(sum(jumps)/len(jumps),5) if jumps else 0.0,
        "dark_fraction":round(sum(l<.45 for l in lightness)/len(lightness),5),
        "opaque":len(opaque),
        "alpha_ratio":round(len(opaque)/max(1,len(pixels)),4),
        "oklab_lightness_mean":round(sum(lightness)/len(lightness),5),
        "oklab_lightness_p10":round(percentile(lightness,0.10),5),
        "oklab_lightness_p90":round(percentile(lightness,0.90),5),
        "oklch_chroma_mean":round(sum(chroma)/len(chroma),5),
        "palette_size":len(set(opaque)),
    }


def _luminance_correlation(first, second):
    if len(first) != len(second) or not first:
        return 0.0
    a=[luminance(color) for color in first]
    b=[luminance(color) for color in second]
    mean_a=sum(a)/len(a); mean_b=sum(b)/len(b)
    centered_a=[value-mean_a for value in a]; centered_b=[value-mean_b for value in b]
    denominator=math.sqrt(sum(value*value for value in centered_a) *
                          sum(value*value for value in centered_b))
    return sum(left*right for left,right in zip(centered_a,centered_b)) / denominator \
        if denominator else 0.0


def build_visual_report(output,seed):
    """Emit machine-readable visual QA data beside development previews."""
    report={"version":1,"generator_version":GENERATOR_VERSION,"style":STYLE_ID,
            "seed":seed,"textures":{},"items":{},"entities":{},
            "style_exceptions":STYLE_DEFINITION.get("exceptions", {}),
            "palette_roles":STYLE_DEFINITION.get("palette_roles", []),
            "previews": {
                "blocks": "block_preview.png",
                "mips": "mip_preview.png",
                "items": "items_contact_sheet.png",
                "entities": "entity_contact_sheet.png",
                "entity_semantic": "entity_semantic_preview.png",
                "lighting_profiles": ["noon", "dusk", "cave"],
            }}
    texture_pixels={}
    report["effective_materials"] = json.loads((output/"atlas.json").read_text())["textures"] if (output/"atlas.json").exists() else {}
    for name in NAMES:
        path=output/f"{name}.png"
        if path.exists():
            _,_,pixels=read_generated_png(path)
            texture_pixels[name]=pixels
            structure=structure_metrics(pixels)
            structure["seams"]=seam_metrics(pixels)
            structure["edge_density"]=round(
                structure["transitions"] / float(2 * SIZE * SIZE), 5)
            report["textures"][name]=dict(_visual_color_stats(pixels),
                                          family=TEXTURE_FAMILIES.get(name,"constructed"),
                                          palette_roles=STYLE_DEFINITION.get("palette_roles", []),
                                          structure=structure,
                                          mips=[dict(_visual_color_stats(tile),tile_size=size)
                                                for size,tile in tile_mip_chain(pixels,name in LEAF_NAMES)])
    family_groups={}
    for name,family in TEXTURE_FAMILIES.items():
        if name in texture_pixels:
            family_groups.setdefault(family, []).append(name)
    family_similarity={}
    for family,names in sorted(family_groups.items()):
        pairs=[abs(_luminance_correlation(texture_pixels[first], texture_pixels[second]))
               for index,first in enumerate(names) for second in names[index+1:]]
        family_similarity[family]={"count":len(names),
                                   "mean_abs_correlation":round(sum(pairs)/len(pairs),5) if pairs else 0.0,
                                   "max_abs_correlation":round(max(pairs),5) if pairs else 0.0}
    report["family_structure_similarity"]=family_similarity
    for path in _final_item_paths(output):
        _,_,pixels=read_generated_png(path)
        report["items"][path.stem]=_visual_color_stats(pixels)
    for path in sorted((output/"entity_skins").glob("*.png")):
        _,_,pixels=read_generated_png(path)
        report["entities"][path.stem]=_visual_color_stats(pixels)
    (output/"visual_report.json").write_text(
        json.dumps(report,indent=2,sort_keys=True)+"\n",encoding="utf-8")


def tile_mip_chain(pixels, preserve_cutout=False):
    """Reference color mips in linear light; coverage correction never feeds back."""
    levels=[(SIZE,list(pixels))]
    source=list(pixels); size=SIZE
    base_holes=sum(p[3]==0 for p in pixels)
    while size>1:
        next_size=size//2; down=[]
        for y in range(next_size):
            for x in range(next_size):
                samples=[source[(y*2+dy)*size+x*2+dx] for dy in range(2) for dx in range(2)]
                alpha=sum(p[3] for p in samples)
                rgb=tuple(_linear_to_srgb(sum(_srgb_to_linear(p[c])*p[3] for p in samples)/alpha)
                          if alpha else 0 for c in range(3))
                down.append(rgb+(alpha//4,))
        source=down; size=next_size
        display=list(down)
        if preserve_cutout and size>1 and base_holes:
            count=min(size*size-1,max(1,(base_holes*size*size+128)//256))
            order=sorted(range(len(display)),key=lambda i:display[i][3])
            for rank,i in enumerate(order):display[i]=display[i][:3]+(0 if rank<count else 255,)
        levels.append((size,display))
    return levels


def build_mip_preview(output):
    columns=4; cell_w=244; cell_h=90
    width=columns*cell_w; height=math.ceil(len(NAMES)/columns)*cell_h
    canvas=[(29,32,35,255)]*(width*height)
    for index,name in enumerate(NAMES):
        x0=(index%columns)*cell_w; y0=(index//columns)*cell_h
        draw_text(canvas,width,x0+3,y0+3,name[:34])
        pixels=read_generated_png(output/f"{name}.png")[2]
        for level,(size,tile) in enumerate(tile_mip_chain(pixels,name in LEAF_NAMES)):
            x=x0+4+level*46
            draw_text(canvas,width,x,y0+16,str(size))
            blit(canvas,width,x,y0+28,tile,size,size,max(1,32//size))
    write_png(output/"mip_preview.png",width,height,canvas)


def build_preview(output,seed,count=3,local_seeds=None):
    build_contact_sheet(output,seed,count,local_seeds)
    build_block_preview(output,seed)
    build_mip_preview(output)
    _item_contact_sheet(output)
    _entity_contact_sheet(output)
    _entity_semantic_preview(output)
    build_visual_report(output,seed)

def parse_local_seeds(values):
    result={}
    for value in values:
        if "=" not in value: raise ValueError(f"invalid --local-seed '{value}', expected material=seed")
        name,raw=value.split("=",1)
        if name not in NAMES: raise ValueError(f"unknown --local-seed material '{name}'")
        result[name]=int(raw,0)
    return result

def parse_args(argv=None):
    parser=argparse.ArgumentParser(); parser.add_argument("--generate",action="store_true")
    parser.add_argument("--validate",action="store_true"); parser.add_argument("--build-atlas",action="store_true")
    parser.add_argument("--recipes",type=Path,help="versioned recipe document override")
    parser.add_argument("--seed",type=int,default=DEFAULT_SEED); parser.add_argument("--output",type=Path,default=Path("assets/textures/generated"))
    parser.add_argument("--candidate-count",type=int,default=1); parser.add_argument("--contact-sheet",action="store_true")
    parser.add_argument("--preview",action="store_true",help="build all assets and development previews")
    parser.add_argument("--visual-report",action="store_true",help="write visual_report.json")
    parser.add_argument("--local-seed",action="append",default=[],metavar="MATERIAL=SEED")
    parser.add_argument("--build-items-atlas",action="store_true")
    parser.add_argument("--build-entity-atlas",action="store_true")
    parser.add_argument("--build-entity-skins",action="store_true")
    parser.add_argument("--build-ios-icon",action="store_true")
    parser.add_argument("--build-android-icon",action="store_true")
    parser.add_argument("--build-desktop-icons",action="store_true")
    parser.add_argument("--ios-icon-output",type=Path,default=Path(
        "ios/Assets.xcassets/AppIcon.appiconset/AppIcon.png"))
    parser.add_argument("--android-icon-output",type=Path,default=Path(
        "android/app/src/main/res/drawable-nodpi/app_icon.png"))
    parser.add_argument("--item-definitions",type=Path,default=Path("assets/textures/definitions/item_icons.json"))
    parser.add_argument("--block-definitions",type=Path,default=Path("assets/textures/definitions/blocks.json"))
    parser.add_argument("--item-overrides",type=Path,default=Path("assets/textures/source/items"))
    parser.add_argument("--allow-item-fallback",action="store_true",help="explicitly allow reported legacy/missing item fallbacks")
    parser.add_argument("--legacy-items",type=Path,default=Path("assets/textures/legacy/items"))
    return parser.parse_args(argv)

def main(argv=None):
    args=parse_args(argv)
    if args.recipes:
        global RECIPE_DOCUMENT
        RECIPE_DOCUMENT=_read_definition(args.recipes)
        from texture_recipes import load_recipes
        load_recipes(sys.modules[__name__])
    if not (args.generate or args.validate or args.build_atlas or args.build_items_atlas or args.build_entity_atlas or args.build_entity_skins or args.build_ios_icon or args.build_android_icon or args.build_desktop_icons or args.contact_sheet or args.preview or args.visual_report): raise SystemExit("select a generation, validation, or atlas operation")
    try:
        if args.candidate_count<1: raise ValueError("--candidate-count must be at least 1")
        local_seeds=parse_local_seeds(args.local_seed)
        if args.preview:
            generate(args.output,args.seed,local_seeds)
            validate(args.output)
            build_atlas(args.output,args.seed,local_seeds)
            build_items_atlas(args.output,args.seed,args.item_definitions,args.block_definitions,args.item_overrides,args.legacy_items,args.allow_item_fallback)
            build_entity_atlas(args.output,args.seed)
            build_entity_skins(args.output,args.seed)
        if args.generate: generate(args.output,args.seed,local_seeds)
        if args.validate: validate(args.output)
        if args.build_atlas: build_atlas(args.output,args.seed,local_seeds)
        if args.build_items_atlas: build_items_atlas(args.output,args.seed,args.item_definitions,args.block_definitions,args.item_overrides,args.legacy_items,args.allow_item_fallback)
        if args.build_entity_atlas: build_entity_atlas(args.output,args.seed)
        if args.build_entity_skins: build_entity_skins(args.output,args.seed)
        if args.build_ios_icon: build_app_icon(args.ios_icon_output,args.seed)
        if args.build_android_icon: build_app_icon(args.android_icon_output,args.seed)
        if args.build_desktop_icons: build_desktop_app_icons(Path("packaging/icons"),args.seed)
        if args.contact_sheet: build_contact_sheet(args.output,args.seed,args.candidate_count,local_seeds)
        if args.preview:
            build_preview(args.output,args.seed,args.candidate_count,local_seeds)
            from texture_recipes import build_sequence_preview
            build_sequence_preview(sys.modules[__name__],args.output)
        if args.visual_report: build_visual_report(args.output,args.seed)
    except (OSError,ValueError) as error: print(error,file=sys.stderr); return 1
    return 0

if __name__=="__main__": raise SystemExit(main())
