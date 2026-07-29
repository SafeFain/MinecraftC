# MinecraftC entity models

These eight GLBs are original MinecraftC assets generated deterministically by
`tools/generate_entity_models.py` version 1 with seed `0x4D43474C`. They are
licensed under CC0-1.0. No Minecraft, Mojang, or third-party model or texture
data was copied or adapted.

The runtime files use glTF 2.0, Y-up coordinates, meters, local forward `-Z`,
and place the feet/base at local `Y=0`. Each contains an embedded original
16x16 RGBA pixel texture with nearest filtering and a genuine multi-joint skin
containing a root plus rigidly weighted body-part joints. Every vertex uses at
most four weights and every skin remains below the 64-joint runtime limit. Each
asset contains `idle`, `walk`, `hurt`, and `death` animation clips. The simple
proportions and palettes are intentionally distinct for each creature and are
inputs in the generator's model tables.

Regenerate and verify with:

```bash
python3 tools/generate_entity_models.py --output assets/models/entities
python3 tests/test_entity_models.py
```
