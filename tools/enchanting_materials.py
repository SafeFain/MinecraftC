"""Original deterministic 16x16 equipment-progression materials (CC0)."""
COLORS = {
    "lapis_ore": (100,110,135), "deepslate_lapis_ore": (57,62,78),
    "bookshelf": (151,110,58), "iron_block": (192,201,206),
    "enchanting_table": (70,35,87), "anvil": (85,88,96),
    "chipped_anvil": (78,81,89), "damaged_anvil": (70,73,81),
    "frosted_ice": (139,187,222),
}
def pixels(context, name, seed):
    palette = context.PALETTES[name]
    if name.endswith("_ore"):
        source = [palette[role] for role in context.generate_ore("diamond_ore",seed)]
        # A toroidal phase preserves connected veins while keeping the tile cut
        # away from a high-contrast cluster edge, for every local seed.
        best, best_ratio = source, float("inf")
        for phase in range(256):
            dx, dy = phase % 16, phase // 16
            candidate = [source[((y+dy)%16)*16+(x+dx)%16] for y in range(16) for x in range(16)]
            seams = context.seam_metrics(candidate)
            ratio = max(seams["perceptual_x"]["ratio"],seams["perceptual_y"]["ratio"],context.structure_metrics(candidate)["seam_ratio"])
            if ratio < best_ratio: best, best_ratio = candidate, ratio
            if ratio <= 2.4: return candidate
        return best
    result = []
    for y in range(16):
        for x in range(16):
            noise = (seed ^ (x*73428767) ^ (y*912931)) & 0xffffffff
            noise = ((noise ^ (noise >> 13))*1274126177) & 0xffffffff
            role = 2 + (noise % 3)
            if 'lapis_ore' in name:
                if (x//3 + 2*(y//3)) % 4 == 0 and noise % 4 != 0: role = 6 if noise % 3 else 5
            elif name == 'bookshelf':
                if y in (0,1,7,8,14,15): role = 1 if y in (1,8,15) else 3
                else: role = 4 + (x//2)%3
            elif name == 'enchanting_table':
                role = 1 if x in (0,15) or y in (0,15) else 3
                if 3 <= x <= 12 and 3 <= y <= 12: role = 5 if x < 8 else 6
                if x == 7 and 3 <= y <= 12: role = 1
            elif 'anvil' in name:
                if x in (0,15) or y in (0,15): role = 1
                if name != 'anvil' and (x == 7+(y//3)%3 or (name == 'damaged_anvil' and x == 12-y//2)): role = 0
            elif name == 'iron_block':
                if x in (0,15) or y in (0,15): role = 1
                elif x == 1 or y == 1: role = 5
            elif name == 'frosted_ice':
                if (x+y)%11 == 0 or (x*3-y)%17 == 0: role = 6
            result.append(palette[role])
    return result
