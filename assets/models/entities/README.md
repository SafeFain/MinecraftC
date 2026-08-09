# MinecraftC entity models

These eight GLBs are original MinecraftC assets generated deterministically by
`tools/generate_entity_models.py` version 4 with seed `0x4D43474C`. They are
licensed under CC0-1.0. No Minecraft, Mojang, or third-party model or texture
data was copied or adapted.

The runtime files use glTF 2.0, Y-up coordinates, meters, local forward `-Z`,
and place the feet/base at local `Y=0`. Each contains an embedded original
64x64 RGBA semantic skin with nearest filtering, independent head/body faces,
half-texel-inset UVs, and a genuine multi-joint skin
containing a root plus rigidly weighted body-part joints. Every vertex uses at
most four weights and every skin remains below the 64-joint runtime limit. Each
block-style material is explicitly double-sided so animated entities retain
their exterior surfaces across both OpenGL and Vulkan projection paths. Each
asset contains seamless `idle`, `walk`, `hurt`, and `death` animation clips.
Hostile assets also contain an original `attack` clip. Adjacent version-1
`.anim.json` files define runtime layers, masks, transitions, priorities, and
gameplay event times. The simple
proportions and palettes are intentionally distinct for each creature and are
inputs in the generator's model tables.

Regenerate and verify with:

```bash
python3 tools/generate_entity_models.py --output assets/models/entities
python3 tests/test_entity_models.py
```
