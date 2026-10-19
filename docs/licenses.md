# Licenses and third-party components

MinecraftC is licensed under the [GNU GPL v3.0 only](../LICENSE), except for
third-party components and assets that retain their own licenses.

| Component or asset | License | Location |
| --- | --- | --- |
| SDL 3.4.10 | Zlib | CMake FetchContent build directory |
| GLM 1.0.1 | MIT | System package or CMake FetchContent build directory |
| MoltenVK 1.4.1 | Apache-2.0 | macOS bundles and `licenses/` |
| Vulkan Memory Allocator 3.3.0 | MIT | `external/VulkanMemoryAllocator/` |
| FastNoiseLite pinned snapshot | MIT | `external/FastNoiseLite/` |
| cgltf 1.15 | MIT | `external/cgltf/` |
| mjansson/mdns pinned snapshot | Unlicense/public domain | `external/mdns/` |
| nlohmann/json 3.12.0 | MIT | `external/nlohmann/` |
| stb_image 2.30 | MIT or public domain (MIT used) | `external/stb/stb_image.h` |
| stb_truetype 1.26 | MIT | `external/stb/stb_truetype.h` |
| Noto Sans CJK SC Regular | SIL OFL 1.1 | `assets/fonts/noto/` |
| Noto Naskh Arabic Regular | SIL OFL 1.1 | `assets/fonts/noto/` |

Vendored dependency directories retain their upstream, checksum, and license
records in an `UPSTREAM.md` or dependency README; CMake records pins for fetched
dependencies. See [ASSET_SOURCES.md](../ASSET_SOURCES.md) and
[assets/textures/LICENSE.md](../assets/textures/LICENSE.md) for asset provenance.

Paths in the component table refer to the source checkout or dependency build.
Release packages retain available runtime/asset notices beside their files;
source dependencies are not all bundled as source in binary packages.
