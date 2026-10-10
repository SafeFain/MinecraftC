#!/usr/bin/env python3
"""Opt-in GPU checks: skip only a provably absent display; scene errors fail."""
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
binary, assets, scene = sys.argv[1:]
command = [binary, assets]
if sys.platform.startswith("linux") and (os.environ.get("MINECRAFTC_TEST_XVFB") == "1" or
        not (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY"))):
    if not shutil.which("xvfb-run"):
        print("SKIPPED: no display and xvfb-run unavailable")
        sys.exit(77)
    command = ["xvfb-run", "-a", *command]
environment = os.environ.copy()
if command[0] == "xvfb-run":
    environment["SDL_VIDEODRIVER"] = "x11"
    environment.pop("WAYLAND_DISPLAY", None)
# Enable validation when installed. Every validation error is a test failure.
if Path("/usr/share/vulkan/explicit_layer.d/VkLayer_khronos_validation.json").is_file():
    environment["VK_INSTANCE_LAYERS"] = "VK_LAYER_KHRONOS_validation"
with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                      text=True, env=environment, start_new_session=os.name == "posix") as process:
    try:
        stdout, stderr = process.communicate(timeout=220)
    except subprocess.TimeoutExpired:
        # xvfb-run owns children; terminate the entire isolated group on timeout.
        if os.name == "posix":
            os.killpg(process.pid, signal.SIGKILL)
        else:
            process.kill()
        stdout, stderr = process.communicate()
        print(stdout, end="")
        print(stderr, end="", file=sys.stderr)
        raise SystemExit(f"FAILED: Vulkan {scene} exceeded 220 seconds")
print(stdout, end="")
print(stderr, end="", file=sys.stderr)
marker = {"gi": "Voxel GI Vulkan smoke passed", "model_fog": "PASS Vulkan model fog:"}[scene]
if process.returncode != 0:
    raise SystemExit(f"FAILED: Vulkan {scene} exited {process.returncode}")
if marker not in stdout:
    raise SystemExit(f"FAILED: Missing successful {scene} readback result")
if any(error in stdout + stderr for error in ("VUID-", "Validation Error")):
    raise SystemExit("FAILED: Vulkan validation error")
