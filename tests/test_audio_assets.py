#!/usr/bin/env python3
import hashlib
import math
import struct
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXPECTED = {
    "menu_whimsy.wav": (104.0, 64, "76f849f2457d04167917802868c73cde275456767da45e8f01775f6d531c87b7"),
    "menu_spark.wav": (112.0, 64, "f4dea09da8fec6d30548fb42e0e554e75f1d5ed73613209bf2f82d1188392085"),
    "gameplay_calm.wav": (64.0, 64, "284e126a74342219e9f244112df3dcad5aa4915a336be4931ba6a9f0016d13a1"),
    "overworld_solitude.wav": (60.0, 64, "84d58d32ca28a058133f88997bae62910c5ac88447c6cc16b72bc8e72ee63b95"),
    "overworld_horizon.wav": (72.0, 96, "d41163b70d066263d70c53bb20ae7da48a2d6df0bc24ed4952376ea2aea876b2"),
    "overworld_nightfall.wav": (54.0, 64, "ec9bc67b38a8e69bc18d8add5e9587935717bbe65f3979514e4624c9fa6bddc3"),
    "heaven_ether.wav": (56.0, 64, "ed79437f567ef8f11ed1632b07dc298689664d69cba0e8578b839a320add4a64"),
    "heaven_sanctum.wav": (48.0, 64, "30e1b2ff0d7b5c4ad6f7dad15c9c3eeeb607c779131df1cb7a54a7e73d4ce53e"),
}


def require(condition, message):
    if not condition:
        raise SystemExit(f"FAILED: {message}")


for filename, (bpm, beats, digest) in EXPECTED.items():
    path = ROOT / "assets" / "audio" / filename
    require(path.is_file(), f"missing music asset {filename}")
    require(hashlib.sha256(path.read_bytes()).hexdigest() == digest,
            f"{filename} differs from its deterministic generated asset")
    with wave.open(str(path), "rb") as source:
        require(source.getnchannels() == 2, f"{filename} is not stereo")
        require(source.getsampwidth() == 2, f"{filename} is not 16-bit PCM")
        require(source.getframerate() == 24000, f"{filename} sample rate changed")
        expected_frames = round(beats * 60.0 / bpm * source.getframerate())
        require(source.getnframes() == expected_frames,
                f"{filename} does not contain one complete musical loop")
        raw = source.readframes(source.getnframes())
    values = struct.unpack(f"<{len(raw) // 2}h", raw)
    peak = max(abs(value) for value in values) / 32768.0
    rms = math.sqrt(sum(value * value for value in values) / len(values)) / 32768.0
    stereo_delta = math.sqrt(sum(
        (values[index] - values[index + 1]) ** 2
        for index in range(0, len(values), 2)) / (len(values) / 2)) / 32768.0
    seam = max(abs(values[0] - values[-2]), abs(values[1] - values[-1])) / 32768.0
    require(0.015 < rms < 0.24, f"{filename} loudness is invalid")
    require(peak < 0.96, f"{filename} clips")
    require(stereo_delta > 0.002, f"{filename} collapsed to mono")
    require(seam < 0.015, f"{filename} has an audible loop discontinuity")

require((ROOT / "tools" / "audio_generator.py").is_file(),
        "music assets have no reproducible generator")
print("music assets are deterministic, stereo, bounded, and loop-safe")
