# MinecraftC Music

`menu_whimsy.wav` and `gameplay_calm.wav` are original music written for
MinecraftC and generated deterministically by `tools/audio_generator.py`.
They are project assets distributed under MinecraftC's GPL-3.0-only license.

- `menu_whimsy.wav`: bright bell arpeggios and a playful 104 BPM melody.
- `gameplay_calm.wav`: warm pads and a sparse 64 BPM exploration melody.

Both files are stereo 16-bit PCM WAV at 24 kHz and are authored as complete
loops. Regenerate them from the repository root with:

```bash
python3 tools/audio_generator.py
```
