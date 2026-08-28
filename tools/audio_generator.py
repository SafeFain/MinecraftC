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
MENU_SPARK_DURATION = 64.0 * 60.0 / 112.0
HEAVEN_DURATION = 64.0 * 60.0 / 56.0
SOLITUDE_DURATION = 64.0 * 60.0 / 60.0
HORIZON_DURATION = 96.0 * 60.0 / 72.0
NIGHTFALL_DURATION = 64.0 * 60.0 / 54.0
SANCTUM_DURATION = 64.0 * 60.0 / 48.0


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


SPARK_CHORDS = (
    (62, 66, 69, 73), (59, 62, 66, 69), (55, 59, 62, 66), (57, 61, 64, 69),
    (62, 66, 69, 74), (54, 59, 62, 66), (55, 59, 62, 67), (57, 61, 64, 69),
)
SPARK_MELODY = (
    78, 81, 85, 83, 81, 78, 76, 73, 74, 78, 81, 86, 85, 81, 78, 76,
    78, 83, 86, 90, 88, 85, 81, 78, 79, 83, 81, 78, 76, 74, 73, 76,
)
SPARK_ARP = (0, 2, 1, 3, 2, 1, 3, 1)


def menu_spark_sample(seconds: float, beat: float) -> tuple[float, float]:
    chord = SPARK_CHORDS[int(beat // 8.0) % len(SPARK_CHORDS)]
    quarter_step = int(beat * 4.0)
    quarter_age_beats = beat - quarter_step * 0.25
    quarter_age = quarter_age_beats * 60.0 / 112.0
    pluck_midi = chord[SPARK_ARP[quarter_step % len(SPARK_ARP)]] + 12
    pluck_env = event_envelope(quarter_age_beats, 0.22, 0.012, 0.16)
    pluck = (bell(note(pluck_midi), quarter_age) * 0.075 +
             math.sin(TAU * note(pluck_midi + 12) * quarter_age) * 0.018)
    pluck *= pluck_env
    pluck_pan = (-0.42, 0.18, 0.42, -0.16)[quarter_step % 4]

    melody_step = int(beat * 0.5)
    melody_age_beats = beat - melody_step * 2.0
    melody_age = melody_age_beats * 60.0 / 112.0
    melody_env = event_envelope(melody_age_beats, 1.55, 0.035, 0.52)
    melody = bell(note(SPARK_MELODY[melody_step % len(SPARK_MELODY)]),
                  melody_age) * melody_env * 0.052

    bass_step = int(beat / 4.0)
    bass_age_beats = beat - bass_step * 4.0
    bass_age = bass_age_beats * 60.0 / 112.0
    bass_env = event_envelope(bass_age_beats, 3.5, 0.08, 0.75)
    bass = warm_tone(note(chord[0] - 12), bass_age) * bass_env * 0.038

    glint = loop_sine(note(chord[1] + 24), seconds, MENU_SPARK_DURATION)
    glint *= (0.004 + 0.004 * math.sin(TAU * beat / 8.0) ** 2)
    left = bass + melody + pluck * (1.0 - pluck_pan) + glint
    right = bass * 0.86 + melody * 0.76 + pluck * (1.0 + pluck_pan) + glint * 0.7
    return left, right


HEAVEN_CHORDS = (
    (50, 57, 62, 66, 69), (47, 54, 59, 62, 66),
    (43, 50, 55, 59, 62), (45, 52, 57, 61, 64),
)
HEAVEN_MELODY = (81, 86, 83, 78, 79, 83, 88, 85)


def heaven_sample(seconds: float, beat: float) -> tuple[float, float]:
    chord = HEAVEN_CHORDS[int(beat // 16.0) % len(HEAVEN_CHORDS)]
    chord_age_beats = beat - int(beat // 16.0) * 16.0
    chord_age = chord_age_beats * 60.0 / 56.0
    pad_env = event_envelope(chord_age_beats, 16.0, 2.8, 3.2)
    left = 0.0
    right = 0.0
    for index, midi in enumerate(chord):
        freq = note(midi)
        phase = TAU * freq * chord_age
        voice = (math.sin(phase) + 0.12 * math.sin(phase * 2.002) +
                 0.035 * math.sin(phase * 3.997))
        voice *= pad_env * (0.026 if index else 0.034)
        left += voice * (1.0 if index % 2 == 0 else 0.72)
        right += voice * (0.72 if index % 2 == 0 else 1.0)

    melody_step = int(beat / 8.0)
    melody_age_beats = beat - melody_step * 8.0
    melody_age = melody_age_beats * 60.0 / 56.0
    melody_env = event_envelope(melody_age_beats, 6.4, 0.18, 2.7)
    melody_freq = note(HEAVEN_MELODY[melody_step % len(HEAVEN_MELODY)])
    crystal = (math.sin(TAU * melody_freq * melody_age) +
               0.28 * math.sin(TAU * melody_freq * 2.01 * melody_age) +
               0.09 * math.sin(TAU * melody_freq * 4.03 * melody_age))
    crystal *= melody_env * 0.042
    crystal_pan = math.sin(TAU * melody_step / len(HEAVEN_MELODY)) * 0.36

    high_air = loop_sine(note(chord[3] + 12), seconds, HEAVEN_DURATION)
    low_air = loop_sine(note(chord[0] - 12), seconds, HEAVEN_DURATION)
    breathing = 0.5 + 0.5 * math.sin(TAU * beat / 32.0) ** 2
    high_air *= 0.006 * breathing
    low_air *= 0.008 * (1.0 - 0.35 * breathing)
    left += crystal * (1.0 - crystal_pan) + high_air + low_air
    right += crystal * (1.0 + crystal_pan) + high_air * 0.68 + low_air * 0.82
    return left, right


SOLITUDE_CHORDS = (
    (45, 52, 57, 60), (41, 48, 52, 57), (38, 45, 50, 53), (43, 50, 55, 59),
    (45, 52, 56, 60), (40, 47, 52, 55), (41, 48, 53, 57), (43, 50, 54, 59),
)
SOLITUDE_NOTES = (69, -1, 64, 67, -1, 60, 62, -1, 71, 67, -1, 64, 65, -1, 62, 59)


def overworld_solitude_sample(seconds: float, beat: float) -> tuple[float, float]:
    """A close, sparse felt-piano piece with long silences."""
    chord = SOLITUDE_CHORDS[int(beat // 8.0) % len(SOLITUDE_CHORDS)]
    chord_age_beats = beat % 8.0
    chord_age = chord_age_beats * 60.0 / 60.0
    room_env = event_envelope(chord_age_beats, 7.8, 1.4, 2.5)
    left = 0.0
    right = 0.0
    for index, midi in enumerate(chord):
        tone = math.sin(TAU * note(midi) * chord_age)
        tone += 0.09 * math.sin(TAU * note(midi) * 2.003 * chord_age)
        tone *= room_env * (0.024 if index else 0.034)
        left += tone * (1.0 if index in (0, 3) else 0.68)
        right += tone * (0.68 if index in (0, 3) else 1.0)

    phrase = int(beat // 4.0)
    phrase_age_beats = beat % 4.0
    midi = SOLITUDE_NOTES[phrase % len(SOLITUDE_NOTES)]
    if midi >= 0:
        age = phrase_age_beats * 60.0 / 60.0
        envelope = event_envelope(phrase_age_beats, 3.15, 0.018, 1.65)
        # The short upper partial gives the attack a muted, felt-piano edge.
        piano = (math.sin(TAU * note(midi) * age) +
                 0.24 * math.sin(TAU * note(midi) * 2.01 * age) +
                 0.045 * math.sin(TAU * note(midi) * 3.98 * age))
        piano *= envelope * 0.052
        pan = (-0.18, 0.12, 0.28, -0.08)[phrase % 4]
        left += piano * (1.0 - pan)
        right += piano * (1.0 + pan)

    tape_air = loop_sine(note(chord[2] + 12), seconds, SOLITUDE_DURATION)
    tape_air *= 0.0045 * (0.55 + 0.45 * math.sin(TAU * beat / 32.0) ** 2)
    return left + tape_air, right + tape_air * 0.72


HORIZON_CHORDS = (
    (43, 50, 55), (46, 53, 58), (41, 48, 53), (48, 55, 60),
    (45, 52, 57), (38, 45, 50), (43, 50, 54), (36, 43, 48),
)
HORIZON_CALLS = (67, 70, -1, 65, 72, -1, 69, 67, 62, -1, 65, 60, -1, 64, 62, -1)


def overworld_horizon_sample(seconds: float, beat: float) -> tuple[float, float]:
    """Low bowed tones and distant calls suggest a broad, empty horizon."""
    chord = HORIZON_CHORDS[int(beat // 12.0) % len(HORIZON_CHORDS)]
    age_beats = beat % 12.0
    age = age_beats * 60.0 / 72.0
    swell = event_envelope(age_beats, 12.0, 2.8, 3.4)
    left = 0.0
    right = 0.0
    for index, midi in enumerate(chord):
        freq = note(midi)
        bowed = (math.sin(TAU * freq * age) +
                 0.16 * math.sin(TAU * freq * 2.0 * age) +
                 0.025 * math.sin(TAU * freq * 5.0 * age))
        bowed *= swell * (0.036 if index == 0 else 0.027)
        left += bowed * (0.75 if index == 1 else 1.0)
        right += bowed * (1.0 if index == 1 else 0.72)

    call_step = int(beat // 6.0)
    call_age_beats = beat % 6.0
    midi = HORIZON_CALLS[call_step % len(HORIZON_CALLS)]
    if midi >= 0:
        call_age = call_age_beats * 60.0 / 72.0
        call_env = event_envelope(call_age_beats, 4.7, 0.38, 2.1)
        call = warm_tone(note(midi), call_age) * call_env * 0.036
        pan = math.sin(TAU * call_step / len(HORIZON_CALLS)) * 0.38
        left += call * (1.0 - pan)
        right += call * (1.0 + pan)

    wind = loop_sine(note(chord[0] - 12), seconds, HORIZON_DURATION)
    wind *= 0.007 * (0.65 + 0.35 * math.sin(TAU * beat / 48.0) ** 2)
    return left + wind, right + wind * 0.82


NIGHTFALL_CHORDS = (
    (40, 47, 52, 59), (43, 50, 55, 62), (38, 45, 50, 57), (45, 52, 57, 64),
)
NIGHTFALL_LIGHTS = (76, 71, 79, -1, 74, 69, 77, -1)


def overworld_nightfall_sample(seconds: float, beat: float) -> tuple[float, float]:
    """Dark drones and isolated glass notes form a quiet nocturne."""
    chord = NIGHTFALL_CHORDS[int(beat // 16.0) % len(NIGHTFALL_CHORDS)]
    age_beats = beat % 16.0
    age = age_beats * 60.0 / 54.0
    drone_env = event_envelope(age_beats, 16.0, 3.6, 4.2)
    left = 0.0
    right = 0.0
    for index, midi in enumerate(chord):
        freq = note(midi)
        drone = (math.sin(TAU * freq * age) +
                 0.07 * math.sin(TAU * freq * 1.501 * age))
        drone *= drone_env * (0.033 if index == 0 else 0.022)
        left += drone * (1.0 if index % 2 == 0 else 0.62)
        right += drone * (0.62 if index % 2 == 0 else 1.0)

    light_step = int(beat // 8.0)
    light_age_beats = beat % 8.0
    midi = NIGHTFALL_LIGHTS[light_step % len(NIGHTFALL_LIGHTS)]
    if midi >= 0:
        light_age = light_age_beats * 60.0 / 54.0
        light_env = event_envelope(light_age_beats, 5.8, 0.06, 3.0)
        light = bell(note(midi), light_age) * light_env * 0.034
        pan = (-0.42, 0.34, 0.12, -0.2)[light_step % 4]
        left += light * (1.0 - pan)
        right += light * (1.0 + pan)

    moon_air = loop_sine(note(chord[2] + 12), seconds, NIGHTFALL_DURATION)
    moon_air *= 0.004 * (0.4 + 0.6 * math.sin(TAU * beat / 32.0) ** 2)
    return left + moon_air * 0.65, right + moon_air


SANCTUM_CHORDS = (
    (36, 48, 55, 60, 64), (41, 53, 57, 60, 65),
    (43, 55, 59, 62, 67), (38, 50, 57, 62, 66),
)
SANCTUM_CHIMES = (84, 79, 88, 83, 86, 81, 91, 86)


def heaven_sanctum_sample(seconds: float, beat: float) -> tuple[float, float]:
    """A slow pipe-organ chorale suspended beneath distant celestial chimes."""
    chord = SANCTUM_CHORDS[int(beat // 16.0) % len(SANCTUM_CHORDS)]
    age_beats = beat % 16.0
    age = age_beats * 60.0 / 48.0
    organ_env = event_envelope(age_beats, 16.0, 3.2, 3.8)
    left = 0.0
    right = 0.0
    for index, midi in enumerate(chord):
        freq = note(midi)
        # Odd and octave partials evoke gently voiced diapason pipes.
        organ = (math.sin(TAU * freq * age) +
                 0.21 * math.sin(TAU * freq * 2.0 * age) +
                 0.12 * math.sin(TAU * freq * 3.0 * age) +
                 0.045 * math.sin(TAU * freq * 4.0 * age))
        organ *= organ_env * (0.032 if index < 2 else 0.022)
        left += organ * (1.0 if index % 2 == 0 else 0.74)
        right += organ * (0.74 if index % 2 == 0 else 1.0)

    chime_step = int(beat // 8.0)
    chime_age_beats = beat % 8.0
    chime_age = chime_age_beats * 60.0 / 48.0
    chime_env = event_envelope(chime_age_beats, 6.0, 0.12, 3.4)
    chime = bell(note(SANCTUM_CHIMES[chime_step % len(SANCTUM_CHIMES)]),
                 chime_age) * chime_env * 0.025
    chime_pan = math.sin(TAU * (chime_step + 1) / len(SANCTUM_CHIMES)) * 0.44

    choir = loop_sine(note(chord[3] + 12), seconds, SANCTUM_DURATION)
    choir *= 0.0055 * (0.55 + 0.45 * math.sin(TAU * beat / 32.0) ** 2)
    left += chime * (1.0 - chime_pan) + choir
    right += chime * (1.0 + chime_pan) + choir * 0.7
    return left, right


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
    write_track(args.output / "menu_spark.wav", 112.0, 64, menu_spark_sample)
    write_track(args.output / "gameplay_calm.wav", 64.0, 64, gameplay_sample)
    write_track(args.output / "overworld_solitude.wav", 60.0, 64,
                overworld_solitude_sample)
    write_track(args.output / "overworld_horizon.wav", 72.0, 96,
                overworld_horizon_sample)
    write_track(args.output / "overworld_nightfall.wav", 54.0, 64,
                overworld_nightfall_sample)
    write_track(args.output / "heaven_ether.wav", 56.0, 64, heaven_sample)
    write_track(args.output / "heaven_sanctum.wav", 48.0, 64,
                heaven_sanctum_sample)


if __name__ == "__main__":
    main()
