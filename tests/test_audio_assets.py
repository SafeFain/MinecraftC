#!/usr/bin/env python3
"""Audio engine invariants and complete, byte-exact score regeneration."""

from array import array
from contextlib import redirect_stdout, redirect_stderr
from dataclasses import replace
import hashlib
import io
import json
import math
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import wave

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "tools"))
import audio_generator as generator
from audio_score import Event, TRACKS, compose, voice_lead

EXPECTED = {
    "menu_whimsy": (104, 96, "f704d95980f508c1886ea5d4a43326da922a2b1c9ecd7a92fa232eaaf1a932b0"),
    "menu_spark": (112, 96, "1eeed31f4c315ec7312460ef107dae22a7e0a244ca06907ba6331f3e6db6732f"),
    "gameplay_calm": (64, 128, "df4c2a50f76dad339abfdbae8d62636ee74530e81cf5e57080317e1e5672dac1"),
    "overworld_solitude": (60, 96, "fa8dcae097bc9a44df1355b245b90e912ac538d80cdca2e95371f85b73b6dbea"),
    "overworld_horizon": (72, 128, "13bf02e0d00bf9a957f03b2257aa73e61f104d4c49c76fefa5381c52b717980f"),
    "overworld_nightfall": (54, 96, "b764dd7094cbf3dab68b464484b0c3aae1236e3cff43a4d6eea8a69097106f27"),
    "heaven_ether": (56, 96, "a45a80b4282248b8932b72661d318b38f1d894c459451ccfe626e31ae4ba71e4"),
    "heaven_sanctum": (48, 96, "e0817640e8078a18bad736002c0545d34e75ca1a8f74942e08992af06ccce20e"),
}


def asset_path(track):
    folder = "assets/plugins/official_heaven/audio" if track.heaven else "assets/audio"
    return ROOT / folder / (track.name + ".wav")


class EngineTests(unittest.TestCase):
    def test_envelopes_and_seven_distinct_voices(self):
        envelope = generator.event_envelope
        self.assertEqual(envelope(-.1, 2, .1, .3), 0)
        self.assertEqual(envelope(0, 2, .1, .3), 0)
        self.assertEqual(envelope(2, 2, .1, .3), 0)
        self.assertAlmostEqual(envelope(1, 2, .1, .3), 1)
        voices = []
        for name in generator.INSTRUMENTS:
            samples = generator.synthesize(name, 69, 24000)
            self.assertEqual(samples[0], 0)
            self.assertEqual(samples[-1], 0)
            self.assertTrue(all(math.isfinite(x) for x in samples))
            self.assertGreater(max(abs(x) for x in samples), .01)
            voices.append(samples.tobytes())
        self.assertEqual(len(set(voices)), 7)
        # Out-of-band upper partials must be removed rather than folded back.
        self.assertLess(max(abs(x) for x in generator.wavetable("bell", 127)), 1.01)
        self.assertEqual(set(generator.wavetable("bell", 160)), {0})

    def test_scores_seed_variation_and_voice_leading(self):
        self.assertEqual(len(TRACKS), 8)
        for track in TRACKS:
            events = compose(track)
            self.assertEqual(events, compose(track))
            self.assertNotEqual(events, compose(replace(track, seed=track.seed + 1)))
            self.assertTrue(all(0 <= e.beat < track.beats and e.duration > 0 for e in events))
            # Intro, contrast and return have distinct note/voice/density content.
            phrases = [tuple((e.midi, e.voice) for e in events if start <= e.beat < start + 16)
                       for start in (0, track.beats // 32 * 16, track.beats - 16)]
            self.assertEqual(len(set(phrases)), 3)
        chord = (48, 55, 60, 64)
        led = voice_lead(chord, (45, 64, 67, 72))
        self.assertEqual(led[0], chord[0])
        self.assertEqual([x % 12 for x in led], [x % 12 for x in chord])

    def test_note_tails_wrap_and_cache_order_is_irrelevant(self):
        frames = 24000
        event = Event(.9, .4, 72, "piano", .1, .3)
        left, right = generator.render_events((event,), 60, frames)
        self.assertGreater(max(abs(x) for x in left[:6000]), .001)
        self.assertEqual(list(left[8000:20000]), [0] * 12000)
        generator.synthesize.cache_clear()
        generator.synthesize("bell", 81, 5000)
        again = generator.render_events((event,), 60, frames)
        self.assertEqual(left, again[0])
        self.assertEqual(right, again[1])
        bass_l, bass_r = generator.pan_gains(36, .7)
        self.assertEqual(bass_l, bass_r)
        lg, rg = generator.pan_gains(72, .7)
        self.assertAlmostEqual(lg * lg + rg * rg, 1)

    def test_reverb_periodic_state_and_decay(self):
        source = array("f", [0]) * 4800
        source[-100] = 1
        period = generator.circular_room(source, 331, .7)
        twice = generator.circular_room(source * 2, 331, .7)
        self.assertGreater(max(abs(x) for x in period[:1000]), .01)
        self.assertLess(max(abs(a - b) for a, b in zip(period, twice[4800:])), 2e-7)
        # Moving the impulse through the loop must simply rotate the response.
        shifted = source[500:] + source[:500]
        response = generator.circular_room(shifted, 331, .7)
        rotated = period[500:] + period[:500]
        self.assertLess(max(abs(a - b) for a, b in zip(response, rotated)), 2e-7)
        silence = generator.circular_room(array("f", [0]) * 4800, 331, .7)
        self.assertEqual(max(abs(x) for x in silence), 0)

    def test_master_removes_dc_and_preserves_dynamics(self):
        left = array("f", (.2 + .1 * math.sin(i * math.tau / 100) for i in range(1000)))
        right = array("f", (x - .4 for x in left))
        report = generator.analyze(generator.master(left, right, .07))
        self.assertLess(max(abs(x) for x in report["dc_offset"]), 1 / 32768)
        self.assertAlmostEqual(report["rms"], .07, places=4)
        peak = generator.analyze(generator.master(left, right, 5))["peak"]
        self.assertLessEqual(peak, .88)
        quiet = generator.master(array("f", [0]) * 100, array("f", [0]) * 100, .07)
        self.assertEqual(set(quiet), {0})

    def test_cli_listing_selection_paths_report_and_inventory(self):
        with tempfile.TemporaryDirectory(prefix="minecraftc-audio-cli-") as directory:
            root = Path(directory)
            base = root / "audio"
            heaven = root / "custom/official_heaven/audio"
            manifest = heaven.parent / "resources.sha256.json"
            manifest.parent.mkdir(parents=True)
            manifest.write_text('{"version":1,"sha256":{}}')
            calls = []
            original = generator.write_track

            def short_render(path, track):
                calls.append((path, track.name))
                return original(path, replace(track, bpm=960, beats=16))

            with redirect_stdout(io.StringIO()) as output:
                generator.main(["--list", "--output", str(base)])
            self.assertEqual(len(output.getvalue().splitlines()), 8)
            self.assertFalse(base.exists())
            with patch.object(generator, "write_track", side_effect=short_render), redirect_stdout(io.StringIO()):
                generator.main(["--track", "menu_spark", "--track", "heaven_ether",
                                "--track", "menu_spark", "--output", str(base),
                                "--heaven-output", str(heaven), "--report", str(root / "report.json")])
            self.assertEqual(calls, [(base / "menu_spark.wav", "menu_spark"),
                                     (heaven / "heaven_ether.wav", "heaven_ether")])
            report = json.loads((root / "report.json").read_text())
            self.assertEqual(report["generator_version"], 2)
            for path, name in calls:
                entry = next(x for x in report["tracks"] if x["track"] == name)
                self.assertEqual(entry["sha256"], hashlib.sha256(path.read_bytes()).hexdigest())
            inventory = json.loads(manifest.read_text())["sha256"]
            self.assertEqual(inventory, {"audio/heaven_ether.wav": hashlib.sha256((heaven / "heaven_ether.wav").read_bytes()).hexdigest()})
            with patch.object(generator, "write_track", side_effect=short_render), redirect_stdout(io.StringIO()):
                generator.main(["--track", "heaven_ether", "--output", str(root / "other/audio")])
            self.assertTrue((root / "other/plugins/official_heaven/audio/heaven_ether.wav").is_file())
            with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                generator.main(["--track", "missing"])
            with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                generator.main(["--track", "menu_spark", "--output", str(base),
                                "--report", str(base / "menu_spark.wav")])
            with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                generator.main(["--track", "menu_spark", "--output", str(base),
                                "--report", str(base / "menu_whimsy.wav")])
            with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                generator.main(["--track", "heaven_ether", "--heaven-output", str(heaven),
                                "--report", str(manifest)])


class AssetTests(unittest.TestCase):
    def test_complete_regeneration_format_quality_and_hashes(self):
        with tempfile.TemporaryDirectory(prefix="minecraftc-audio-reference-") as directory:
            for track in TRACKS:
                with self.subTest(track=track.name):
                    bpm, beats, digest = EXPECTED[track.name]
                    self.assertEqual((track.bpm, track.beats), (bpm, beats))
                    path = asset_path(track)
                    self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), digest)
                    with wave.open(str(path), "rb") as source:
                        self.assertEqual((source.getnchannels(), source.getsampwidth(), source.getframerate()), (2, 2, 24000))
                        self.assertEqual(source.getnframes(), round(beats * 60 / bpm * 24000))
                        pcm = array("h", source.readframes(source.getnframes()))
                        if sys.byteorder != "little":
                            pcm.byteswap()
                    metrics = generator.analyze(pcm)
                    self.assertTrue(.015 < metrics["rms"] < .24)
                    self.assertLess(metrics["peak_dbfs"], -1)
                    self.assertGreater(metrics["stereo_difference_rms"], .002)
                    self.assertLess(metrics["loop_seam"], .015)
                    self.assertLess(max(abs(x) for x in metrics["dc_offset"]), 1 / 32768)
                    # Entire final PCM, including mastering and FX state, is checked.
                    # A prefix render cannot validate full-track RMS normalization.
                    regenerated = Path(directory) / (track.name + ".wav")
                    report = generator.write_track(regenerated, track)
                    self.assertEqual(regenerated.read_bytes(), path.read_bytes())
                    self.assertEqual(report["sha256"], digest)
                    print(f"Full deterministic regeneration passed: {track.name}", flush=True)


if __name__ == "__main__":
    unittest.main()
