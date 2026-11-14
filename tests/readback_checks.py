"""Compare isolated material consumers with an independently rendered default.

Use a small channel tolerance to ignore readback rounding, and require changes on
at least 1% of the visible consumer. A single stray pixel cannot prove an override.
"""
from pathlib import Path


def read_ppm(path):
    magic, size, maximum, pixels = Path(path).read_bytes().split(b"\n", 3)
    width, height = map(int, size.split())
    if magic != b"P6" or maximum != b"255" or len(pixels) != width * height * 3:
        raise ValueError(f"invalid RGB capture: {path}")
    return (width, height), pixels


def changed_pixels(a, b):
    return sum(any(abs(a[i + c] - b[i + c]) > 4 for c in range(3))
               for i in range(0, len(a), 3))


def compare_natural(directory):
    directory = Path(directory)
    size, background = read_ppm(directory / "default-background.ppm")
    other_size, other_background = read_ppm(directory / "natural-background.ppm")
    if size != other_size or changed_pixels(background, other_background):
        raise ValueError("control background changed between default and Natural runs")
    for consumer in ("opaque", "translucent", "dropped", "held", "ui"):
        default_size, default = read_ppm(directory / f"default-{consumer}.ppm")
        natural_size, natural = read_ppm(directory / f"natural-{consumer}.ppm")
        if default_size != size or natural_size != size:
            raise ValueError(f"{consumer}: inconsistent capture dimensions")
        visible = changed_pixels(background, default)
        if visible < 16:
            raise ValueError(f"{consumer}: default control did not draw a visible consumer")
        changed = changed_pixels(default, natural)
        required = max(16, visible // 100)
        if changed < required:
            raise ValueError(f"{consumer}: Natural override missing; "
                             f"changed {changed} pixels, need {required} of {visible} visible pixels")
        print(f"Natural/default {consumer}: {changed}/{visible} visible pixels changed")
