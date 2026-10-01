# Voxel GI Vulkan regression smoke

`vulkan_gi_smoke` is an explicit display-dependent test, excluded from normal
builds and CTest. It renders Very High and Ultra using the actual production
GI shaders and 4× scene MSAA, and reads the rendered half-resolution GI/AO image. An additional compute probe
uses the exact `voxel_gi_trace.glsl` production traversal/filtering functions.

It checks:

- Finite, nonzero indirect RGB and visible AO.
- Unchanged quality, unrelated Bloom strength and Bloom resource rebuilds retain
  the GI volume cache.
- Each X/Y/Z one-cell move uploads 128 KiB (32 KiB attributes + 96 KiB auxiliary data) and injects 4,096 voxels, with rendered
  output matching a forced full-volume upload and injection within `1e-5` per channel.
- Diagonal movement uses disjoint partial regions and also matches full upload/injection.
- Stability adjustment preserves the volume; strength, block edits and ambient
  lighting changes reject stale history across swapchain images.
- Ambient changes inject the entire volume without redundant attribute uploads.
- Scene/distance changes converge, and zero strength disables GI.
- Three axis-aligned and oblique one-block walls reject emitted RGB; resolvable
  apertures transmit it at every valid hierarchy level. Diagnostic uniforms can
  move finer windows away to exercise missing-fine-coverage fallback.
- Torch/crystal chromaticity follows the atlas and remains separate from reflection.
- Isolated face exposure responds to light direction and adjacent-chunk edits.
- Footprints remain continuous around 2/4-block scale boundaries; edited auxiliary
  regions agree with complete uploads/injection within `1e-5`.

The optional second argument is an evidence directory. It saves two PPM images
of the half-resolution GI diagnostic (square-root display mapping, not the final
HDR scene) and CSV records of actual shader ray/filter results. For example:

```bash
"$project_root/build-local/vulkan_gi_smoke" "$project_root/assets" "$gi_smoke_dir/evidence"
```

Each clipmap has a 24-byte auxiliary record per cell, in a 6 MiB storage buffer
at 64³. Added GPU logical payload is 18 MiB for Very High and 24 MiB for Ultra;
combined GI volume payload is 30/40 MiB. These figures exclude allocation alignment,
CPU snapshots, staging and screen attachments. The expanded coarse CPU cache adds
324/328.5 KiB per accepted source chunk at the default Very High/Ultra distances;
accepted source count remains bounded by loaded clipmap-intersecting chunks.

Occupancy is conservative inside each of up to 4³ subcells. Default Ultra's
outermost subcells are two blocks wide; smaller holes can remain closed. Partial
opacity still uses the containing cell's conservative maximum. Direction light
visibility uses existing sky light, not a new directional shadow ray. World
block light remains scalar; no second bounce or radiance feedback is introduced.

Vulkan calls and readback are confined to the backend's `VulkanGiSmokeProbe.h`.
The probe is only used by this explicit test target; gameplay does not read back
images or force full updates. The ordinary GI regression also checks disjoint
multi-axis regions, 32/48/64 resolutions, packing order and wall-time history
retention across 30/60/120 FPS and one/two/three history reuse intervals.

## Build

```bash
cmake -S . -B build-local -DCMAKE_BUILD_TYPE=Release
cmake --build build-local --target vulkan_gi_smoke -j2
```

Use an absolute asset path and a separate launch directory to isolate logs.
No GameSession or player save is created.

```bash
project_root="$PWD"
gi_smoke_dir="$(mktemp -d /tmp/minecraftc-gi-smoke.XXXXXX)"
cd "$gi_smoke_dir"
timeout 300s "$project_root/build-local/vulkan_gi_smoke" "$project_root/assets"
```

## Headless Linux

With Xvfb, xauth and Mesa lavapipe installed, select the local ICD filename
(this example matches the current development environment):

```bash
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json SDL_VIDEODRIVER=x11 \
  timeout 300s xvfb-run -a "$project_root/build-local/vulkan_gi_smoke" \
  "$project_root/assets"
```

Success requires exit code zero and `Voxel GI Vulkan smoke passed`, preceded by
both tier summaries. A startup marker alone, an initialization failure, or a
timeout is not success. A restricted sandbox may prevent X11 connections; run
with authorized display access in that case.

This is functional software-GPU regression coverage, not hardware performance
measurement or manual visual acceptance. The final depth/normal-guided post
upsampler executes during these frames, but its silhouette appearance is not
asserted by this GI/AO readback. Windows/macOS, mobile hardware, validation
layers and final-scene edge-focused visual acceptance are separate checks.
