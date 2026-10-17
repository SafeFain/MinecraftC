# Seeded spawn and structure V2 fixture

Native ABI 1 example, loaded by `plugin_seeded_spawn_and_structure_v2`.
Queries dimensions/structures extension version 2 while retaining their V1 tables.
The seed selects a spawn on Y=100 although the highest island is Y=240.
Accepted structures have two independently ordered instances per cell, signed
relative Y, AIR overwrites, and an exact zero loot seed.

Callbacks receive the already-derived dimension generation seed and actual
parameters. They must remain pure, concurrent and independent of load order.
This is an SDK regression fixture, not the official Heaven provider.
