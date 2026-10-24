#pragma once

// Serialized native content. Append entries; never reorder this list.
// ID, material key, display name, linear-independent UI color, shape, emission.
#define MC_NETHER_BLOCKS(X) \
 X(NETHERRACK,"netherrack","Netherrack",.43f,.17f,.16f,Cube,0) \
 X(SOUL_SAND,"soul_sand","Soul Sand",.32f,.24f,.19f,Cube,0) \
 X(SOUL_SOIL,"soul_soil","Soul Soil",.26f,.20f,.16f,Cube,0) \
 X(BLACKSTONE,"blackstone","Blackstone",.14f,.13f,.17f,Cube,0) \
 X(CRIMSON_NYLIUM,"crimson_nylium","Crimson Nylium",.54f,.13f,.22f,Cube,0) \
 X(WARPED_NYLIUM,"warped_nylium","Warped Nylium",.12f,.47f,.43f,Cube,0) \
 X(CRIMSON_STEM,"crimson_stem","Crimson Stem",.36f,.12f,.20f,Cube,0) \
 X(WARPED_STEM,"warped_stem","Warped Stem",.12f,.34f,.35f,Cube,0) \
 X(CRIMSON_PLANKS,"crimson_planks","Crimson Planks",.45f,.19f,.29f,Cube,0) \
 X(WARPED_PLANKS,"warped_planks","Warped Planks",.16f,.42f,.40f,Cube,0) \
 X(NETHER_WART_BLOCK,"nether_wart_block","Nether Wart Block",.56f,.06f,.12f,Cube,0) \
 X(WARPED_WART_BLOCK,"warped_wart_block","Warped Wart Block",.10f,.39f,.37f,Cube,0) \
 X(SHROOMLIGHT,"shroomlight","Shroomlight",1.f,.55f,.22f,Cube,15) \
 X(GLOWSTONE,"glowstone","Glowstone",.88f,.69f,.33f,Cube,15) \
 X(NETHER_QUARTZ_ORE,"nether_quartz_ore","Nether Quartz Ore",.65f,.40f,.36f,Cube,0) \
 X(NETHER_GOLD_ORE,"nether_gold_ore","Nether Gold Ore",.64f,.38f,.17f,Cube,0) \
 X(ANCIENT_DEBRIS,"ancient_debris","Ancient Debris",.36f,.25f,.21f,Cube,0) \
 X(NETHER_BRICKS,"nether_bricks","Nether Bricks",.22f,.10f,.15f,Cube,0) \
 X(RED_NETHER_BRICKS,"red_nether_bricks","Red Nether Bricks",.42f,.08f,.10f,Cube,0) \
 X(POLISHED_BLACKSTONE,"polished_blackstone","Polished Blackstone",.20f,.19f,.23f,Cube,0) \
 X(BLACKSTONE_BRICKS,"blackstone_bricks","Polished Blackstone Bricks",.18f,.17f,.21f,Cube,0) \
 X(CRYING_OBSIDIAN,"crying_obsidian","Crying Obsidian",.29f,.10f,.46f,Cube,10) \
 X(MAGMA_BLOCK,"magma_block","Magma Block",.58f,.19f,.05f,Cube,3) \
 X(CRIMSON_FUNGUS,"crimson_fungus","Crimson Fungus",.78f,.16f,.14f,Cross,0) \
 X(WARPED_FUNGUS,"warped_fungus","Warped Fungus",.12f,.72f,.61f,Cross,0) \
 X(CRIMSON_ROOTS,"crimson_roots","Crimson Roots",.60f,.08f,.19f,Cross,0) \
 X(WARPED_ROOTS,"warped_roots","Warped Roots",.10f,.55f,.50f,Cross,0) \
 X(NETHER_SPROUTS,"nether_sprouts","Nether Sprouts",.12f,.62f,.57f,Cross,0) \
 X(WEEPING_VINES,"weeping_vines","Weeping Vines",.62f,.07f,.12f,Cross,0) \
 X(TWISTING_VINES,"twisting_vines","Twisting Vines",.12f,.52f,.46f,Cross,0) \
 X(NETHER_WART,"nether_wart","Nether Wart",.66f,.10f,.15f,Cross,0) \
 X(SOUL_FIRE,"soul_fire","Soul Fire",.10f,.80f,.92f,Cross,10) \
 X(NETHER_PORTAL_X,"nether_portal_x","Nether Portal",.49f,.12f,.83f,Cube,11) \
 X(NETHER_PORTAL_Z,"nether_portal_z","Nether Portal",.49f,.12f,.83f,Cube,11) \
 X(RESPAWN_ANCHOR,"respawn_anchor","Respawn Anchor",.24f,.10f,.32f,Cube,0) \
 X(BLAZE_SPAWNER,"blaze_spawner","Blaze Spawner",.22f,.17f,.13f,Cube,0) \
 X(MAGMA_SPAWNER,"magma_spawner","Magma Cube Spawner",.24f,.10f,.07f,Cube,0) \
 X(BONE_BLOCK,"bone_block","Bone Block",.83f,.81f,.69f,Cube,0)
