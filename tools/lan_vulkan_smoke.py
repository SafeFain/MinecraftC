#!/usr/bin/env python3
"""Run the display-dependent LAN host/client scene with isolated saves and logs."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--width", type=int, default=960)
    parser.add_argument("--height", type=int, default=640)
    args = parser.parse_args()
    if args.output:
        args.output.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="lan-vulkan-", dir=args.output))
    command = [str(args.binary.resolve())]
    scene = [str(args.assets.resolve()), str(root), str(args.width), str(args.height)]
    print(f"Evidence: {root}", flush=True)
    with (root / "host.log").open("w") as host_log, (root / "client.log").open("w") as client_log:
        host = subprocess.Popen(command + ["--host"] + scene, cwd=root,
                                stdout=host_log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 20
            while not (root / "port").exists():
                if host.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError("host initialization failed; see host.log")
                time.sleep(.05)
            # Wait for the port file's writer to close, rather than racing creation.
            while not (root / "port").read_text().strip():
                if host.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError("host did not publish its port")
                time.sleep(.01)
            client = subprocess.run(command + ["--client"] + scene, cwd=root,
                                    stdout=client_log, stderr=subprocess.STDOUT, timeout=90)
            if client.returncode:
                raise RuntimeError("client scene failed; see client.log")
            if host.wait(timeout=20):
                raise RuntimeError("host movement assertion failed; see host.log")
            print("PASS: independent host/client, authoritative movement, complete loading, joined Vulkan capture")
        finally:
            if host.poll() is None:
                host.terminate()
                try:
                    host.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    host.kill()
                    host.wait()


if __name__ == "__main__":
    main()
