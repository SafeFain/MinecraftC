#pragma once

// Append-only native resource and potion identities, independent of plugins.
#define MC_NETHER_RESOURCES(X) \
 X(QUARTZ,"quartz","Nether Quartz") \
 X(GOLD_NUGGET,"gold_nugget","Gold Nugget") \
 X(NETHER_BRICK,"nether_brick","Nether Brick") \
 X(NETHERITE_SCRAP,"netherite_scrap","Netherite Scrap") \
 X(NETHERITE_INGOT,"netherite_ingot","Netherite Ingot") \
 X(NETHERITE_UPGRADE_TEMPLATE,"netherite_upgrade_template","Netherite Upgrade Smithing Template") \
 X(GLOWSTONE_DUST,"glowstone_dust","Glowstone Dust") \
 X(BLAZE_ROD,"blaze_rod","Blaze Rod") \
 X(BLAZE_POWDER,"blaze_powder","Blaze Powder") \
 X(GHAST_TEAR,"ghast_tear","Ghast Tear") \
 X(MAGMA_CREAM,"magma_cream","Magma Cream") \
 X(ENDER_PEARL,"ender_pearl","Ender Pearl") \
 X(WITHER_SKELETON_SKULL,"wither_skeleton_skull","Wither Skeleton Skull") \
 X(SADDLE,"saddle","Saddle") \
 X(WARPED_FUNGUS_ON_A_STICK,"warped_fungus_on_a_stick","Warped Fungus on a Stick") \
 X(GLASS_BOTTLE,"glass_bottle","Glass Bottle") \
 X(WATER_BOTTLE,"water_bottle","Water Bottle") \
 X(AWKWARD_POTION,"awkward_potion","Awkward Potion") \
 X(SUGAR,"sugar","Sugar") \
 X(SPIDER_EYE,"spider_eye","Spider Eye") \
 X(FERMENTED_SPIDER_EYE,"fermented_spider_eye","Fermented Spider Eye") \
 X(GLISTERING_MELON,"glistering_melon","Glistering Melon Slice") \
 X(REDSTONE_DUST,"redstone_dust","Redstone Dust") \
 X(APPLE,"apple","Apple") \
 X(GOLDEN_APPLE,"golden_apple","Golden Apple")

#define MC_NETHER_EQUIPMENT(X) \
 X(NETHERITE_PICKAXE,"netherite_pickaxe","Netherite Pickaxe",DIAMOND_PICKAXE,2031) \
 X(NETHERITE_AXE,"netherite_axe","Netherite Axe",DIAMOND_AXE,2031) \
 X(NETHERITE_SHOVEL,"netherite_shovel","Netherite Shovel",DIAMOND_SHOVEL,2031) \
 X(NETHERITE_HOE,"netherite_hoe","Netherite Hoe",DIAMOND_HOE,2031) \
 X(NETHERITE_SWORD,"netherite_sword","Netherite Sword",DIAMOND_SWORD,2031) \
 X(NETHERITE_HELMET,"netherite_helmet","Netherite Helmet",DIAMOND_HELMET,407) \
 X(NETHERITE_CHESTPLATE,"netherite_chestplate","Netherite Chestplate",DIAMOND_CHESTPLATE,592) \
 X(NETHERITE_LEGGINGS,"netherite_leggings","Netherite Leggings",DIAMOND_LEGGINGS,555) \
 X(NETHERITE_BOOTS,"netherite_boots","Netherite Boots",DIAMOND_BOOTS,481)

// effect amplifier is zero-based; durations are game ticks. Instant effects use 1.
#define MC_NETHER_POTIONS(X) \
 X(FIRE_RESISTANCE_POTION,"fire_resistance_potion","Potion of Fire Resistance",FireResistance,0,3600,0) \
 X(LONG_FIRE_RESISTANCE_POTION,"long_fire_resistance_potion","Long Potion of Fire Resistance",FireResistance,0,9600,0) \
 X(STRENGTH_POTION,"strength_potion","Potion of Strength",Strength,0,3600,0) \
 X(LONG_STRENGTH_POTION,"long_strength_potion","Long Potion of Strength",Strength,0,9600,0) \
 X(STRONG_STRENGTH_POTION,"strong_strength_potion","Strong Potion of Strength",Strength,1,1800,0) \
 X(REGENERATION_POTION,"regeneration_potion","Potion of Regeneration",Regeneration,0,900,0) \
 X(LONG_REGENERATION_POTION,"long_regeneration_potion","Long Potion of Regeneration",Regeneration,0,1800,0) \
 X(STRONG_REGENERATION_POTION,"strong_regeneration_potion","Strong Potion of Regeneration",Regeneration,1,450,0) \
 X(HEALING_POTION,"healing_potion","Potion of Healing",Healing,0,1,0) \
 X(STRONG_HEALING_POTION,"strong_healing_potion","Strong Potion of Healing",Healing,1,1,0) \
 X(HARMING_POTION,"harming_potion","Potion of Harming",Harming,0,1,0) \
 X(STRONG_HARMING_POTION,"strong_harming_potion","Strong Potion of Harming",Harming,1,1,0) \
 X(WEAKNESS_POTION,"weakness_potion","Potion of Weakness",Weakness,0,1800,0) \
 X(LONG_WEAKNESS_POTION,"long_weakness_potion","Long Potion of Weakness",Weakness,0,4800,0) \
 X(SPLASH_FIRE_RESISTANCE_POTION,"splash_fire_resistance_potion","Splash Potion of Fire Resistance",FireResistance,0,3600,1) \
 X(SPLASH_LONG_FIRE_RESISTANCE_POTION,"splash_long_fire_resistance_potion","Long Splash Potion of Fire Resistance",FireResistance,0,9600,1) \
 X(SPLASH_STRENGTH_POTION,"splash_strength_potion","Splash Potion of Strength",Strength,0,3600,1) \
 X(SPLASH_LONG_STRENGTH_POTION,"splash_long_strength_potion","Long Splash Potion of Strength",Strength,0,9600,1) \
 X(SPLASH_STRONG_STRENGTH_POTION,"splash_strong_strength_potion","Strong Splash Potion of Strength",Strength,1,1800,1) \
 X(SPLASH_REGENERATION_POTION,"splash_regeneration_potion","Splash Potion of Regeneration",Regeneration,0,900,1) \
 X(SPLASH_LONG_REGENERATION_POTION,"splash_long_regeneration_potion","Long Splash Potion of Regeneration",Regeneration,0,1800,1) \
 X(SPLASH_STRONG_REGENERATION_POTION,"splash_strong_regeneration_potion","Strong Splash Potion of Regeneration",Regeneration,1,450,1) \
 X(SPLASH_HEALING_POTION,"splash_healing_potion","Splash Potion of Healing",Healing,0,1,1) \
 X(SPLASH_STRONG_HEALING_POTION,"splash_strong_healing_potion","Strong Splash Potion of Healing",Healing,1,1,1) \
 X(SPLASH_HARMING_POTION,"splash_harming_potion","Splash Potion of Harming",Harming,0,1,1) \
 X(SPLASH_STRONG_HARMING_POTION,"splash_strong_harming_potion","Strong Splash Potion of Harming",Harming,1,1,1) \
 X(SPLASH_WEAKNESS_POTION,"splash_weakness_potion","Splash Potion of Weakness",Weakness,0,1800,1) \
 X(SPLASH_LONG_WEAKNESS_POTION,"splash_long_weakness_potion","Long Splash Potion of Weakness",Weakness,0,4800,1)

#define MC_NETHER_FAMILY_ITEMS(X) \
 X(CRIMSON_DOOR,"crimson_door","Crimson Door",Door,Crimson) \
 X(WARPED_DOOR,"warped_door","Warped Door",Door,Warped) \
 X(CRIMSON_BUTTON,"crimson_button","Crimson Button",Button,Crimson) \
 X(WARPED_BUTTON,"warped_button","Warped Button",Button,Warped) \
 X(CRIMSON_SLAB,"crimson_slab","Crimson Slab",Slab,Crimson) \
 X(CRIMSON_STAIRS,"crimson_stairs","Crimson Stairs",Stair,Crimson) \
 X(WARPED_SLAB,"warped_slab","Warped Slab",Slab,Warped) \
 X(WARPED_STAIRS,"warped_stairs","Warped Stairs",Stair,Warped) \
 X(NETHER_BRICK_SLAB,"nether_brick_slab","Nether Brick Slab",Slab,NetherBricks) \
 X(NETHER_BRICK_STAIRS,"nether_brick_stairs","Nether Brick Stairs",Stair,NetherBricks) \
 X(BLACKSTONE_SLAB,"blackstone_slab","Polished Blackstone Brick Slab",Slab,Blackstone) \
 X(BLACKSTONE_STAIRS,"blackstone_stairs","Polished Blackstone Brick Stairs",Stair,Blackstone)
