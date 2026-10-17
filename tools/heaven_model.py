"""Export the original five-box Starstep model and face palette (no new artwork)."""
import base64
import json
import struct
import zlib
from pathlib import Path


def generate():
    root = Path(__file__).resolve().parents[1] / "assets/plugins/official_heaven"
    root.mkdir(parents=True, exist_ok=True)
    colors = [(117, 78, 43), (158, 112, 62), (125, 130, 137), (213, 221, 224),
              (244, 193, 49), (61, 220, 208), (142, 127, 244), (50, 54, 65), (227, 217, 184)]
    pixels = bytearray([255] * (64 * 64 * 4))
    for palette, color in enumerate(colors):
        for face, shade in enumerate([.90, .78, .70, .84, 1., .60]):
            tile = palette * 6 + face
            for y in range(8):
                for x in range(8):
                    grain = .91 if (x * 3 + y * 5) % 7 == 0 else 1.
                    # Match the C++ float operations before truncating to bytes.
                    f32 = lambda n: struct.unpack('<f', struct.pack('<f', n))[0]
                    rgb = [int(f32(f32(c * f32(shade)) * f32(grain))) for c in color]
                    index = ((tile // 8 * 8 + y) * 64 + tile % 8 * 8 + x) * 4
                    pixels[index:index + 3] = bytes(rgb)
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', 64, 64, 8, 6, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(b''.join(b'\0' + pixels[y * 256:(y + 1) * 256] for y in range(64)))) + chunk(b'IEND', b'')
    (root / 'palette.png').write_bytes(png)
    corners = [(-.5, -.5, -.5), (.5, -.5, -.5), (.5, .5, -.5), (-.5, .5, -.5),
               (-.5, -.5, .5), (.5, -.5, .5), (.5, .5, .5), (-.5, .5, .5)]
    faces = [(0, 3, 2, 1), (5, 6, 7, 4), (4, 7, 3, 0), (1, 2, 6, 5), (3, 7, 6, 2), (4, 0, 1, 5)]
    boxes = [((0, .24, 0), (.10, 1.05, .10), 0), ((0, .72, 0), (.26, .10, .26), 4),
             ((0, .90, 0), (.20, .28, .20), 6), ((0, 1.07, 0), (.12, .10, .12), 5),
             ((0, -.20, 0), (.14, .10, .14), 4)]
    vertices, indices = [], []
    for center, size, palette in boxes:
        for face, corners_at_face in enumerate(faces):
            tile = palette * 6 + face
            lo = ((tile % 8 + .0625) / 8, (tile // 8 + .0625) / 8)
            hi = ((tile % 8 + .9375) / 8, (tile // 8 + .9375) / 8)
            uvs = [(lo[0], hi[1]), lo, (hi[0], lo[1]), hi]
            start = len(vertices)
            for corner, uv in zip(corners_at_face, uvs):
                vertices.append(tuple(f32(f32(center[i]) + f32(f32(corners[corner][i]) * f32(size[i]))) for i in range(3)) + uv)
            indices.extend(start + i for i in (0, 1, 2, 0, 2, 3))
    vb = b''.join(struct.pack('<5f', *v) for v in vertices)
    ib = struct.pack('<' + 'I' * len(indices), *indices)
    data = vb + ib + png
    doc = dict(asset=dict(version='2.0', generator='MinecraftC original held tool exporter'),
               buffers=[dict(byteLength=len(data), uri='data:application/octet-stream;base64,' + base64.b64encode(data).decode())],
               bufferViews=[dict(buffer=0, byteOffset=0, byteLength=len(vb), byteStride=20), dict(buffer=0, byteOffset=len(vb), byteLength=len(ib)), dict(buffer=0, byteOffset=len(vb)+len(ib), byteLength=len(png))],
               accessors=[dict(bufferView=0, componentType=5126, count=len(vertices), type='VEC3', byteOffset=0,
                               min=[min(v[i] for v in vertices) for i in range(3)], max=[max(v[i] for v in vertices) for i in range(3)]),
                          dict(bufferView=0, componentType=5126, count=len(vertices), type='VEC2', byteOffset=12),
                          dict(bufferView=1, componentType=5125, count=len(indices), type='SCALAR')],
               images=[dict(bufferView=2, mimeType='image/png')], samplers=[dict(magFilter=9728, minFilter=9728, wrapS=33071, wrapT=33071)],
               textures=[dict(source=0, sampler=0)], materials=[dict(doubleSided=True, pbrMetallicRoughness=dict(baseColorTexture=dict(index=0), metallicFactor=0, roughnessFactor=1))],
               meshes=[dict(primitives=[dict(attributes=dict(POSITION=0, TEXCOORD_0=1), indices=2, material=0)])],
               nodes=[dict(mesh=0)], scenes=[dict(nodes=[0])], scene=0)
    (root / 'starstep.gltf').write_bytes((json.dumps(doc, separators=(',', ':')) + '\n').encode('utf-8'))


if __name__ == '__main__':
    generate()
