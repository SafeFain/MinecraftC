"""Original deterministic native Nether material palettes and pixel structures."""
NETHER_MATERIALS = {'netherrack': {'color': (109, 43, 40), 'plant': False, 'emission': 0}, 'soul_sand': {'color': (81, 61, 48), 'plant': False, 'emission': 0}, 'soul_soil': {'color': (66, 51, 40), 'plant': False, 'emission': 0}, 'blackstone': {'color': (35, 33, 43), 'plant': False, 'emission': 0}, 'crimson_nylium': {'color': (137, 33, 56), 'plant': False, 'emission': 0}, 'warped_nylium': {'color': (30, 119, 109), 'plant': False, 'emission': 0}, 'crimson_stem': {'color': (91, 30, 51), 'plant': False, 'emission': 0}, 'warped_stem': {'color': (30, 86, 89), 'plant': False, 'emission': 0}, 'crimson_planks': {'color': (114, 48, 73), 'plant': False, 'emission': 0}, 'warped_planks': {'color': (40, 107, 102), 'plant': False, 'emission': 0}, 'nether_wart_block': {'color': (142, 15, 30), 'plant': False, 'emission': 0}, 'warped_wart_block': {'color': (25, 99, 94), 'plant': False, 'emission': 0}, 'shroomlight': {'color': (255, 140, 56), 'plant': False, 'emission': 15}, 'glowstone': {'color': (224, 175, 84), 'plant': False, 'emission': 15}, 'nether_quartz_ore': {'color': (165, 102, 91), 'plant': False, 'emission': 0}, 'nether_gold_ore': {'color': (163, 96, 43), 'plant': False, 'emission': 0}, 'ancient_debris': {'color': (91, 63, 53), 'plant': False, 'emission': 0}, 'nether_bricks': {'color': (56, 25, 38), 'plant': False, 'emission': 0}, 'red_nether_bricks': {'color': (107, 20, 25), 'plant': False, 'emission': 0}, 'polished_blackstone': {'color': (51, 48, 58), 'plant': False, 'emission': 0}, 'blackstone_bricks': {'color': (45, 43, 53), 'plant': False, 'emission': 0}, 'crying_obsidian': {'color': (73, 25, 117), 'plant': False, 'emission': 10}, 'magma_block': {'color': (147, 48, 12), 'plant': False, 'emission': 3}, 'crimson_fungus': {'color': (198, 40, 35), 'plant': True, 'emission': 0}, 'warped_fungus': {'color': (30, 183, 155), 'plant': True, 'emission': 0}, 'crimson_roots': {'color': (153, 20, 48), 'plant': True, 'emission': 0}, 'warped_roots': {'color': (25, 140, 127), 'plant': True, 'emission': 0}, 'nether_sprouts': {'color': (30, 158, 145), 'plant': True, 'emission': 0}, 'weeping_vines': {'color': (158, 17, 30), 'plant': True, 'emission': 0}, 'twisting_vines': {'color': (30, 132, 117), 'plant': True, 'emission': 0}, 'nether_wart': {'color': (168, 25, 38), 'plant': True, 'emission': 0}, 'soul_fire': {'color': (25, 204, 234), 'plant': True, 'emission': 10}, 'nether_portal_x': {'color': (124, 30, 211), 'plant': False, 'emission': 11}, 'nether_portal_z': {'color': (124, 30, 211), 'plant': False, 'emission': 11}, 'respawn_anchor': {'color': (61, 25, 81), 'plant': False, 'emission': 0}, 'blaze_spawner': {'color': (56, 43, 33), 'plant': False, 'emission': 0}, 'magma_spawner': {'color': (61, 25, 17), 'plant': False, 'emission': 0}, 'bone_block': {'color': (211, 206, 175), 'plant': False, 'emission': 0}}

NETHER_MATERIALS.update({
    'crimson_door_bottom':{'color':(114,48,73),'plant':True,'emission':0},
    'crimson_door_top':{'color':(114,48,73),'plant':True,'emission':0},
    'warped_door_bottom':{'color':(40,107,102),'plant':True,'emission':0},
    'warped_door_top':{'color':(40,107,102),'plant':True,'emission':0},
})

def pixels(tg,name,seed):
    spec=NETHER_MATERIALS[name]
    reference='stone'
    if 'door' in name:reference='oak_door_top' if name.endswith('_top') else 'oak_door_bottom'
    elif 'ore' in name:reference='gold_ore' if 'gold' in name else 'iron_ore'
    elif 'brick' in name:reference='stone_bricks'
    elif 'planks' in name:reference='oak_planks'
    elif 'stem' in name:reference='oak_log'
    elif 'obsidian' in name or 'portal' in name:reference='obsidian'
    elif spec['plant']:reference='brown_mushroom' if 'fungus' in name else 'fern'
    original=tg.PALETTES[reference]
    palette=tg.PALETTES[name]
    tg.PALETTES[reference]=palette+[palette[-1]]*max(0,len(original)-len(palette))
    try:return tg.generate_texture(reference,seed,{reference:seed})
    finally:tg.PALETTES[reference]=original
