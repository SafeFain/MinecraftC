# Third-party asset licenses

Record every imported asset pack here before committing it.

| Dependency | Author | Source URL | License | Local path | Changes |
| --- | --- | --- | --- | --- | --- |
| SDL 3.4.10 | SDL contributors | https://github.com/libsdl-org/SDL/tree/release-3.4.10 | Zlib | CMake FetchContent build tree | Unmodified pinned source build with unused subsystems disabled. |
| cgltf 1.15 | jkuhlmann | https://github.com/jkuhlmann/cgltf/tree/v1.15 | MIT | `external/cgltf/` | Unmodified pinned header; SHA-256 recorded in `UPSTREAM.md`. |
| nlohmann/json 3.12.0 | Niels Lohmann | https://github.com/nlohmann/json/releases/tag/v3.12.0 | MIT | `external/nlohmann/` | Unmodified release header; SHA-256 recorded in `UPSTREAM.md`. |
| stb_truetype 1.26 | Sean Barrett and contributors | https://github.com/nothings/stb/blob/master/stb_truetype.h | MIT | `external/stb/stb_truetype.h` | Unmodified header; SHA-256 recorded in `UPSTREAM-truetype.md`. |

| Asset or pack | Author | Source URL | License | Local path | Changes |
| --- | --- | --- | --- | --- | --- |
| Example | Author name | https://example.invalid | CC0-1.0 | `assets/textures/third_party/example/` | Cropped to 16×16 |
| Noto Sans CJK SC Regular | Google, Adobe, and contributors | https://github.com/notofonts/noto-cjk | SIL OFL 1.1 | `assets/fonts/noto/` | Unmodified font; SHA-256 recorded in `UPSTREAM.md`. |

Do not place third-party files in `source/` or `generated/`. Include a copy of
the upstream license beside the imported files when its terms require one.
