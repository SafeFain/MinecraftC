# GI quality and performance validation

Very High and Ultra use colored voxel bounce with independent reflected/emissive
scales (2.4 and 2.0). Exposure and the serialized strength slider retain their
existing meaning. Valid stationary receivers refresh alternate 8×8 pixel tiles on each
reuse of their own swapchain image. Moving/disoccluded receivers trace immediately;
zero temporal stability disables reuse. Conservative hierarchy skips require
converged source data. Filter fetches are shared, but each tap retains its own
visibility check.

Source copies are capped at four chunks/frame. Coarse occupancy/exposure work is
resumable and capped at 98,304 source-cell visits/frame; incomplete or cancelled
revisions are never published. Source order remains near-to-far. Small environmental
changes refresh one level/frame; large changes invalidate history and relight all
levels immediately. Unrelated settings retain the cache.

## Reproduce on the target GPU

Build from the repository root:

```bash
cmake -S . -B build-local -DCMAKE_BUILD_TYPE=Release
cmake --build build-local --target vulkan_gi_benchmark vulkan_gi_smoke -j2
./build-local/vulkan_gi_smoke assets /tmp/gi-smoke
./build-local/vulkan_gi_benchmark assets /tmp/gi-optimized
./build-local/vulkan_gi_benchmark assets /tmp/gi-reference --reference
```

Defaults are **2560×1440**, 120 warmup frames and **600 measured frames** per case.
Run the two commands consecutively with other GPU workloads closed. Target machine:
RTX 5070, Ryzen 9700X, 32 GB DDR5. Neither renderer demo invokes the gameplay FPS
limiter; the window requests immediate presentation. If unavailable, the renderer
logs that presentation may still constrain CPU frame time.

`--reference` disables checkerboard reuse and conservative hierarchical skipping,
keeping the enhanced gains, traversal safety limits and other settings equal. It
isolates these optimizations, rather than claiming to recreate a previous binary.
For a historical comparison, build the earlier commit separately with its matching
shader assets. Never mix old binaries with new uniform layouts/shaders.

Use `--scene colored-room|cave-entry|thin-wall` and
`--tier high|very-high-off|very-high|ultra` to select a case; both default to `all`.
`--width`, `--height`, `--frames` and `--warmup` allow diagnostic runs. Fewer than
600 frames, software Vulkan, and smaller resolutions do not establish target-GPU
acceptance. A warmup that leaves pending source/plane work fails explicitly.

## Evidence and interpretation

- `summary.csv`: CPU frame and GPU frame/screen/injection/composite median, P95,
  P99. A missing GPU series is **-1**, never a measured zero.
- `*-frames.csv`: CPU cache/coarse/packing/wait phases, delayed GPU submission IDs,
  upload bytes and pending work. GPU queries use existing fences without an added
  per-frame wait. Their rows refer to an earlier submission, not the CPU row's
  frame; use submission IDs when investigating spikes.
- `*-metadata.txt`: actual GPU/driver, image count, resolution, warmup and mode.
- `*.ppm`: complete postprocessed Vulkan screenshots, captured before presentation
  after measurement. All comparisons use exposure 1.0.
- `*-lighting.csv`: mean linear GI RGB, peak luminance and lit receiver counts.

The three fixed camera positions exercise colored walls and two differently colored
sources, an enclosed room/cave entrance, and an emitter hidden behind a one-block
wall. Compare Very High against **very-high-off** to isolate GI. Comparing High
against Very High also includes differences in AO, reflections and other preset
budgets. Keep wall and emitter hues, avoid clipped highlights, silhouettes/black
halos, persistent checker patterns and stale light after edits.

The target is at least **40% less median steady-state GI screen+injection GPU time**
against the full-trace reference, with lower total-frame P95/P99. This is an
acceptance target, not a claim that every scene or GPU has met it. Hardware tests
are required before reporting an RTX 5070 gain. Real startup, GPU ray/pixel tests
and CTest establish correctness independently of this performance target.

## Local diagnostic result (2026-10-03)

Sequential 600-frame runs on **llvmpipe / LLVM 21.1.8 at 256×144** measured:

| GPU phase | Full-trace reference | Optimized | Reduction |
|---|---:|---:|---:|
| GI screen median | 111.411 ms | 73.3275 ms | 34.18% |
| Total frame median | 127.239 ms | 89.016 ms | 30.04% |
| Total frame P95 | 144.026 ms | 104.269 ms | 27.60% |
| Total frame P99 | 156.214 ms | 114.189 ms | 26.90% |

This software result is below the 40% target and does not establish RTX 5070 /
1440p performance. The pixel-checker version reduced the same software GI median
by only 18.6%; coherent 8×8 tile refresh was retained after the comparison.
CPU frame values include ordinary fence/presentation waits; the separate cache,
coarse and packing columns identify CPU work.
