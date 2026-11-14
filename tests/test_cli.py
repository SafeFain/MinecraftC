#!/usr/bin/env python3
"""Check process status and output together; CTest regexes ignore status."""
from pathlib import Path
import subprocess
import sys

def require(condition, message):
    if not condition:
        raise SystemExit(f"FAILED: {message}")

binary, case, version, configured_version = sys.argv[1:]
argument = {"version": "--version", "help": "--help", "opengl": "--renderer=opengl"}[case]
result = subprocess.run([binary, argument], capture_output=True, text=True, timeout=8)
require(result.returncode == (1 if case == "opengl" else 0), f"unexpected exit status: {result}")
if case == "version":
    source_version = (Path(__file__).resolve().parents[1] / "VERSION").read_text(encoding="utf-8").strip()
    require(configured_version == source_version,
            f"CMake version {configured_version!r} is stale; VERSION is {source_version!r}")
    core, channel = source_version.split("-", 1)
    stage, separator, iteration = channel.partition(".")
    label = {"alpha": "Alpha", "beta": "Beta", "rc": "RC", "release": "Release"}[stage]
    expected_display = f"{label}{separator}{iteration}-{core}"
    require(version == expected_display, f"CMake display version disagrees with VERSION: {version!r}")
    require(result.stdout.strip() == f"MinecraftC {version}", f"wrong version: {result}")
    require(not result.stderr, f"unexpected error output: {result}")
elif case == "help":
    require("--renderer=vulkan|vulkan-demo|vulkan-textured-demo" in result.stdout, f"wrong renderer help: {result}")
    require(not result.stderr, f"unexpected error output: {result}")
else:
    require("Unknown renderer: opengl" in result.stderr, f"wrong rejection: {result}")
    require(not result.stdout, f"unexpected normal output: {result}")
