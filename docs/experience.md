# Experience

MinecraftC follows Java 1.21.11's experience foundation. Levels do not increase
damage or health. Enchanting, anvils, Mending, experience bottles and new breeding
gameplay are outside this implementation.

## Levels and orbs

The Survival HUD displays a green progress bar and the level above the hotbar.
The next level costs `2L+7` points at levels 0–15, `5L−38` at 16–30, and `9L−158`
at 31 and above. Level 16 requires 352 points; level 30 requires 1395.
Creative players can collect and retain experience with the HUD hidden;
Spectators and dead players cannot attract or collect orbs.

Orbs fall, bounce, follow nearby eligible players within eight blocks, respond
to flowing water and disappear after five loaded minutes. Fire, lava and
explosions can destroy them. Equal-value orbs merge with a remaining pickup
count, preserving both total experience and the ten-pickups-per-second rate.
Their value, count, position, motion, health and age persist with chunk entities.
Only loaded simulation time advances age. Any eligible player may collect an orb.

## Sources and death

| Source | Experience points |
|---|---|
| Coal ore | 0–2, with a suitable pickaxe |
| Diamond ore | 3–7, with a suitable pickaxe |
| Nether quartz ore | 2–5, with a suitable pickaxe |
| Nether gold ore | 0–1, with a suitable pickaxe |
| Native spawners | 15–43 |
| Existing passive animals / Striders | 1–3 |
| Most existing hostile mobs / Blastlings | 5 |
| Blazes / Piglin Brutes | 10 / 20 |
| Magma Cubes | 1, 2 or 4 according to size, independently for each killed cube |
| Villagers / Iron Golems | 0 |
| Successful fishing catch | 1–6 |
| Successful villager trade | 3–6; 8–11 when it raises the villager's level |

Mob experience requires a registered player hit during the preceding five
seconds; angered Zombified Piglins retain their native exception. Environmental
deaths without player credit and natural despawns give none. `mob_drops` controls
mob rewards; `block_drops` controls mined-block rewards. Plugin-only mobs and ores
default to zero experience. No terrain generation or random seed layout changes.

Furnaces bank experience by completed recipe. Taking any result by an accepted
inventory operation releases the bank once; breaking or exploding the furnace
also releases it. Failed, full-inventory and replayed operations cannot claim it.
Whole points are awarded and each recipe's fractional remainder is rounded
probabilistically. Existing recipes use Java values, including 0.1 for stone and
glass, 0.35 for cooked food, 0.7 for iron, 1 for gold/cactus, and 2 for ancient
debris. Plugin recipes currently default to zero without changing ABI 1 layouts.

On death, `keep_inventory=true` retains all experience. Otherwise the player's
experience resets and `min(7 × level, 100)` points drop in the death dimension.
Experience belongs to the player across dimension travel and game-mode changes.

## Commands and compatibility

```
/experience add <targets> <amount> [points|levels]
/experience set <targets> <amount> [points|levels]
/experience query <player> points|levels
```

`/xp` is an alias. Targets support `@s`, `@a` and exact online nicknames; queries
require one player. Points are the default unit. Adding accepts signed 32-bit
amounts; setting requires a nonnegative integer. Setting points cannot reach the
next level. Changing levels preserves the bar's fraction; subtraction clamps at
zero. Arithmetic saturates at the supported Java-scale level limit 238609312.
Commands require cheats and remain host-only in LAN games.

New writes use world v23, player-profile v6 and LAN v8. World v22 and profiles v5
remain readable, with player experience and furnace banks initialized to zero.
Loading does not rewrite them. Normal saving upgrades individual partitions;
mixed v22/v23 partitions remain readable, including plugin recovery and entity
transfer snapshots. The local `lan-identity` credential remains unchanged.
V2–v21 worlds remain inspection-only; old programs refuse new-format files.

The host owns rewards and pickup. Guest snapshots carry their own experience and
sound sequences; clients cannot request an experience grant or claim the same
orb twice. Guest experience persists on save, disconnect and reconnection.

The orb sprite and synthesized sounds are original procedural assets. No Mojang
textures or audio are distributed. Gameplay reference: [Minecraft Wiki](https://minecraft.wiki/w/Experience).
