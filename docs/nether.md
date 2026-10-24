# Built-in Nether

The Nether is compiled into MinecraftC and remains available with all plugins off.
Its key is `minecraftc:nether` (ID 2), saved separately under `dimensions/nether/`.
Optional Heaven retains ID 1 and has its own travel rules.

New worlds require save v22, generation v21, Nether generation v1, LAN protocol v7,
player profile v5 and survival rules 2603. Older worlds stay visible but cannot be
opened or migrated by this release. Refused loads do not rewrite their files.
Heaven generation remains v9. The application version still comes only from VERSION.

## Exploration

Five deterministic biomes provide enclosed 3D caverns: Nether Wastes, Soul Sand
Valley, Crimson Forest, Warped Forest and Basalt Deltas. Natural terrain occupies
Y=0..127, with bedrock at both ends and a lava sea at Y=31. Building extends to
Y=255. Distant terrain retains solid layers, caves, lava and structures.

Find fortresses, bridge/stable/housing/treasure bastions, fossils and ruined portals.
Ruined portals also appear in the Overworld; its other terrain and cave algorithms
are unchanged. Bastions do not generate in Basalt Deltas. Structure loot, guards,
spawners and locate queries use the same world-coordinate authority.

There is no skylight, weather, sun, moon or cloud layer. Colored fog, particles and
original procedural ambience distinguish biomes. Lava moves faster; placed water
evaporates. Soul sand slows movement, soul fire hurts, and fire resistance protects
against the corresponding heat damage.

## Travel and respawn

Ignite an obsidian rectangle using flint and steel. Either axis and omitted corners
work; exterior dimensions range from 4×5 to 23×23. Breaking the frame disables it.
Overworld/Nether X and Z scale by 8, using mathematical floor for negatives; Y does
not scale. Existing gates are preferred (16-block Nether / 128-block Overworld
search). Distance compares the nearest supported bottom-row entry cell, including
wide gates whose stored corner is outside the radius. Scaled targets are bounded
to the world border. A safe new gate or protected obsidian platform is prepared
when no existing gate is valid.

Survival waits 80 ticks by default; Creative has no default wait. Gamerules control
entry and both delays. Cooldown remains active while standing in the destination
gate. Players, mobs and dropped items can travel; dismount before entering.
Destination geometry is saved before transfer. Entity/item transfers publish checked
chunk after-images in an idempotent journal and recover before simulation resumes.

Beds explode in the Nether. Charge a respawn anchor with up to four glowstone;
interact with a charged anchor to choose it. Respawning spends one charge. A missing,
empty or unsafe anchor falls back to the Overworld spawn. An anchor used in the
Overworld explodes.

## Survival progression

Native mobs include ghasts, blazes, wither skeletons, magma cubes, piglins, brutes,
zombified piglins, hoglins, zoglins, striders and endermen, alongside skeletons.
Fireballs, deflection, wither effects, magma splitting, gaze aggression, pearls and
Overworld zombification are simulated by the host. Gold armor keeps ordinary
piglins neutral; gold ingots buy barter loot. Brutes remain hostile. Theft and
attacks provoke nearby piglins. Saddle striders and guide/boost them with warped
fungus on a stick; lava and land have different movement speeds.

Smelt ancient debris to scraps, combine scraps and gold into netherite ingots,
and obtain/copy an upgrade template. A smithing table atomically consumes template,
diamond equipment and ingot while preserving existing equipment wear. Netherite
tools/armor have their own durability and combat/mining properties; their dropped
items resist burning.

A brewing stand has three bottle slots, ingredient, blaze-powder fuel and progress.
Brew fire resistance, strength, regeneration, healing, harming and weakness, with
applicable extended/strong variants and splash forms. Effects save type, level and
time; instant healing/harming reverse on undead. Weakness and a golden apple cure
zombie villagers. No enchanting or lingering potions are included.

Names and controls integrate with all ten languages, normal container gestures and
touch controls. LAN validates distance, inventory inputs and actions on the server.
Riding, navigation, runtime IDs and time already spent inside a gate are transient;
exiting safely detaches riders rather than restoring a stale seat after reload.
