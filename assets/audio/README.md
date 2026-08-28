# MinecraftC Music

The eight tracks in this directory are original music written for MinecraftC
and generated deterministically by `tools/audio_generator.py`. They are project
assets distributed under MinecraftC's GPL-3.0-only license.

- `menu_whimsy.wav`: bright bell arpeggios and a playful 104 BPM melody.
- `menu_spark.wav`: nimble crystalline plucks and a lively 112 BPM melody.
- The main menu independently chooses either menu track at startup and after
  each complete track, so playback has no fixed sequence.
- `gameplay_calm.wav`: warm pads and a sparse 64 BPM exploration melody,
  reserved for the Overworld.
- `overworld_solitude.wav`: sparse felt piano and long silences at 60 BPM,
  reserved for the Overworld.
- `overworld_horizon.wav`: low bowed tones and distant melodic calls at 72 BPM,
  reserved for the Overworld.
- `overworld_nightfall.wav`: dark drones and isolated glass tones at 54 BPM,
  reserved for the Overworld.
- The Overworld independently chooses one of its four tracks when entered and
  after each complete track, so playback has no fixed sequence.
- `heaven_ether.wav`: slowly breathing pads and floating crystal tones at 56
  BPM, reserved for Heaven.
- `heaven_sanctum.wav`: a slow pipe-organ chorale with celestial chimes at 48
  BPM, reserved for Heaven.
- Heaven independently chooses either of its tracks when entered and after each
  complete track, so playback has no fixed sequence.

All files are stereo 16-bit PCM WAV at 24 kHz and are authored as complete
loops. Regenerate them from the repository root with:

```bash
python3 tools/audio_generator.py
```
