#!/usr/bin/env python3
"""Give each CTest invocation its own short, writable temporary directory.

Keep the caller's working directory: tests receive explicit asset/fixture paths.
Failed runs retain their files and print their location for diagnosis. Successful
runs clean up, including tests using legacy fixed names within their private TEMP.
"""
import argparse
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", required=True)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    directory = None
    try:
        if not command:
            raise ValueError("missing test command")
        args.root.mkdir(parents=True, exist_ok=True)
        directory = Path(tempfile.mkdtemp(prefix="", dir=args.root.resolve()))
        environment = os.environ.copy()
        environment.update(TEMP=str(directory), TMP=str(directory), TMPDIR=str(directory))
        result = subprocess.run(command, env=environment)
        if result.returncode in (0, 77):
            shutil.rmtree(directory)
            return result.returncode
        print(f"FAILED [{args.name}]: exit {result.returncode} "
              f"(0x{result.returncode & 0xffffffff:08x}); files: {directory}", file=sys.stderr)
        return 1
    except (OSError, ValueError) as error:
        print(f"INFRASTRUCTURE [{args.name}]: {error}; files: {directory}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
