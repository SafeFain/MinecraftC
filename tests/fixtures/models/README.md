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
