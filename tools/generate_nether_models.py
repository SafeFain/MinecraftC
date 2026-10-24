#!/usr/bin/env python3
"""Original native Nether cuboid rigs, semantic skins and action graphs.

Uses the project's existing glTF writer; it never imports Minecraft assets.
"""
import argparse
import json
import struct
import copy
import math
from pathlib import Path

import generate_entity_models as models
import texture_generator as textures

DEFINITIONS = {
    "ghast": ("spider", (4, 4, 4), (225, 220, 213)),
    "blaze": ("spider", (.6, 1.8, .6), (231, 165, 42)),
    "wither_skeleton": ("zombie", (.7, 2.4, .7), (47, 43, 48)),
    "magma_cube": ("pig", (.51, .51, .51), (105, 28, 13)),
    "piglin": ("zombie", (.6, 1.95, .6), (187, 125, 95)),
    "piglin_brute": ("zombie", (.6, 1.95, .6), (156, 99, 69)),
    "zombified_piglin": ("zombie", (.6, 1.95, .6), (119, 148, 91)),
    "hoglin": ("pig", (1.4, 1.4, 1.4), (170, 99, 69)),
    "zoglin": ("pig", (1.4, 1.4, 1.4), (134, 133, 94)),
    "strider": ("zombie", (.9, 1.7, .9), (184, 47, 51)),
    "enderman": ("zombie", (.6, 2.9, .6), (31, 24, 37)),
    "ghast_fireball": ("pig", (.5, .5, .5), (248, 132, 28)),
    "blaze_fireball": ("pig", (.25, .25, .25), (250, 180, 49)),
    "thrown_potion": ("pig", (.2, .35, .2), (128, 105, 189)),
    "ender_pearl": ("pig", (.2, .2, .2), (30, 132, 122)),
}


def parts(name, base_parts):
    if name in ("ghast", "blaze"):
        if name == "ghast":
            result = [("body", (0, 2.8, 0), (4, 2.4, 4)), ("head", (0, 2.8, -1.98), (3.8, 2.2, .04))]
            for side, x in (("l", -1), ("r", 1)):
                for i in range(4):
                    result.append((f"leg_{side}{i}", (x * (1.1 if i < 2 else .4), .8, (i % 2) * 1.4 - .7), (.3, 1.6, .3)))
            return result
        result = [("body", (0, 1, 0), (.25, .7, .25)), ("head", (0, 1.55, 0), (.6, .5, .6))]
        for side, phase in (("l", 0), ("r", math.pi)):
            for i in range(4):
                angle = i * math.pi / 2 + phase / 2
                result.append((f"leg_{side}{i}", (.38 * math.cos(angle), .35 + i * .28, .38 * math.sin(angle)), (.12, .45, .12)))
        return result
    if name in ("hoglin", "zoglin"):
        result = [(part, (c[0] * 1.35, c[1] * 1.1, c[2]), (d[0] * 1.4, d[1] * 1.2, d[2])) for part, c, d in base_parts]
        return result + [("tusk_l", (-.32, .75, -.92), (.1, .35, .13)), ("tusk_r", (.32, .75, -.92), (.1, .35, .13))]
    if name in ("magma_cube", "ghast_fireball", "blaze_fireball", "thrown_potion", "ender_pearl"):
        size = DEFINITIONS[name][1]
        result = [("body", (0, size[1] / 2, 0), size), ("head", (0, size[1] * .6, -size[2] * .501), (size[0] * .8, size[1] * .25, .01))]
        for leg, x, z in (("fl", -1, -1), ("fr", 1, -1), ("bl", -1, 1), ("br", 1, 1)):
            result.append(("leg_" + leg, (x * size[0] * .25, .01, z * size[2] * .25), (.01, .01, .01)))
        return result
    if name == "enderman":
        return [("body", (0, 1.8, 0), (.4, .9, .25)), ("head", (0, 2.65, 0), (.6, .5, .6)),
                ("arm_l", (-.31, 1.5, 0), (.12, 1.8, .12)), ("arm_r", (.31, 1.5, 0), (.12, 1.8, .12)),
                ("leg_l", (-.13, .7, 0), (.12, 1.4, .12)), ("leg_r", (.13, .7, 0), (.12, 1.4, .12))]
    if name == "strider":
        return [("body", (0, 1.15, 0), (.9, .9, .9)), ("head", (0, 1.2, -.451), (.82, .75, .01)),
                ("leg_l", (-.24, .35, 0), (.2, .7, .2)), ("leg_r", (.24, .35, 0), (.2, .7, .2)),
                ("arm_l", (-.58, 1.3, 0), (.35, .03, .35)), ("arm_r", (.58, 1.3, 0), (.35, .03, .35))]
    if name == "wither_skeleton":
        return [(part, (c[0], c[1] * 1.3, c[2]), (d[0] * .7, d[1] * 1.3, d[2] * .7)) for part, c, d in base_parts]
    result = [(part, (c[0], c[1] * 1.08, c[2]), d) for part, c, d in base_parts]
    return result + [("snout", (0, 1.65, -.30), (.28, .18, .22)),
                     ("ear_l", (-.32, 1.8, 0), (.18, .22, .08)), ("ear_r", (.32, 1.8, 0), (.18, .22, .08))]


def skin(name, color):
    result = []
    for y in range(64):
        for x in range(64):
            tx, ty = x % 16, y % 16
            slot = (y // 16) * 4 + x // 16
            gain = .86 + ((tx // 3 + ty // 4 + slot) % 4) * .055
            c = tuple(max(8, min(255, int(channel * gain))) for channel in color)
            if slot == textures.ENTITY_SKIN_LAYOUT["head_front"]:
                if ty in (5, 6) and tx in (3, 4, 10, 11):
                    c = (207, 67, 222) if name == "enderman" else (255, 171, 34) if name in ("magma_cube", "blaze") else (35, 22, 24)
                if name == "ghast" and 4 <= tx <= 11 and 10 <= ty <= 12:
                    c = (78, 39, 39)
            if name == "magma_cube" and ty % 5 == 0:
                c = (245, 105, 19)
            if name in ("piglin_brute", "piglin") and slot == textures.ENTITY_SKIN_LAYOUT["body_front"] and tx in (4, 5):
                c = (230, 174, 45)
            result.append(c + (255,))
    return result


def generate(output):
    output.mkdir(parents=True, exist_ok=True)
    original_parts, original_skin = models.parts_for, textures.generate_entity_skin
    try:
        for name, (base, size, color) in DEFINITIONS.items():
            native_parts = parts(name, original_parts(base))
            models.parts_for = lambda _base, value=native_parts: value
            textures.generate_entity_skin = lambda _name, _seed, n=name, c=color: skin(n, c)
            data = models.build_v2(base, size, color + (255,))
            # Quadrupeds need an attack action as well as locomotion. Reuse the
            # authored hurt squash as a short lunge without changing base assets.
            length = struct.unpack_from("<I", data, 12)[0]
            document = json.loads(data[20:20 + length])
            if not any(a["name"] == "attack" for a in document["animations"]):
                action = copy.deepcopy(next(a for a in document["animations"] if a["name"] == "hurt"))
                action["name"] = "attack"
                document["animations"].append(action)
                encoded = json.dumps(document, sort_keys=True, separators=(",", ":")).encode()
                encoded += b" " * ((-len(encoded)) % 4)
                binary = data[20 + length:]
                data = struct.pack("<4sII", b"glTF", 2, 20 + len(encoded) + len(binary)) + struct.pack("<II", len(encoded), 0x4E4F534A) + encoded + binary
            (output / (name + ".glb")).write_bytes(data)
            graph_path = output / (name + ".anim.json")
            models.write_action_graph(graph_path, base)
            graph = json.loads(graph_path.read_text())
            graph["actions"]["attack"] = {"clip": "attack", "layer": "action", "loop": False, "duration": .55, "priority": 100, "events": [{"name": "melee", "time": .30}]}
            graph["bindings"]["attack"] = "attack"
            graph_path.write_text(json.dumps(graph, indent=2) + "\n", encoding="utf-8")
    finally:
        models.parts_for, textures.generate_entity_skin = original_parts, original_skin


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    generate(parser.parse_args().output)
