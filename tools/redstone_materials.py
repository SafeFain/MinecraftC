"""Original CC0 circuit materials, authored in semantic palette roles."""
COLORS = {
    'redstone_dust': (102, 25, 20), 'redstone_dust_on': (238, 51, 30),
    'lever': (125, 108, 80), 'redstone_torch': (224, 57, 34),
    'redstone_torch_off': (105, 46, 33),
    'redstone_repeater': (140, 132, 124), 'redstone_repeater_on': (195, 82, 65),
    'redstone_comparator': (140, 136, 128), 'redstone_comparator_on': (195, 90, 68),
    'piston': (129, 124, 109), 'piston_side': (128, 123, 107),
    'piston_top': (174, 139, 83), 'sticky_piston_top': (108, 155, 73),
    'observer': (112, 116, 119), 'observer_face': (139, 142, 140),
    'observer_back': (113, 86, 79), 'observer_back_on': (202, 57, 39),
    'redstone_lamp': (112, 81, 49), 'redstone_lamp_on': (221, 179, 94),
    'redstone_block': (183, 37, 26), 'redstone_ore': (117, 113, 106),
    'deepslate_redstone_ore': (63, 67, 72),
}

COLORS.update({f"redstone_dust_{power}": (102+9*power,25+2*power,20+power) for power in range(1,15)})

def pixels(context, name, seed):
    palette = context.PALETTES[name]
    if name.endswith('_ore'):
        from enchanting_materials import pixels as mineral
        return mineral(context, name, seed)
    result = []
    for y in range(16):
        for x in range(16):
            role = 2 + ((x * 3 + y * 5 + seed) % 3)
            if 'dust' in name:
                role = 0 if x not in (6,7,8,9) and y not in (6,7,8,9) else 3 + (x+y)%3
            elif name == 'observer_face':
                if y in (5,6) and x in (4,5,10,11): role = 0
                if y == 10 and 5 <= x <= 10: role = 0
            elif 'observer_back' in name:
                if 5 <= x <= 10 and 5 <= y <= 10: role = 5 if name.endswith('_on') else 0
            elif 'piston' in name:
                if x%8 in (0,7): role = 1
                if y%8 in (1,6): role = 5
            elif 'lamp' in name:
                if x%8 in (0,7) or y%8 in (0,7): role = 1
            elif 'repeater' in name or 'comparator' in name:
                if x in (7,8): role = 0 if not name.endswith('_on') else 6
                if y in (4,11) and 5 <= x <= 10: role = 5
            elif 'torch' in name:
                role = 1 if x%8 in (0,7) else 5 if y%8 in (2,3,4) else 3
            elif name == 'lever':
                if x in (7,8): role = 5
            result.append(palette[role])
    return result
