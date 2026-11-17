#!/usr/bin/env python3
"""Keep optional compiler-cache startup failures out of build correctness."""

import os
from pathlib import Path
import shutil
import subprocess


def invoke(executable, option):
    try:
        result = subprocess.run([executable, option], env=os.environ.copy(),
                                capture_output=True, timeout=30, check=False)
        return result.returncode == 0
    except (OSError, subprocess.TimeoutExpired):
        return False


def publish(executable):
    values = {"MINECRAFTC_COMPILER_LAUNCHER": executable, "SCCACHE_PATH": executable,
              "SCCACHE_GHA_ENABLED": os.environ.get("SCCACHE_GHA_ENABLED", ""),
              "SCCACHE_GHA_VERSION": os.environ.get("SCCACHE_GHA_VERSION", "")}
    os.environ.update(values)
    if os.environ.get("GITHUB_ENV"):
        with Path(os.environ["GITHUB_ENV"]).open("a", encoding="utf-8") as stream:
            for key, value in values.items():
                stream.write(f"{key}={value}\n")


def start():
    executable = os.environ.get("SCCACHE_PATH") or shutil.which("sccache")
    if executable:
        if invoke(executable, "--start-server"):
            publish(executable)
            print("Compiler cache enabled", flush=True)
            return "enabled"
        # Do not echo backend error bodies: they can contain signed storage URLs.
        print("::warning::Compiler cache startup failed; trying local storage", flush=True)
        invoke(executable, "--stop-server")
        # In sccache 0.19, a nonempty VERSION enables GHA even when ENABLED=false.
        os.environ.update(SCCACHE_GHA_ENABLED="false", SCCACHE_GHA_VERSION="")
        if invoke(executable, "--start-server"):
            publish(executable)
            print("Compiler cache using local storage", flush=True)
            return "local"
    os.environ.update(SCCACHE_GHA_ENABLED="false", SCCACHE_GHA_VERSION="")
    publish("")
    print("::warning::Compiler cache unavailable; using the compiler directly", flush=True)
    return "disabled"


if __name__ == "__main__":
    start()
