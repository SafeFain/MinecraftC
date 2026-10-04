# GameRule reference — Java 1.21.11 baseline

Syntax: `/gamerule <rule>` queries; `/gamerule <rule> <value>` sets.
Both operations require the world's cheats flag. `/help gamerule [<rule>]`
is always available. Tab completes names and boolean values, including at the
cursor inside a command. Names are case-sensitive; booleans are exactly
`true`/`false`, integers are decimal with an optional minus sign.

Accepts standard short names, `minecraft:` names and the old names below.
MinecraftC adds `DayNightDuration` / `minecraftc:day_night_duration` in seconds.
Queries through inverted old names return inverted values too.
`doFireTick false/true` sets radius 0/128; `allowFireTicksAwayFromPlayer true/false`
sets -1/128. These are compatibility conversions; the last write wins.

Each world owns its rules, shared across Overworld and Heaven. Save v15 appends
a typed rule table using canonical IDs. v2–v14 read with Java defaults; existing
v14 day/night duration is preserved. Unknown well-typed IDs round-trip. Loading
never changes terrain generation version 17. Query/help/error/support messages
remain visible with command feedback disabled. General feedback/status strings
are translated in all ten languages; detailed explanations are Chinese/English
with an explicit localized “English explanation” label in other languages.

## Baseline provenance

Official [Java 1.21.11 release notes](https://www.minecraft.net/en-us/article/minecraft-java-edition-1-21-11).
The independent factual fixture `tests/data/java-1.21.11-gamerules.tsv` was extracted
from the official server `GameRules` static registration, using official mappings
and `javap -c -p`. No Mojang executable code is vendored.

- Server: https://piston-data.mojang.com/v1/objects/64bb6d763bed0a9f1d632ec347938594144943ed/server.jar
- Official mappings: https://piston-data.mojang.com/v1/objects/5621e9253f05fd57872bbe7f8ddf5f9a7d525955/server.txt
- Both downloaded artifacts were SHA-1 verified against official release metadata.
- 59 Java registrations, including experimental Minecart Improvements `max_minecart_speed`.
- `max_command_forks` minimum **0** follows the released binary; release notes say 1.

## Rules

Implemented means the rule controls its corresponding existing MinecraftC mechanism.
Partial means the description states concrete limitations. Unavailable rules can
be queried, set and persisted, and emit an explicit missing-mechanic message.
This adds no Nether, multiplayer, redstone, command blocks or advancements.

| Rule (prefix `minecraft:` optional) | Old name | Default | Range | Support |
|---|---|---|---|---|
| `advance_time` | `doDaylightCycle` | true | true/false | Implemented |
| `advance_weather` | `doWeatherCycle` | true | true/false | Implemented |
| `allow_entering_nether_using_portals` | `allowEnteringNetherUsingPortals` | true | true/false | Unavailable |
| `block_drops` | `doTileDrops` | true | true/false | Implemented |
| `block_explosion_drop_decay` | `blockExplosionDropDecay` | true | true/false | Unavailable |
| `command_blocks_work` | `commandBlocksEnabled` | true | true/false | Unavailable |
| `command_block_output` | `commandBlockOutput` | true | true/false | Unavailable |
| `drowning_damage` | `drowningDamage` | true | true/false | Implemented |
| `elytra_movement_check` | `disableElytraMovementCheck` | true | true/false | Unavailable |
| `ender_pearls_vanish_on_death` | `enderPearlsVanishOnDeath` | true | true/false | Unavailable |
| `entity_drops` | `doEntityDrops` | true | true/false | Unavailable |
| `fall_damage` | `fallDamage` | true | true/false | Implemented |
| `fire_damage` | `fireDamage` | true | true/false | Implemented |
| `fire_spread_radius_around_player` | `fireSpreadRadiusAroundPlayer` | 128 | -1..2147483647 | Partial |
| `forgive_dead_players` | `forgiveDeadPlayers` | true | true/false | Unavailable |
| `freeze_damage` | `freezeDamage` | true | true/false | Unavailable |
| `global_sound_events` | `globalSoundEvents` | true | true/false | Unavailable |
| `immediate_respawn` | `doImmediateRespawn` | false | true/false | Implemented |
| `keep_inventory` | `keepInventory` | false | true/false | Implemented |
| `lava_source_conversion` | `lavaSourceConversion` | false | true/false | Implemented |
| `limited_crafting` | `doLimitedCrafting` | false | true/false | Unavailable |
| `locator_bar` | `locatorBar` | true | true/false | Unavailable |
| `log_admin_commands` | `logAdminCommands` | true | true/false | Partial |
| `max_block_modifications` | `commandModificationBlockLimit` | 32768 | 1..2147483647 | Unavailable |
| `max_command_forks` | `maxCommandForkCount` | 65536 | 0..2147483647 | Unavailable |
| `max_command_sequence_length` | `maxCommandChainLength` | 65536 | 0..2147483647 | Unavailable |
| `max_entity_cramming` | `maxEntityCramming` | 24 | 0..2147483647 | Unavailable |
| `max_minecart_speed` | `minecartMaxSpeed` | 8 | 1..1000 | Unavailable |
| `max_snow_accumulation_height` | `snowAccumulationHeight` | 1 | 0..8 | Partial |
| `mob_drops` | `doMobLoot` | true | true/false | Implemented |
| `mob_explosion_drop_decay` | `mobExplosionDropDecay` | true | true/false | Partial |
| `mob_griefing` | `mobGriefing` | true | true/false | Partial |
| `natural_health_regeneration` | `naturalRegeneration` | true | true/false | Implemented |
| `player_movement_check` | `disablePlayerMovementCheck` | true | true/false | Unavailable |
| `players_nether_portal_creative_delay` | `playersNetherPortalCreativeDelay` | 0 | 0..2147483647 | Unavailable |
| `players_nether_portal_default_delay` | `playersNetherPortalDefaultDelay` | 80 | 0..2147483647 | Unavailable |
| `players_sleeping_percentage` | `playersSleepingPercentage` | 100 | 0..2147483647 | Implemented |
| `projectiles_can_break_blocks` | `projectilesCanBreakBlocks` | true | true/false | Unavailable |
| `pvp` | `pvp` | true | true/false | Unavailable |
| `raids` | `disableRaids` | true | true/false | Unavailable |
| `random_tick_speed` | `randomTickSpeed` | 3 | 0..2147483647 | Partial |
| `reduced_debug_info` | `reducedDebugInfo` | false | true/false | Implemented |
| `respawn_radius` | `spawnRadius` | 10 | 0..2147483647 | Partial |
| `send_command_feedback` | `sendCommandFeedback` | true | true/false | Implemented |
| `show_advancement_messages` | `announceAdvancements` | true | true/false | Unavailable |
| `show_death_messages` | `showDeathMessages` | true | true/false | Implemented |
| `spawner_blocks_work` | `spawnerBlocksEnabled` | true | true/false | Unavailable |
| `spawn_mobs` | `doMobSpawning` | true | true/false | Partial |
| `spawn_monsters` | `spawnMonsters` | true | true/false | Implemented |
| `spawn_patrols` | `doPatrolSpawning` | true | true/false | Unavailable |
| `spawn_phantoms` | `doInsomnia` | true | true/false | Unavailable |
| `spawn_wandering_traders` | `doTraderSpawning` | true | true/false | Unavailable |
| `spawn_wardens` | `doWardenSpawning` | true | true/false | Unavailable |
| `spectators_generate_chunks` | `spectatorsGenerateChunks` | true | true/false | Partial |
| `spread_vines` | `doVinesSpread` | true | true/false | Unavailable |
| `tnt_explodes` | `tntExplodes` | true | true/false | Implemented |
| `tnt_explosion_drop_decay` | `tntExplosionDropDecay` | false | true/false | Partial |
| `universal_anger` | `universalAnger` | false | true/false | Unavailable |
| `water_source_conversion` | `waterSourceConversion` | true | true/false | Implemented |
| `DayNightDuration` | `` | 1200 | 1..4294967295 | Implemented |

## Behavior and limitations

- **`advance_time`:** Advance the day/night phase naturally; explicit time commands remain available.
- **`advance_weather`:** Advance natural weather timers and let sleep clear weather; explicit weather commands remain available.
- **`allow_entering_nether_using_portals`:** Allow Nether portal entry. MinecraftC has no Nether portal system.
- **`block_drops`:** Allow mined blocks, container contents and explosion-destroyed blocks to drop items.
- **`block_explosion_drop_decay`:** Reduce drops from block-origin explosions other than TNT. No such explosions currently exist.
- **`command_blocks_work`:** Enable command blocks; command blocks are not implemented.
- **`command_block_output`:** Send command block execution feedback; command blocks are not implemented.
- **`drowning_damage`:** Allow drowning damage to the player; breathing and other damage continue independently.
- **`elytra_movement_check`:** Enable server Elytra movement validation; no Elytra or multiplayer validation exists. Legacy disableElytraMovementCheck is inverted.
- **`ender_pearls_vanish_on_death`:** Remove thrown Ender pearls when their owner dies; Ender pearls are not implemented.
- **`entity_drops`:** Allow non-mob entities such as boats and minecarts to drop items when destroyed; these entities are absent.
- **`fall_damage`:** Allow player fall damage.
- **`fire_damage`:** Allow player fire and lava damage. Does not prevent ignition or damage to mobs.
- **`fire_spread_radius_around_player`:** Fire aging/spreading radius: 0 stops fire ticks, -1 removes the distance limit. Only loaded edited fire is modeled; up to 256 fires tick per pass. doFireTick false/true sets 0/128; allowFireTicksAwayFromPlayer true/false sets -1/128. Last write wins.
- **`forgive_dead_players`:** Neutral mobs forgive dead players; neutral-mob anger is not implemented.
- **`freeze_damage`:** Allow powder-snow freezing damage; freezing is not implemented.
- **`global_sound_events`:** Broadcast distant global events; no global sound event system exists.
- **`immediate_respawn`:** Respawn automatically through the normal respawn flow without the death screen.
- **`keep_inventory`:** Keep all inventory slots, armor and offhand on death; otherwise drop them normally. No XP system exists.
- **`lava_source_conversion`:** Allow supported flowing lava between two source neighbors to become a source.
- **`limited_crafting`:** Restrict crafting to unlocked recipes; recipe unlocking is not implemented.
- **`locator_bar`:** Show multiplayer locator bar; multiplayer tracking is not implemented.
- **`log_admin_commands`:** Log successful local mutating command types through the logger; no server administration broadcast exists.
- **`max_block_modifications`:** Limit blocks modified by fill/clone; these commands are absent.
- **`max_command_forks`:** Limit execute command forks. Official 1.21.11 binary accepts 0; execute is absent.
- **`max_command_sequence_length`:** Limit command-chain length; command chains are absent.
- **`max_entity_cramming`:** Limit crowded entities before suffocation; entity cramming is not implemented.
- **`max_minecart_speed`:** Experimental Minecart Improvements rule, included in the baseline registry; minecarts are absent.
- **`max_snow_accumulation_height`:** 0 disables new snow accumulation. Values 1–8 permit one existing snow layer; stacked snow layers are absent.
- **`mob_drops`:** Allow mob death loot. Player inventory loss and existing dropped items are independent.
- **`mob_explosion_drop_decay`:** Reduce Blastling explosion block drops with probability 1/explosion power.
- **`mob_griefing`:** Allow Blastling explosions to destroy blocks; explosion damage still applies. Other Java mob block interactions are absent.
- **`natural_health_regeneration`:** Allow food-based natural healing. Hunger depletion and starvation continue independently.
- **`player_movement_check`:** Enable multiplayer movement validation; no multiplayer validation exists. Legacy disablePlayerMovementCheck is inverted.
- **`players_nether_portal_creative_delay`:** Creative Nether portal delay in ticks; Nether portals are absent.
- **`players_nether_portal_default_delay`:** Default Nether portal delay in ticks; Nether portals are absent.
- **`players_sleeping_percentage`:** Single-player sleep skips the night when the percentage is at most 100. Higher values prevent the skip. Heaven travel is independent.
- **`projectiles_can_break_blocks`:** Allow projectile destruction of supported fragile blocks; such projectile interactions are absent.
- **`pvp`:** Allow player-versus-player combat; multiplayer is absent.
- **`raids`:** Enable raids; raids are absent. Legacy disableRaids is inverted.
- **`random_tick_speed`:** Random draws per 16-cubed section each 20 Hz tick, default 3. Only edited farmland, wheat and saplings are modeled within 128 horizontal blocks. 0 stops these updates; fluids/furnaces continue. At most 16384 draws per tick; excess is capped with rotating section priority.
- **`reduced_debug_info`:** Hide coordinates, chunk counts and GI statistics from the gameplay window title; FPS remains visible.
- **`respawn_radius`:** Search up to 16 safe loaded columns in the horizontal square around world spawn; otherwise use world spawn. Valid beds override the radius.
- **`send_command_feedback`:** Show successful command feedback. Queries/help/errors and support warnings remain visible; toggling this rule acknowledges once.
- **`show_advancement_messages`:** Announce advancements; advancements are absent.
- **`show_death_messages`:** Show the local player death message without changing the death or respawn flow.
- **`spawner_blocks_work`:** Enable spawner blocks; spawners are absent.
- **`spawn_mobs`:** Allow natural mob spawning and first-load village population in the Overworld. Existing entities and spawn eggs are independent. Re-enabling retries pending population without duplicating loaded entities.
- **`spawn_monsters`:** Allow natural hostile spawning. Does not delete existing hostiles or block spawn eggs.
- **`spawn_patrols`:** Enable patrol spawning; patrols are absent.
- **`spawn_phantoms`:** Enable phantom spawning; phantoms are absent.
- **`spawn_wandering_traders`:** Enable wandering trader spawning; wandering traders are absent.
- **`spawn_wardens`:** Enable Warden spawning; Wardens are absent.
- **`spectators_generate_chunks`:** Allow ordinary spectator movement to advance terrain streaming. Loading and explicit teleport commands still run their loading pipeline; terrain LOD remains active.
- **`spread_vines`:** Allow vine growth; vine random growth is absent.
- **`tnt_explodes`:** Allow primed TNT to explode at the end of its fuse; disabled TNT disappears without an explosion.
- **`tnt_explosion_drop_decay`:** Reduce TNT block drops with probability 1/explosion power when true, default false. Explosion drops remain capped at 32 stacks.
- **`universal_anger`:** Share neutral-mob anger with all players; neutral anger is absent.
- **`water_source_conversion`:** Allow supported flowing water between two source neighbors to become a source.
- **`DayNightDuration`:** MinecraftC extension: day/night duration in seconds, 1–4294967295. Also accepts minecraftc:day_night_duration.
