"""Original MinecraftC themes and deterministic event arrangements."""

from __future__ import annotations

from dataclasses import dataclass

MENU_CHORDS = (
    (60, 64, 67, 71), (57, 60, 64, 67), (65, 69, 72, 76), (67, 71, 74, 79),
    (60, 64, 67, 74), (64, 67, 71, 76), (65, 69, 72, 76), (67, 72, 74, 79),
)

MENU_MELODY = (
    76, 79, 83, 81, 79, 76, 74, 76, 81, 79, 76, 72, 74, 76, 79, 83,
    84, 83, 79, 76, 77, 81, 79, 76, 74, 76, 79, 81, 79, 76, 74, 72,
)

ARP = (0, 1, 2, 1, 3, 2, 1, 2)

GAME_CHORDS = (
    (48, 55, 60, 64), (45, 52, 57, 60), (41, 48, 53, 57), (43, 50, 55, 60),
    (48, 55, 59, 64), (40, 47, 52, 55), (41, 48, 53, 57), (43, 50, 55, 59),
)

GAME_MELODY = (72, -1, 67, 69, 64, -1, 67, 72, 71, -1, 67, 64, 65, -1, 64, 62)

SPARK_CHORDS = (
    (62, 66, 69, 73), (59, 62, 66, 69), (55, 59, 62, 66), (57, 61, 64, 69),
    (62, 66, 69, 74), (54, 59, 62, 66), (55, 59, 62, 67), (57, 61, 64, 69),
)

SPARK_MELODY = (
    78, 81, 85, 83, 81, 78, 76, 73, 74, 78, 81, 86, 85, 81, 78, 76,
    78, 83, 86, 90, 88, 85, 81, 78, 79, 83, 81, 78, 76, 74, 73, 76,
)

SPARK_ARP = (0, 2, 1, 3, 2, 1, 3, 1)

HEAVEN_CHORDS = (
    (50, 57, 62, 66, 69), (47, 54, 59, 62, 66),
    (43, 50, 55, 59, 62), (45, 52, 57, 61, 64),
)

HEAVEN_MELODY = (81, 86, 83, 78, 79, 83, 88, 85)

SOLITUDE_CHORDS = (
    (45, 52, 57, 60), (41, 48, 52, 57), (38, 45, 50, 53), (43, 50, 55, 59),
    (45, 52, 56, 60), (40, 47, 52, 55), (41, 48, 53, 57), (43, 50, 54, 59),
)

SOLITUDE_NOTES = (69, -1, 64, 67, -1, 60, 62, -1, 71, 67, -1, 64, 65, -1, 62, 59)

HORIZON_CHORDS = (
    (43, 50, 55), (46, 53, 58), (41, 48, 53), (48, 55, 60),
    (45, 52, 57), (38, 45, 50), (43, 50, 54), (36, 43, 48),
)

HORIZON_CALLS = (67, 70, -1, 65, 72, -1, 69, 67, 62, -1, 65, 60, -1, 64, 62, -1)

NIGHTFALL_CHORDS = (
    (40, 47, 52, 59), (43, 50, 55, 62), (38, 45, 50, 57), (45, 52, 57, 64),
)

NIGHTFALL_LIGHTS = (76, 71, 79, -1, 74, 69, 77, -1)

SANCTUM_CHORDS = (
    (36, 48, 55, 60, 64), (41, 53, 57, 60, 65),
    (43, 55, 59, 62, 67), (38, 50, 57, 62, 66),
)

SANCTUM_CHIMES = (84, 79, 88, 83, 86, 81, 91, 86)


@dataclass(frozen=True)
class Track:
    name: str
    bpm: float
    beats: int
    seed: int
    chords: tuple
    melody: tuple
    bed: str
    lead: str
    rms: float
    room: float
    decay: float
    delay: float

    @property
    def heaven(self) -> bool:
        return self.name.startswith("heaven_")


TRACKS = (
    Track("menu_whimsy", 104, 96, 101, MENU_CHORDS, MENU_MELODY,
          "pluck", "bell", .075, .17, 1.7, .065),
    Track("menu_spark", 112, 96, 211, SPARK_CHORDS, SPARK_MELODY,
          "pluck", "bell", .072, .19, 1.9, .085),
    Track("gameplay_calm", 64, 128, 307, GAME_CHORDS, GAME_MELODY,
          "pad", "piano", .065, .23, 2.5, .035),
    Track("overworld_solitude", 60, 96, 401, SOLITUDE_CHORDS, SOLITUDE_NOTES,
          "pad", "piano", .052, .13, 1.5, .015),
    Track("overworld_horizon", 72, 128, 503, HORIZON_CHORDS, HORIZON_CALLS,
          "bowed", "bowed", .058, .29, 3.1, .07),
    Track("overworld_nightfall", 54, 96, 601, NIGHTFALL_CHORDS, NIGHTFALL_LIGHTS,
          "pad", "bell", .048, .25, 2.9, .055),
    Track("heaven_ether", 56, 96, 701, HEAVEN_CHORDS, HEAVEN_MELODY,
          "pad", "bell", .059, .31, 3.4, .085),
    Track("heaven_sanctum", 48, 96, 809, SANCTUM_CHORDS, SANCTUM_CHIMES,
          "organ", "bell", .063, .30, 3.6, .045),
)


@dataclass(frozen=True)
class Event:
    beat: float
    duration: float
    midi: int
    voice: str
    velocity: float
    pan: float


def variation(seed: int, index: int) -> float:
    """Coordinate hash: independent of RNG implementation and render order."""
    value = (seed ^ ((index + 1) * 0x9E3779B9)) & 0xFFFFFFFF
    value = ((value ^ (value >> 16)) * 0x7FEB352D) & 0xFFFFFFFF
    value = ((value ^ (value >> 15)) * 0x846CA68B) & 0xFFFFFFFF
    return (value ^ (value >> 16)) / 0xFFFFFFFF


def voice_lead(chord: tuple, previous: tuple) -> tuple:
    """Preserve the bass; move upper voices to the nearest chord-tone octave."""
    if not previous:
        return chord
    result = [chord[0]]
    for index, midi in enumerate(chord[1:], 1):
        target = previous[min(index, len(previous) - 1)]
        candidates = [midi + octave for octave in (-12, 0, 12)
                      if chord[0] + 5 <= midi + octave <= 84]
        result.append(min(candidates, key=lambda n: (abs(n - target), n)))
    return tuple(result)


def compose(track: Track) -> tuple[Event, ...]:
    """Six/eight four-bar phrases: introduction, theme, contrast, varied return."""
    events = []
    menu = track.name.startswith("menu_")
    solitude = track.name == "overworld_solitude"
    night = track.name == "overworld_nightfall"
    horizon = track.name == "overworld_horizon"
    sanctum = track.name == "heaven_sanctum"
    ether = track.name == "heaven_ether"
    phrases = track.beats // 16

    def add(beat, duration, midi, voice, velocity, pan=0.0):
        if midi < 0:
            return
        identity = len(events)
        jitter = (variation(track.seed, identity * 3) - .5) * (.022 if menu else .035)
        dynamic = .90 + .20 * variation(track.seed, identity * 3 + 1)
        spread = (variation(track.seed, identity * 3 + 2) - .5) * .08
        events.append(Event((beat + jitter) % track.beats, duration, midi, voice,
                            velocity * dynamic, max(-.7, min(.7, pan + spread))))

    previous = ()
    for phrase in range(phrases):
        start = phrase * 16
        intro = phrase == 0
        ending = phrase == phrases - 1
        contrast = phrase == phrases // 2
        returning = phrase > phrases // 2
        density = .68 if intro else .76 if ending else 1.0
        # Contrast revoices the progression instead of repeating the old loop.
        for half in range(2):
            beat = start + half * 8
            chord_index = (phrase * 2 + half + (2 if contrast else 0)) % len(track.chords)
            chord = voice_lead(track.chords[chord_index], previous)
            previous = chord
            for index, midi in enumerate(chord):
                if solitude and index not in (0, 2):
                    continue
                pan = 0 if index == 0 else (-.38 if index % 2 else .38)
                voice = "piano" if menu and index == 0 else track.bed
                level = (.038 if index == 0 else .025) * density
                if menu:
                    if index == 0:
                        add(beat, 5.0, midi - 12, voice, .075 * density)
                    continue
                add(beat - .35, 9.5 if not solitude else 7.5, midi,
                    voice, level, pan)
            if not menu and not solitude:
                add(beat + 1, 8.5, chord[-1] + 12, "air",
                    (.009 if night else .013) * density, -.48 if half else .48)
            if menu:
                pattern = SPARK_ARP if track.name == "menu_spark" else ARP
                for index in range(16):
                    if (intro or ending) and index % 4 == 3:
                        continue
                    if contrast and index % 2:
                        continue
                    midi = chord[pattern[(index + (2 if returning else 0)) % 8]] + 12
                    add(beat + index * .5, .95, midi, "pluck",
                        (.046 if index % 2 else .057) * density,
                        (-.44, .12, .44, -.12)[index % 4])
                if track.name == "menu_spark" and not (intro or ending):
                    for index in (3, 7, 11, 15):
                        add(beat + index * .5 + .25, .65, chord[(index // 4) % 4] + 24,
                            "bell", .023, .45 if half else -.45)
        step = 2 if menu else 4 if (solitude or track.name == "gameplay_calm") else 8
        for index in range(16 // step):
            beat = start + index * step
            if intro and index < (1 if menu else 2):
                continue
            source = (phrase * (16 // step) + index) % len(track.melody)
            midi = track.melody[source]
            if midi < 0 or (ending and index == 16 // step - 1):
                continue
            if contrast:
                midi -= 12 if menu or ether or sanctum else 0
            length = step * (.76 if menu else .85)
            voice = "piano" if contrast and menu else track.lead
            add(beat + (.5 if horizon else 0), length, midi, voice,
                (.105 if solitude else .085 if menu else .074) * density,
                -.12 if phrase % 2 else .16)
            if returning and index % 2 == 0 and not night:
                add(beat + step * .65, step * .55, midi - (12 if menu else 7),
                    "piano" if not sanctum else "organ", .027 * density,
                    .35 if phrase % 2 else -.35)
        if not intro and not ending and not solitude:
            chord = track.chords[(phrase * 2 + 1) % len(track.chords)]
            add(start + 13, 3.8, chord[-1] + (12 if menu or ether else 0),
                "bell" if ether or sanctum else "piano", .032, -.32)
    return tuple(sorted(events, key=lambda event: (event.beat, event.midi, event.voice)))
