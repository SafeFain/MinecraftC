#!/usr/bin/env python3
"""Reject platform or graphics APIs outside their designated adapters."""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys


RULES = {
    "sdl": re.compile(r"(?:#\s*include\s*[<\"]SDL3/|\bSDL_[A-Za-z0-9_]+)"),
    "opengl": re.compile(
        r"(?:#\s*include\s*[<\"]glad/|\bgl[A-Z][A-Za-z0-9_]*\s*\(|"
        r"\bGL(?:uint|int|enum|sizei|boolean|sizeiptr|intptr)\b|\bGL_[A-Z0-9_]+)"
    ),
    "vulkan": re.compile(
        r"(?:#\s*include\s*[<\"]vulkan/|\bvk[A-Z][A-Za-z0-9_]*\s*\(|"
        r"\bVk[A-Z][A-Za-z0-9_]*\b|\bVK_[A-Z0-9_]+)"
    ),
    "native": re.compile(
        r"(?:#\s*include\s*[<\"](?:windows\.h|shlobj\.h|unistd\.h|pwd\.h|"
        r"mach-o/dyld\.h)[>\"]|\b(?:GetModuleFileNameW|SHGetKnownFolderPath|"
        r"MoveFileExW|getpwuid_r|readlink)\s*\()"
    ),
}

ALLOWED_PREFIXES = {
    "sdl": ("src/platform/sdl/",),
    "opengl": ("src/renderer/backend/opengl/",),
    "vulkan": (
        "src/renderer/backend/vulkan/",
        "src/platform/sdl/SdlWindow.cpp",
    ),
    "native": ("src/platform/native/",),
}


def source_files(root: pathlib.Path):
    for path in (root / "src").rglob("*"):
        if path.suffix in {".h", ".hpp", ".c", ".cpp", ".mm"}:
            yield path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=pathlib.Path, default=pathlib.Path.cwd())
    parser.add_argument(
        "--baseline", type=pathlib.Path,
        default=pathlib.Path("tools/api_boundary_baseline.json"))
    args = parser.parse_args()
    root = args.root.resolve()
    baseline_path = args.baseline
    if not baseline_path.is_absolute():
        baseline_path = root / baseline_path
    baseline = json.loads(baseline_path.read_text(encoding="utf-8"))
    known = {rule: set(paths) for rule, paths in baseline.items()}
    violations: dict[str, set[str]] = {rule: set() for rule in RULES}

    for path in source_files(root):
        relative = path.relative_to(root).as_posix()
        text = path.read_text(encoding="utf-8", errors="replace")
        for rule, pattern in RULES.items():
            if relative.startswith(ALLOWED_PREFIXES[rule]):
                continue
            if pattern.search(text):
                violations[rule].add(relative)

    errors: list[str] = []
    for rule in RULES:
        additions = sorted(violations[rule] - known.get(rule, set()))
        stale = sorted(known.get(rule, set()) - violations[rule])
        if additions:
            errors.append(f"{rule}: new forbidden dependencies: {', '.join(additions)}")
        if stale:
            errors.append(
                f"{rule}: remove migrated paths from baseline: {', '.join(stale)}")

    if errors:
        print("API boundary check failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    print("API boundary baseline is unchanged; no new violations")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
