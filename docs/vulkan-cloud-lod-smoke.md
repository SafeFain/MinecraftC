# Reproducing the Cloud LOD Vulkan Smoke

The persistent scene in `tests/VulkanCloudLodSmoke.cpp` submits actual Vulkan
sky, near/far cloud and terrain passes for 24 frames. It covers cloud-layer
heights below/inside/above, negative sampling coordinates, a moving sampling
window, 192/1024-block exact-cloud settings and alternating terrain LOD presence.
It requires the expected draw passes and bounded nonempty cloud instances.
This is startup/path validation; it does not assert pixel appearance or hardware performance.

## Build

From the repository root, using the project's regular desktop dependencies:

```bash
cmake -S . -B build-local -DCMAKE_BUILD_TYPE=Release
cmake --build build-local --target vulkan_cloud_lod_smoke -j2
```

BUILD_TESTING must be enabled. This explicit EXCLUDE_FROM_ALL target is not part
of ordinary builds/CTest, which remain usable without a display. It reuses the
project platform/Vulkan libraries; it does not parse generated linker commands.
Use the corresponding configuration/executable directory for multi-config generators.

## Run on a Linux display

Pass an absolute asset path. Run from a fresh directory to isolate diagnostics
and avoid the legacy launch-directory saves convention:

```bash
project_root="$PWD"
smoke_dir="$(mktemp -d /tmp/minecraftc-cloud-smoke.XXXXXX)"
cd "$smoke_dir"
timeout 45s "$project_root/build-local/vulkan_cloud_lod_smoke" "$project_root/assets" \
  > smoke.log 2>&1
smoke_status=$?
cat smoke.log
test "$smoke_status" -eq 0
```

The scene itself creates no GameSession or player saves. Failure to initialize
SDL/Vulkan is a failed run; do not interpret timeout or an initialization marker
alone as this scene's success.

## Headless Linux with Mesa llvmpipe

Requires Xvfb, xauth, Mesa's lavapipe ICD and the same build above. From the
repository root, first locate the ICD (`lvp_icd.json` varies by distribution),
then select its actual path. For this environment:

```bash
project_root="$PWD"
smoke_dir="$(mktemp -d /tmp/minecraftc-cloud-smoke.XXXXXX)"
cd "$smoke_dir"
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json SDL_VIDEODRIVER=x11 \
  timeout 45s xvfb-run -a "$project_root/build-local/vulkan_cloud_lod_smoke" \
  "$project_root/assets" > smoke.log 2>&1
smoke_status=$?
cat smoke.log
test "$smoke_status" -eq 0
```

Success requires exit code 0 and the final `Cloud LOD smoke: 24 Vulkan frames`
message. Keep the log in a task checkpoint along with the actual command/result.
Software Vulkan does not replace hardware GPU, platform CI or manual visual acceptance.

## Other desktop platforms

The target can be explicitly built with the desktop project configuration and
run as `vulkan_cloud_lod_smoke <absolute-asset-directory>` on a Vulkan-capable
display. Windows/macOS execution has not been verified by this Linux remediation.
Android/iOS do not build standalone CTest/smoke executables.
