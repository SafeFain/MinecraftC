#!/usr/bin/env python3
"""Generate MinecraftC's original, loopable music assets."""

from __future__ import annotations

import argparse
import math
import struct
import wave
from pathlib import Path

SAMPLE_RATE = 24_000
TAU = math.tau
MENU_DURATION = 64.0 * 60.0 / 104.0
GAMEPLAY_DURATION = 64.0 * 60.0 / 64.0


def note(midi: int) -> float:
    return 440.0 * 2.0 ** ((midi - 69) / 12.0)


def smoothstep(value: float) -> float:
    value = max(0.0, min(1.0, value))
    return value * value * (3.0 - 2.0 * value)


def event_envelope(age: float, duration: float, attack: float, release: float) -> float:
    if age < 0.0 or age >= duration:
        return 0.0
    return smoothstep(age / attack) * smoothstep((duration - age) / release)


def bell(freq: float, age: float) -> float:
    return (math.sin(TAU * freq * age) +
            0.32 * math.sin(TAU * freq * 2.01 * age) +
            0.12 * math.sin(TAU * freq * 3.98 * age))


def warm_tone(freq: float, age: float) -> float:
    return (math.sin(TAU * freq * age) +
            0.18 * math.sin(TAU * freq * 2.0 * age) +
            0.06 * math.sin(TAU * freq * 3.0 * age))


def loop_sine(freq: float, seconds: float, duration: float) -> float:
    cycles = round(freq * duration)
    return math.sin(TAU * cycles * seconds / duration)


MENU_CHORDS = (
    (60, 64, 67, 71), (57, 60, 64, 67), (65, 69, 72, 76), (67, 71, 74, 79),
    (60, 64, 67, 74), (64, 67, 71, 76), (65, 69, 72, 76), (67, 72, 74, 79),
)
MENU_MELODY = (
    76, 79, 83, 81, 79, 76, 74, 76, 81, 79, 76, 72, 74, 76, 79, 83,
    84, 83, 79, 76, 77, 81, 79, 76, 74, 76, 79, 81, 79, 76, 74, 72,
)
ARP = (0, 1, 2, 1, 3, 2, 1, 2)


def menu_sample(seconds: float, beat: float) -> tuple[float, float]:
    chord = MENU_CHORDS[int(beat // 8.0) % len(MENU_CHORDS)]
    half_step = int(beat * 2.0)
    half_age_beats = beat - half_step * 0.5
    half_age = half_age_beats * 60.0 / 104.0
    arp_midi = chord[ARP[half_step % len(ARP)]] + 12
    arp_env = event_envelope(half_age_beats, 0.5, 0.025, 0.34)
    arp = bell(note(arp_midi), half_age) * arp_env * 0.105
    arp_pan = -0.32 if half_step % 2 == 0 else 0.32

    melody_step = int(beat / 2.0)
    melody_age_beats = beat - melody_step * 2.0
    melody_age = melody_age_beats * 60.0 / 104.0
    melody_env = event_envelope(melody_age_beats, 1.72, 0.08, 0.7)
    melody = bell(note(MENU_MELODY[melody_step % len(MENU_MELODY)]), melody_age)
    melody *= melody_env * 0.07

    bass_step = int(beat / 4.0)
    bass_age_beats = beat - bass_step * 4.0
    bass_age = bass_age_beats * 60.0 / 104.0
    bass_env = event_envelope(bass_age_beats, 3.65, 0.18, 1.15)
    bass = warm_tone(note(chord[0] - 12), bass_age) * bass_env * 0.055

    shimmer = loop_sine(note(chord[2] + 12), seconds, MENU_DURATION) * 0.012
    shimmer *= 0.55 + 0.45 * math.sin(TAU * beat / 16.0) ** 2
    left = bass + melody * 0.72 + arp * (1.0 - arp_pan) + shimmer * 0.8
    right = bass + melody + arp * (1.0 + arp_pan) + shimmer
    return left, right


GAME_CHORDS = (
    (48, 55, 60, 64), (45, 52, 57, 60), (41, 48, 53, 57), (43, 50, 55, 60),
    (48, 55, 59, 64), (40, 47, 52, 55), (41, 48, 53, 57), (43, 50, 55, 59),
)
GAME_MELODY = (72, -1, 67, 69, 64, -1, 67, 72, 71, -1, 67, 64, 65, -1, 64, 62)


def gameplay_sample(seconds: float, beat: float) -> tuple[float, float]:
    chord_index = int(beat // 8.0) % len(GAME_CHORDS)
    chord = GAME_CHORDS[chord_index]
    chord_age_beats = beat - int(beat // 8.0) * 8.0
    chord_age = chord_age_beats * 60.0 / 64.0
    pad_env = event_envelope(chord_age_beats, 8.0, 1.15, 1.35)
    left = 0.0
    right = 0.0
    for index, midi in enumerate(chord):
        freq = note(midi)
        voice = warm_tone(freq, chord_age) * pad_env * (0.038 if index else 0.05)
        left += voice * (0.82 if index % 2 else 1.0)
        right += voice * (1.0 if index % 2 else 0.82)

    melody_step = int(beat / 4.0)
    melody_age_beats = beat - melody_step * 4.0
    midi = GAME_MELODY[melody_step % len(GAME_MELODY)]
    if midi >= 0:
        melody_age = melody_age_beats * 60.0 / 64.0
        melody_env = event_envelope(melody_age_beats, 3.35, 0.28, 1.2)
        melody = (math.sin(TAU * note(midi) * melody_age) +
                  0.16 * math.sin(TAU * note(midi) * 2.0 * melody_age))
        melody *= melody_env * 0.052
        left += melody * 0.72
        right += melody

    breath = loop_sine(note(chord[1] + 12), seconds, GAMEPLAY_DURATION) * 0.006
    breath *= 0.5 + 0.5 * math.sin(TAU * beat / 32.0) ** 2
    return left + breath, right + breath * 0.8


def write_track(path: Path, bpm: float, beats: int, sampler) -> None:
    duration = beats * 60.0 / bpm
    frames = round(duration * SAMPLE_RATE)
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as output:
        output.setnchannels(2)
        output.setsampwidth(2)
        output.setframerate(SAMPLE_RATE)
        chunk = bytearray()
        for frame in range(frames):
            seconds = frame / SAMPLE_RATE
            beat = frame * bpm / (60.0 * SAMPLE_RATE)
            left, right = sampler(seconds, beat)
            left = max(-0.96, min(0.96, left))
            right = max(-0.96, min(0.96, right))
            chunk.extend(struct.pack("<hh", round(left * 32767), round(right * 32767)))
            if len(chunk) >= 262_144:
                output.writeframesraw(chunk)
                chunk.clear()
        if chunk:
            output.writeframesraw(chunk)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path,
                        default=Path("assets/audio"))
    args = parser.parse_args()
    write_track(args.output / "menu_whimsy.wav", 104.0, 64, menu_sample)
    write_track(args.output / "gameplay_calm.wav", 64.0, 64, gameplay_sample)


if __name__ == "__main__":
    main()
