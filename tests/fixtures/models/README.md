# glTF loader fixtures

`GltfLoaderTests.cpp` deterministically writes its small GLB and external-buffer
glTF fixtures into the platform temporary directory before loading them. Keeping
the binary fixture definitions beside the assertions makes malformed offsets,
joint limits, and extension requirements independently auditable without
checking generated binary files into the repository.

Coverage includes embedded PNG image data; 16-bit, 32-bit, and non-indexed
triangles; TRS and matrix nodes; skinning; linear, step, and cubic-spline
animation channels; an external-buffer glTF; an oversized skin; an accessor
range overflow; an out-of-range vertex joint; a required unsupported extension;
and a truncated external buffer.

Review regressions additionally cover overflowing accessor arithmetic,
unnormalized integer weights, matrix-node animation rejection, and two skins
sharing one mesh.

`GltfUpgradeCases.h` also uses a confined in-memory asset source to test external
PNG/JPEG and data URIs, sampler separation, normalized UV/weights, sparse overlays
with/without base data, sparse index rejection, inactive scenes, hierarchy cycles,
nonfinite vertices, namespace/failed-load caching and 64/65/256/257-joint boundaries.
The one-pixel JPEG is original generated test data, embedded beside the assertions.
