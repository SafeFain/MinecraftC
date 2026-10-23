#!/usr/bin/env python3
"""Run repeatable native model benchmarks, preserving raw logs and JSON metrics."""
import argparse
import json
import pathlib
import statistics
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--frames", type=int, default=1800)
    parser.add_argument("--parts", type=int, nargs="+", choices=(1, 4), default=[1, 4])
    parser.add_argument("--counts", type=int, nargs="+", choices=(128, 512), default=[128, 512])
    parser.add_argument("--reverse-order", action="store_true",
                        help="record instancing first in each pair to diagnose run-order bias")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    rows = []
    for parts in args.parts:
        for count in args.counts:
            for repeat in range(args.repeats):
                for batching in ((True, False) if args.reverse_order else (False, True)):
                    command = [str(args.executable.resolve()), "--renderer=vulkan-demo",
                               f"--benchmark-frames={args.frames}", f"--benchmark-models={count}",
                               f"--benchmark-model-parts={parts}", "--benchmark-model-only"]
                    if not batching:
                        command.append("--benchmark-single-instance")
                    log = args.output / f"models{count}-parts{parts}-batch{int(batching)}-run{repeat+1}.log"
                    with log.open("w", encoding="utf-8") as stream:
                        subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, check=True)
                    text = log.read_text(encoding="utf-8", errors="replace")
                    line = next(line for line in text.splitlines() if line.startswith("MINECRAFTC_BENCHMARK "))
                    row = {key: float(value) for key, value in (field.split("=", 1) for field in line.split()[1:])}
                    row["repeat"] = repeat+1
                    row["device"] = next((line.split("Vulkan device: ", 1)[1] for line in text.splitlines() if "Vulkan device: " in line), "unrecorded")
                    rows.append(row)
                    (args.output / "results.json").write_text(json.dumps(rows, indent=2)+"\n", encoding="utf-8")
                    print(line, flush=True)
    regressions = []
    for parts in args.parts:
        for count in args.counts:
            groups = [[row for row in rows if row["instances"] == count and row["parts"] == parts and row["batching"] == mode] for mode in (0, 1)]
            reference, batched = [{key: statistics.median(row[key] for row in group) for key in ("p50_ms", "p95_ms", "animation_ms", "queue_ms", "model_draws", "palette_bytes")} for group in groups]
            if batched["model_draws"] > reference["model_draws"] * .1:
                raise RuntimeError(f"batch reduction failed for {count}/{parts}")
            if any(batched[key] > reference[key] * 1.05 for key in ("p50_ms", "p95_ms")):
                regressions.append({"instances": count, "parts": parts, "reference": reference, "batched": batched})
    (args.output / "regressions.json").write_text(json.dumps(regressions, indent=2)+"\n", encoding="utf-8")
    if regressions:
        raise RuntimeError("repeat-median frame regression exceeds 5%; inspect regressions.json")


if __name__ == "__main__":
    main()
