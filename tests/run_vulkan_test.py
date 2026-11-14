#!/usr/bin/env python3
"""Opt-in GPU checks: skip only a provably absent display; scene errors fail."""
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time
deadline = time.monotonic() + 220
binary, assets, scene, *extra = sys.argv[1:]
command = [binary, assets]
if scene == "plugins":
    if len(extra) != 1:
        raise SystemExit("Plugin smoke requires a native module")
    command += ["--extensions", extra[0]]
elif scene == "capabilities":
    command += ["--capabilities"]
elif scene == "content_package":
    if len(extra) != 1:
        raise SystemExit("Content package smoke requires a published package")
    command += ["--content-package", extra[0]]
elif scene in ("heaven_model", "heaven_off"):
    command += ["--heaven-model" if scene == "heaven_model" else "--heaven-off"]
elif scene == "enchanting":
    command += ["--enchanting"]
elif scene == "redstone":
    command += ["--redstone"]
elif scene == "experience":
    command += ["--experience"]
elif scene == "natural_textures":
    command += ["--natural-textures"]
elif extra:
    raise SystemExit("Unexpected smoke arguments")
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
def run(arguments, expected):
    with subprocess.Popen(arguments, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True, env=environment, start_new_session=os.name == "posix") as process:
        try:
            stdout, stderr = process.communicate(timeout=max(.001, deadline - time.monotonic()))
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
    if process.returncode != 0:
        raise SystemExit(f"FAILED: Vulkan {scene} exited {process.returncode}")
    if expected not in stdout:
        raise SystemExit(f"FAILED: Missing successful {scene} readback result: {expected}")
    if any(error in stdout + stderr for error in ("VUID-", "Validation Error")):
        raise SystemExit("FAILED: Vulkan validation error")

marker = {
    "enchanting": "Three workstations, portrait layout, falling-anvil model and animated equipment Vulkan readback passed",
    "redstone": "Redstone signals, piston motion and slime Vulkan readback passed",
    "experience": "Experience HUD and orb Vulkan readback passed",
    "content_package": "Content package Vulkan material/icon and native interaction readback passed",
    "gi": "Voxel GI Vulkan smoke passed", "model_fog": "PASS Vulkan model fog:",
    "plugins": "Plugin Vulkan shader/post pass, rebuild and resume passed",
    "capabilities": "Plugin sky/cloud, held GLB, colored particles, optional audio mode and Vulkan rebuild/resume passed",
    "heaven_model": "Official Heaven day/night sky and registered color-grade Vulkan readback passed",
    "heaven_off": "Heaven disabled: native UI Vulkan readback passed",
    "natural_textures": "Natural textures Vulkan terrain/layers, cutout/fluid, UI, held/dropped and day/night readback passed",
}[scene]
if scene == "natural_textures":
    default_command = ["--default-textures" if arg == "--natural-textures" else arg for arg in command]
    run(default_command, marker.replace("Natural", "Default", 1))
run(command, marker)
if scene == "natural_textures":
    import tempfile
    from readback_checks import compare_natural
    try:
        compare_natural(tempfile.gettempdir())
    except (OSError, ValueError) as error:
        raise SystemExit(f"FAILED: Natural/default readback: {error}") from error
