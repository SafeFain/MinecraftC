#!/usr/bin/env python3
"""iOS Actions configuration reuse, timings and simulator smoke checks."""

import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time


def run(args, **kwargs):
    return subprocess.run(args, check=True, **kwargs)


def output(args):
    return subprocess.check_output(args, text=True).strip()


def note(stage, value):
    print(f"{stage}: {value}", flush=True)
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as stream:
            stream.write(f"- {stage}: {value}\n")


def timed(stage, callback):
    start = time.monotonic()
    try:
        return callback()
    finally:
        note(stage, f"{time.monotonic() - start:.1f}s")


def fingerprint(root):
    # Cache files contain absolute paths and compiler/SDK probe results.
    facts = {key: os.environ.get(key, "") for key in
             ("ImageOS", "ImageVersion", "RUNNER_ARCH", "SCCACHE_PATH",
              "MINECRAFTC_MOLTENVK_ROOT")}
    facts["workspace"] = str(root.resolve())
    if not facts["ImageVersion"]:
        raise RuntimeError("Runner ImageVersion is required for configuration cache safety")
    for command in (["xcodebuild", "-version"], ["xcode-select", "-p"],
                    ["xcrun", "clang", "--version"], ["cmake", "--version"]):
        facts[" ".join(command)] = output(command)
    for sdk in ("iphoneos", "iphonesimulator"):
        for flag in ("--show-sdk-path", "--show-sdk-version", "--show-sdk-build-version"):
            facts[sdk + flag] = output(["xcrun", "--sdk", sdk, flag])
    digest = hashlib.sha256(json.dumps(facts, sort_keys=True).encode())
    paths = output(["git", "ls-files", "-z"]).split("\0")
    for name in sorted(paths):
        path = root / name
        if (path.name == "CMakeLists.txt" or path.suffix == ".cmake" or
                name.startswith(("tools/", ".github/workflows/", "ios/", "sdk/"))):
            digest.update(name.encode())
            digest.update(path.read_bytes())
    return "ios-config-v1-" + digest.hexdigest()


def configure(lane, root):
    if lane not in ("simulator", "device"):
        raise ValueError("Unknown iOS lane")
    build = root / ("build-ios-" + lane)
    args = ["cmake", "-S", str(root), "-B", str(build), "-G", "Xcode",
            "-DCMAKE_SYSTEM_NAME=iOS", "-DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON",
            "-DCMAKE_XCODE_ATTRIBUTE_C_COMPILER_LAUNCHER=" + os.environ["SCCACHE_PATH"],
            "-DCMAKE_XCODE_ATTRIBUTE_CLANG_ENABLE_MODULES=NO",
            "-DCMAKE_XCODE_ATTRIBUTE_COMPILER_INDEX_STORE_ENABLE=NO",
            "-DCMAKE_XCODE_ATTRIBUTE_CLANG_USE_RESPONSE_FILE=NO",
            "-DCMAKE_OSX_SYSROOT=" + ("iphoneos" if lane == "device" else "iphonesimulator"),
            "-DCMAKE_OSX_ARCHITECTURES=arm64", "-DCMAKE_OSX_DEPLOYMENT_TARGET=14.0",
            "-DMINECRAFTC_FETCH_DEPENDENCIES=ON",
            "-DMINECRAFTC_MOLTENVK_ROOT=" + os.environ["MINECRAFTC_MOLTENVK_ROOT"]]
    if lane == "device":
        for dep in ("glm", "sdl3"):
            source = root / "build-ios-simulator" / "_deps" / (dep + "-src")
            if not (source / "CMakeLists.txt").is_file():
                raise RuntimeError(f"Missing pinned dependency source: {source}")
            args.append(f"-DFETCHCONTENT_SOURCE_DIR_{dep.upper()}={source}")
    restored = (build / "CMakeCache.txt").is_file()
    try:
        run(args)
    except subprocess.CalledProcessError:
        if not restored:
            raise
        note(lane, "Restored configuration failed; retrying cold once")
        # Only fixed, direct workspace children are eligible for recursive removal.
        if build.is_symlink() or build.resolve().parent != root.resolve():
            raise RuntimeError("Unsafe build directory")
        shutil.rmtree(build)
        run(args)


def choose_device(devices, runtime, family):
    matches = [d for d in devices.get(runtime, [])
               if d.get("isAvailable") and d["name"].startswith(family)]
    return min(matches, key=lambda d: d["udid"]) if matches else None


def smoke(app):
    sim = ["xcrun", "simctl"]
    runtimes = json.loads(output(sim + ["list", "runtimes", "available", "-j"]))["runtimes"]
    runtime = max((r for r in runtimes if ".iOS-" in r["identifier"] and r["isAvailable"]),
                  key=lambda r: tuple(map(int, r["version"].split("."))))["identifier"]
    types = json.loads(output(sim + ["list", "devicetypes", "-j"]))["devicetypes"]
    for family in ("iPhone", "iPad"):
        devices = json.loads(output(sim + ["list", "devices", "available", "-j"]))["devices"]
        existing = choose_device(devices, runtime, family)
        created = existing is None
        udid = None
        process = None
        log = Path(os.environ["RUNNER_TEMP"]) / f"minecraftc-ios-{family.lower()}.log"
        try:
            if existing:
                udid = existing["udid"]
            else:
                device_type = min((d for d in types if d["name"].startswith(family)),
                                  key=lambda d: d["identifier"])["identifier"]
                udid = timed(family + " create", lambda: output(
                    sim + ["create", "MinecraftC-" + family, device_type, runtime]))
            note(family + " device", f"{udid} ({'created' if created else 'reused'})")
            if not existing or existing["state"] != "Booted":
                timed(family + " boot", lambda: run(sim + ["boot", udid]))
            timed(family + " bootstatus", lambda: run(sim + ["bootstatus", udid, "-b"]))
            # A fresh application container preserves the first-install startup check.
            installed = subprocess.run(sim + ["get_app_container", udid,
                "io.github.SafeFain.MinecraftC", "app"], stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL, check=False)
            if installed.returncode == 0:
                run(sim + ["uninstall", udid, "io.github.SafeFain.MinecraftC"])
            timed(family + " install", lambda: run(sim + ["install", udid, str(app)]))
            def launch():
                nonlocal process
                with log.open("w", encoding="utf-8") as stream:
                    process = subprocess.Popen(sim + ["launch", "--console", udid,
                        "io.github.SafeFain.MinecraftC"], stdout=stream, stderr=subprocess.STDOUT)
                    deadline = time.monotonic() + 300
                    while time.monotonic() < deadline:
                        if "Vulkan first frame presented" in log.read_text(errors="replace"):
                            return
                        if process.poll() is not None:
                            break
                        time.sleep(1)
                raise RuntimeError(f"{family}: no Vulkan first frame within 300s or launch exited")
            timed(family + " launch to first frame", launch)
            contents = log.read_text(errors="replace")
            if ("MinecraftC initialized" not in contents or
                    "Descriptor sets binding resources using Metal argument buffers" in contents):
                raise RuntimeError(f"{family}: initialization or MoltenVK workaround check failed")
        except BaseException:
            if udid:
                with log.open("a", encoding="utf-8") as stream:
                    subprocess.run(sim + ["spawn", udid, "log", "show", "--style", "compact",
                        "--last", "6m", "--predicate", 'process == "MinecraftC"'],
                        stdout=stream, stderr=subprocess.STDOUT, check=False)
            reports = Path.home() / "Library/Logs/DiagnosticReports"
            for report in reports.glob("MinecraftC*"):
                if report.is_file() and time.time() - report.stat().st_mtime < 600:
                    shutil.copy2(report, log.parent / ("minecraftc-ios-crash-" + report.name + ".log"))
            raise
        finally:
            def cleanup():
                if process and process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                if udid:
                    subprocess.run(sim + ["terminate", udid, "io.github.SafeFain.MinecraftC"], check=False)
                    subprocess.run(sim + ["shutdown", udid], check=False)
                    if created:
                        run(sim + ["delete", udid])
            try:
                timed(family + " cleanup", cleanup)
            finally:
                if log.exists():
                    print(log.read_text(errors="replace"), flush=True)


def main():
    command, *args = sys.argv[1:]
    root = Path.cwd()
    if command == "fingerprint":
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as stream:
            stream.write("key=" + fingerprint(root) + "\n")
    elif command == "configure":
        timed(args[0] + " configure", lambda: configure(args[0], root))
    elif command == "smoke":
        timed("Simulator smoke total", lambda: smoke(root / "build-ios-simulator/Release-iphonesimulator/MinecraftC.app"))
    elif command == "run":
        timed(args[0], lambda: run(args[1:]))
    elif command == "cache-start":
        if subprocess.run(["sccache", "--show-stats"], stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL, check=False).returncode:
            run(["sccache", "--start-server"])
        run(["sccache", "--zero-stats"])
    elif command == "cache-stats":
        run(["sccache", "--show-stats"])
        stats = output(["sccache", "--show-stats", "--stats-format=json"])
        (Path(os.environ["RUNNER_TEMP"]) / f"minecraftc-ios-{args[0]}-sccache.json").write_text(stats)
        note(args[0] + " sccache", stats)
    elif command == "summary":
        from datetime import datetime
        jobs = json.loads((Path(os.environ["RUNNER_TEMP"]) / "minecraftc-ios-jobs.json").read_text())
        job = next(j for j in jobs["jobs"] if j["name"] == "ios-arm64")
        for step in job["steps"]:
            if step.get("started_at") and step.get("completed_at"):
                seconds = (datetime.fromisoformat(step["completed_at"].replace("Z", "+00:00")) -
                           datetime.fromisoformat(step["started_at"].replace("Z", "+00:00"))).total_seconds()
                note(step["name"], f"{seconds:.0f}s ({step['conclusion']})")
    else:
        raise ValueError(command)


if __name__ == "__main__":
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(143))
    main()
