# Voxel GI Vulkan regression smoke

`vulkan_gi_smoke` is an explicit display-dependent test, excluded from normal
builds and CTest. It renders Very High and Ultra using the actual production
GI shaders and 4× scene MSAA, and reads the rendered half-resolution GI/AO image.

It checks:

- Finite, nonzero indirect RGB and visible AO.
- Unchanged quality, unrelated Bloom strength and Bloom resource rebuilds retain
  the GI volume cache.
- Each X/Y/Z one-cell move uploads 32 KiB and injects 4,096 voxels, with rendered
  output matching a forced full-volume upload and injection within `1e-5` per channel.
- Diagonal movement uses disjoint partial regions and also matches full upload/injection.
- Stability adjustment preserves the volume; strength, block edits and ambient
  lighting changes reject stale history across swapchain images.
- Ambient changes inject the entire volume without redundant attribute uploads.
- Scene/distance changes converge, and zero strength disables GI.

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
timeout 60s "$project_root/build-local/vulkan_gi_smoke" "$project_root/assets"
```

## Headless Linux

With Xvfb, xauth and Mesa lavapipe installed, select the local ICD filename
(this example matches the current development environment):

```bash
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json SDL_VIDEODRIVER=x11 \
  timeout 60s xvfb-run -a "$project_root/build-local/vulkan_gi_smoke" \
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
layers and edge-focused visual acceptance are separate checks.
