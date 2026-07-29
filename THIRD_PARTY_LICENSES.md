# Third-party asset licenses

Record every imported asset pack here before committing it.

| Dependency | Author | Source URL | License | Local path | Changes |
| --- | --- | --- | --- | --- | --- |
| cgltf 1.15 | jkuhlmann | https://github.com/jkuhlmann/cgltf/tree/v1.15 | MIT | `external/cgltf/` | Unmodified pinned header; SHA-256 recorded in `UPSTREAM.md`. |
| nlohmann/json 3.12.0 | Niels Lohmann | https://github.com/nlohmann/json/releases/tag/v3.12.0 | MIT | `external/nlohmann/` | Unmodified release header; SHA-256 recorded in `UPSTREAM.md`. |

| Asset or pack | Author | Source URL | License | Local path | Changes |
| --- | --- | --- | --- | --- | --- |
| Example | Author name | https://example.invalid | CC0-1.0 | `assets/textures/third_party/example/` | Cropped to 16×16 |

Do not place third-party files in `source/` or `generated/`. Include a copy of
the upstream license beside the imported files when its terms require one.
