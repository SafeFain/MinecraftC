#!/usr/bin/env python3
"""Render original MinecraftC scores with the Python standard library only."""

from __future__ import annotations

import argparse
from array import array
from dataclasses import dataclass
from functools import lru_cache
import hashlib
import json
import math
from pathlib import Path
import sys
import wave

from audio_score import Event, TRACKS, Track, compose

SAMPLE_RATE = 24_000
GENERATOR_VERSION = 2
TABLE_SIZE = 4096
PEAK_LIMIT = .88  # Below -1 dBFS; no clipping/hard limiting in the master.


@dataclass(frozen=True)
class Instrument:
    partials: tuple
    attack: float
    release: float
    decay: float
    transient: float
    transient_ratio: float
    transient_decay: float
    detune: float = 0.0
    breath: float = 0.0


INSTRUMENTS = {
    "piano": Instrument((1, .25, .075, .025), .008, .8, 1.7, .23, 2.003, .14),
    "bell": Instrument((1, .08, .015), .006, 1.4, 2.2, .43, 2.013, .7),
    "pluck": Instrument((1, .32, .15, .06, .02), .004, .35, .4, .12, 3.997, .06),
    "bowed": Instrument((1, .3, .15, .075, .035), .65, 1.4, 18, .04, 2, .5, .0009, .08),
    "pad": Instrument((1, .18, .07, .018), 1.1, 1.8, 26, .025, 3, .8, .0015, .12),
    "organ": Instrument((1, .32, .18, .09, .035, .018), .32, 1.5, 60, .02, 2, .3, .00035),
    "air": Instrument((.65, .035, .012), 1.6, 2, 24, .045, 1.501, 4, .002, .3),
}


def note(midi: int) -> float:
    return 440.0 * 2.0 ** ((midi - 69) / 12.0)


def smoothstep(value: float) -> float:
    value = max(0.0, min(1.0, value))
    return value * value * (3.0 - 2.0 * value)


def event_envelope(age: float, duration: float, attack: float, release: float) -> float:
    if age < 0 or age >= duration:
        return 0.0
    return smoothstep(age / attack) * smoothstep((duration - age) / release)


@lru_cache(maxsize=128)
def wavetable(voice: str, midi: int) -> tuple:
    freq = note(midi)
    # Exclude out-of-band harmonics before synthesis, including detuned voices.
    partials = [(i, gain) for i, gain in enumerate(INSTRUMENTS[voice].partials, 1)
                if i * freq * 1.003 < SAMPLE_RATE * .43]
    scale = sum(gain for _, gain in partials) or 1
    return tuple(sum(gain * math.sin(math.tau * i * index / TABLE_SIZE)
                     for i, gain in partials) / scale for index in range(TABLE_SIZE))


SINE = tuple(math.sin(math.tau * i / TABLE_SIZE) for i in range(TABLE_SIZE))


@lru_cache(maxsize=48)
def synthesize(voice: str, midi: int, frames: int) -> array:
    """A bounded mono-note cache; velocity/pan remain independent of the cache."""
    instrument = INSTRUMENTS[voice]
    table = wavetable(voice, midi)
    freq = note(midi)
    step = freq * TABLE_SIZE / SAMPLE_RATE
    second_step = step * (1 + instrument.detune)
    transient_step = step * instrument.transient_ratio
    transient = instrument.transient if freq * instrument.transient_ratio < SAMPLE_RATE * .43 else 0
    body_decay = math.exp(-1 / (SAMPLE_RATE * instrument.decay))
    fast_decay = math.exp(-1 / (SAMPLE_RATE * instrument.transient_decay))
    attack = min(instrument.attack * SAMPLE_RATE, frames * .3)
    release = min(instrument.release * SAMPLE_RATE, frames * .45)
    phase = second = overtone = 0.0
    body = 1.0
    output = array("f")
    mask = TABLE_SIZE - 1
    breath_step = TABLE_SIZE * .42 / SAMPLE_RATE
    for index in range(frames):
        position = int(phase)
        blend = phase - position
        tone = table[position & mask] * (1 - blend) + table[(position + 1) & mask] * blend
        if instrument.detune:
            position2 = int(second)
            blend2 = second - position2
            tone = .68 * tone + .32 * (table[position2 & mask] * (1 - blend2) +
                                      table[(position2 + 1) & mask] * blend2)
        transient_pos = int(overtone)
        transient_blend = overtone - transient_pos
        strike = SINE[transient_pos & mask] * (1 - transient_blend) + SINE[(transient_pos + 1) & mask] * transient_blend
        a = min(1.0, index / attack)
        r = min(1.0, (frames - 1 - index) / release)
        envelope = a * a * (3 - 2 * a) * r * r * (3 - 2 * r)
        breathing = 1 - instrument.breath * (.5 + .5 * SINE[int(index * breath_step) & mask])
        output.append((tone * body + strike * transient) * envelope * breathing)
        phase = (phase + step) % TABLE_SIZE
        second = (second + second_step) % TABLE_SIZE
        overtone = (overtone + transient_step) % TABLE_SIZE
        body *= body_decay
        transient *= fast_decay
    return output


def pan_gains(midi: int, pan: float) -> tuple[float, float]:
    # Keep bass centered, gradually restore stereo placement above MIDI 48.
    pan *= max(0.0, min(1.0, (midi - 43) / 12))
    return math.sqrt((1 - pan) * .5), math.sqrt((1 + pan) * .5)


def render_events(events: tuple[Event, ...], bpm: float, frames: int) -> tuple[array, array]:
    left, right = array("f", [0]) * frames, array("f", [0]) * frames
    for event in events:
        count = max(2, round(event.duration * 60 / bpm * SAMPLE_RATE))
        samples = synthesize(event.voice, event.midi, count)
        start = round(event.beat * 60 / bpm * SAMPLE_RATE) % frames
        lg, rg = pan_gains(event.midi, event.pan)
        lg *= event.velocity
        rg *= event.velocity
        cursor = start
        for sample in samples:
            left[cursor] += sample * lg
            right[cursor] += sample * rg
            cursor += 1
            if cursor == frames:
                cursor = 0
    return left, right


def circular_room(source: array, delay: int, decay: float) -> array:
    """Damped feedback comb warmed with periodic input down to <1e-9 error.

    The filter is contractive. Warm-up includes extra delay traversals for
    the low-pass state; no truncated tail or zero-state start is published.
    """
    feedback = 10 ** (-3 * delay / (SAMPLE_RATE * decay))
    warmup = (math.ceil(math.log(1e-10) / math.log(feedback)) + 2) * delay
    ring = array("f", [0]) * delay
    output = array("f")
    damped = 0.0
    cursor = 0
    frames = len(source)
    for index in range(-warmup, frames):
        echo = ring[cursor]
        damped += .24 * (echo - damped)
        ring[cursor] = source[index % frames] + feedback * damped
        if index >= 0:
            output.append(echo)
        cursor += 1
        if cursor == delay:
            cursor = 0
    return output


def effects(left: array, right: array, track: Track) -> tuple[array, array]:
    frames = len(left)
    mono = array("f", ((a + b) * .5 for a, b in zip(left, right)))
    wet_left, wet_right = array("f", [0]) * frames, array("f", [0]) * frames
    for seconds, channel in ((.0677, wet_left), (.0893, wet_right),
                             (.1139, wet_left), (.1373, wet_right)):
        echo = circular_room(mono, round(seconds * SAMPLE_RATE), track.decay)
        for index, sample in enumerate(echo):
            channel[index] += sample * track.room * .5
    delay_left = round(.75 * 60 / track.bpm * SAMPLE_RATE)
    delay_right = round(1.25 * 60 / track.bpm * SAMPLE_RATE)
    for index in range(frames):
        wet_left[index] += left[index] + track.delay * right[(index - delay_left) % frames]
        wet_right[index] += right[index] + track.delay * left[(index - delay_right) % frames]
    return wet_left, wet_right


def master(left: array, right: array, target_rms: float) -> array:
    count = len(left)
    dc_left, dc_right = sum(left) / count, sum(right) / count
    energy = sum((x - dc_left) ** 2 for x in left) + sum((x - dc_right) ** 2 for x in right)
    rms = math.sqrt(energy / (count * 2))
    peak = max(max(abs(x - dc_left) for x in left), max(abs(x - dc_right) for x in right))
    gain = min(target_rms / max(rms, 1e-12), PEAK_LIMIT / max(peak, 1e-12))
    pcm = array("h")
    for a, b in zip(left, right):
        pcm.append(round((a - dc_left) * gain * 32767))
        pcm.append(round((b - dc_right) * gain * 32767))
    return pcm


def analyze(pcm: array) -> dict:
    frames = len(pcm) // 2
    left, right = pcm[::2], pcm[1::2]
    peak = max(abs(x) for x in pcm) / 32768
    rms = math.sqrt(sum(x * x for x in pcm) / len(pcm)) / 32768
    difference = math.sqrt(sum((a - b) ** 2 for a, b in zip(left, right)) / frames) / 32768
    seam = max(abs(left[0] - left[-1]), abs(right[0] - right[-1])) / 32768
    return {"frames": frames, "duration_seconds": frames / SAMPLE_RATE,
            "peak": peak, "peak_dbfs": 20 * math.log10(max(peak, 1e-12)),
            "rms": rms, "dc_offset": [sum(left) / frames / 32768, sum(right) / frames / 32768],
            "stereo_difference_rms": difference, "loop_seam": seam}


def render_track(track: Track) -> array:
    frames = round(track.beats * 60 / track.bpm * SAMPLE_RATE)
    left, right = render_events(compose(track), track.bpm, frames)
    left, right = effects(left, right, track)
    pcm = master(left, right, track.rms)
    synthesize.cache_clear()  # Bound memory across a complete album render.
    return pcm


def write_track(path: Path, track: Track) -> dict:
    pcm = render_track(track)
    report = analyze(pcm)
    path.parent.mkdir(parents=True, exist_ok=True)
    if sys.byteorder != "little":
        pcm.byteswap()
    with wave.open(str(path), "wb") as output:
        output.setnchannels(2)
        output.setsampwidth(2)
        output.setframerate(SAMPLE_RATE)
        output.writeframes(pcm.tobytes())
    report.update(track=track.name, bpm=track.bpm, beats=track.beats,
                  sha256=hashlib.sha256(path.read_bytes()).hexdigest())
    return report


def main(argv=None) -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("assets/audio"))
    parser.add_argument("--heaven-output", type=Path)
    parser.add_argument("--list", action="store_true", help="list scores without generating files")
    parser.add_argument("--track", action="append", choices=[t.name for t in TRACKS],
                        help="render only named scores; may be repeated")
    parser.add_argument("--report", type=Path, help="write deterministic PCM quality metrics as JSON")
    args = parser.parse_args(argv)
    if args.list:
        for track in TRACKS:
            print(f"{track.name}: {track.bpm:g} BPM, {track.beats} beats, {track.beats * 60 / track.bpm:.2f}s")
        return
    heaven_output = args.heaven_output or args.output.parent / "plugins/official_heaven/audio"
    selected = [t for t in TRACKS if args.track is None or t.name in args.track]
    destinations = [(t, (heaven_output if t.heaven else args.output) / (t.name + ".wav")) for t in selected]
    protected = {(heaven_output if t.heaven else args.output).resolve() / (t.name + ".wav")
                 for t in TRACKS}
    protected.add((heaven_output.parent / "resources.sha256.json").resolve())
    if args.report and args.report.resolve() in protected:
        parser.error("report path must not overwrite a music asset or package inventory")
    reports = []
    for track, path in destinations:
        report = write_track(path, track)
        reports.append(report)
        print(f"{track.name}: {report['duration_seconds']:.2f}s, peak {report['peak_dbfs']:.2f} dBFS, RMS {report['rms']:.4f}", flush=True)
    if any(t.heaven for t in selected) and (heaven_output.parent / "resources.sha256.json").is_file():
        from check_official_package import refresh
        refresh(heaven_output.parent)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps({"generator_version": GENERATOR_VERSION,
                                          "sample_rate": SAMPLE_RATE, "tracks": reports}, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
