#!/usr/bin/env python3
"""Check process status and output together; CTest regexes ignore status."""
import subprocess
import sys

def require(condition, message):
    if not condition:
        raise SystemExit(f"FAILED: {message}")

binary, case, version = sys.argv[1:]
argument = {"version": "--version", "help": "--help", "opengl": "--renderer=opengl"}[case]
result = subprocess.run([binary, argument], capture_output=True, text=True, timeout=8)
require(result.returncode == (1 if case == "opengl" else 0), f"unexpected exit status: {result}")
if case == "version":
    require(result.stdout.strip() == f"MinecraftC {version}", f"wrong version: {result}")
    require(not result.stderr, f"unexpected error output: {result}")
elif case == "help":
    require("--renderer=vulkan|vulkan-demo|vulkan-textured-demo" in result.stdout, f"wrong renderer help: {result}")
    require(not result.stderr, f"unexpected error output: {result}")
else:
    require("Unknown renderer: opengl" in result.stderr, f"wrong rejection: {result}")
    require(not result.stdout, f"unexpected normal output: {result}")
