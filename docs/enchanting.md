# Enchanting and equipment progression

Equipment carries applied enchantments, stored book enchantments, a UTF-8 custom name and prior anvil penalty. These properties survive inventory gestures, drops, containers, world/profile storage, LAN snapshots and netherite upgrades. Ordinary crafting retains its recipe semantics.

The 32 native enchantments are defined separately by level, weight, exclusions, applicability and acquisition source. Enchanting tables exclude treasure enchantments. Librarian offers are stable per villager; fishing and existing chest pools can supply books and enchanted equipment. Soul Speed is restricted to bastion rewards and piglin barter; Swift Sneak uses jungle ruins in this game's existing structure set.

Craft paper from reeds, books from paper/leather, bookshelves, iron blocks, an enchanting table and an anvil. Lapis veins use an independent coordinate seed domain after existing ore decisions. Global generation 22 retains seed layout, Heaven 9 and Nether terrain 1. Older generation worlds remain visible but cannot be played or rewritten by a refused load.

An enchanting table accepts one eligible item and lapis. Up to 15 unobstructed bookshelves affect the three offers. The displayed requirement differs from payment: the options consume 1/2/3 levels and lapis. Reopening retains the player's enchanting seed; a successful enchant changes it.

Anvils repair with materials, combine equipment or stored books, and rename or clear names. Prior operation penalties and enchantment costs determine the level fee; Survival rejects fees of 40 or more, with the Java pure-rename exception. Creative has the cost exceptions. Use can damage an anvil. Unsupported anvils fall, retain their state across saves and can damage entities on landing.

Grindstones remove non-curse enchantments and repair matching equipment. Names and curses remain. Experience is emitted only after successful output extraction. All output gestures run the same host transaction; clients submit actions, never result items.

Mending considers the selected main hand, offhand and armor, randomly choosing damaged eligible equipment. Each experience point repairs two durability; the remainder goes to the experience bar. Backpack items and direct `/xp` grants do not participate. Unbreaking has distinct armor and non-armor rolls. Binding blocks Survival unequipping; Vanishing removes death drops unless inventory is retained.

Frost Walker creates persisted, scheduled frosted ice without loading distant chunks. Mining, melee, arrows, armor protection, breathing, movement and fishing consume their relevant enchantment levels. `/enchant <targets> <enchantment> [level]` requires cheats; Creative provides all stored book variants.

World writes use format 24 (reads 22/23), player profiles use 7 (reads 5/6), and LAN uses protocol 9. Generation compatibility remains independent of storage readability. Cursor, crafting and station inputs are durable player state and are returned when the window closes. The plugin ABI remains 1.

Original procedural materials are documented in `assets/textures/LICENSE.md`; no Mojang art or audio is distributed.

Rule references: the [Java data model announcement](https://www.minecraft.net/en-us/article/minecraft-snapshot-24w18a), extracted [1.21.11 Fire Protection](https://raw.githubusercontent.com/misode/mcmeta/1.21.11-data/data/minecraft/enchantment/fire_protection.json), [Blast Protection](https://raw.githubusercontent.com/misode/mcmeta/1.21.11-data/data/minecraft/enchantment/blast_protection.json) and [Power](https://raw.githubusercontent.com/misode/mcmeta/1.21.11-data/data/minecraft/enchantment/power.json) data. Fire duration and explosion knockback attributes accumulate across equipped pieces and clamp at zero remaining duration/impulse.

Recent enchantment hit attribution has a persisted five-second lifetime. Looting resolves the credited online player's current held item at death, including after reload; disconnected owners confer no held-item bonus. Iron golem iron and saddles do not gain Looting counts. This context does not change the existing transient experience kill-credit policy.
