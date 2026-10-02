"""Strict, dependency-free PNG import for non-interlaced 8-bit artwork.

Accept RGB, RGBA, grayscale, gray-alpha and indexed (1/2/4/8 bit) PNGs,
including tRNS and all five standard row filters. Return canonical RGBA.
"""
import struct
import zlib


def read_png(path):
    try:
        return _decode(path.read_bytes())
    except (ValueError, IndexError, struct.error, zlib.error) as error:
        raise ValueError(f"invalid PNG {path}: {error}") from error


def _decode(data):
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("bad signature")
    offset = 8
    header = None
    palette = None
    transparency = b""
    compressed = bytearray()
    ended = False
    idat_ended = False
    seen_idat = False
    while offset < len(data):
        if offset + 12 > len(data):
            raise ValueError("truncated chunk")
        length, = struct.unpack_from(">I", data, offset)
        end = offset + 12 + length
        if end > len(data):
            raise ValueError("truncated chunk payload")
        kind = data[offset+4:offset+8]
        payload = data[offset+8:end-4]
        crc, = struct.unpack_from(">I", data, end-4)
        if zlib.crc32(kind + payload) & 0xffffffff != crc:
            raise ValueError("chunk checksum mismatch")
        offset = end
        if header is None and kind != b"IHDR":
            raise ValueError("IHDR must be first")
        if kind == b"IHDR":
            if header is not None or length != 13:
                raise ValueError("invalid IHDR")
            header = struct.unpack(">IIBBBBB", payload)
        elif kind == b"PLTE":
            if seen_idat or palette is not None or not length or length % 3 or length > 768:
                raise ValueError("invalid palette")
            palette = [tuple(payload[i:i+3]) for i in range(0, length, 3)]
        elif kind == b"tRNS":
            if seen_idat or transparency:
                raise ValueError("invalid transparency chunk")
            transparency = payload
        elif kind == b"IDAT":
            if idat_ended:
                raise ValueError("non-consecutive IDAT chunks")
            compressed.extend(payload)
            seen_idat = True
        elif kind == b"IEND":
            if length or not seen_idat:
                raise ValueError("invalid IEND")
            ended = True
            break
        elif kind[0] & 32 == 0:
            raise ValueError(f"unsupported critical chunk {kind!r}")
        if seen_idat and kind != b"IDAT":
            idat_ended = True
    if not ended or offset != len(data):
        raise ValueError("missing IEND or trailing data")
    width, height, depth, color, compression, filtering, interlace = header
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(color)
    if not channels or compression or filtering or interlace:
        raise ValueError("requires non-interlaced PNG with standard compression")
    if depth != 8 and not (color in (0, 3) and depth in (1, 2, 4)):
        raise ValueError("requires 8-bit channels or 1/2/4-bit gray/indexed PNG")
    if not 0 < width <= 4096 or not 0 < height <= 4096:
        raise ValueError("dimensions must be 1..4096")
    if color == 3 and (not palette or len(palette) > 1 << depth or len(transparency) > len(palette)):
        raise ValueError("missing or invalid indexed palette")
    if transparency and color != 3 and len(transparency) != {0: 2, 2: 6}.get(color):
        raise ValueError("invalid tRNS for color type")
    stride = (width * channels * depth + 7) // 8
    expected = height * (stride + 1)
    decoder = zlib.decompressobj()
    raw = decoder.decompress(compressed, expected + 1)
    if len(raw) != expected or not decoder.eof or decoder.unused_data:
        raise ValueError("incorrect decompressed pixel length")
    bpp = max(1, (channels * depth + 7) // 8)
    previous = bytearray(stride)
    pixels = []
    transparent_key = struct.unpack(">" + "H" * (len(transparency)//2), transparency) \
        if transparency and color in (0, 2) else None
    for y in range(height):
        start = y * (stride + 1)
        mode = raw[start]
        if mode > 4:
            raise ValueError("invalid row filter")
        row = bytearray(raw[start+1:start+1+stride])
        for i in range(stride):
            left = row[i-bpp] if i >= bpp else 0
            above = previous[i]
            corner = previous[i-bpp] if i >= bpp else 0
            if mode == 1: predictor = left
            elif mode == 2: predictor = above
            elif mode == 3: predictor = (left + above) // 2
            elif mode == 4:
                p = left + above - corner
                distances = (abs(p-left), abs(p-above), abs(p-corner))
                predictor = (left, above, corner)[distances.index(min(distances))]
            else: predictor = 0
            row[i] = (row[i] + predictor) & 255
        for x in range(width):
            if depth < 8:
                value = (row[x*depth//8] >> (8-depth-(x*depth % 8))) & ((1 << depth)-1)
                components = (value,)
            else:
                components = tuple(row[x*channels:(x+1)*channels])
            if color == 3:
                index = components[0]
                if index >= len(palette):
                    raise ValueError("palette index out of range")
                pixel = palette[index] + (transparency[index] if index < len(transparency) else 255,)
            elif color in (0, 4):
                gray = components[0] * 255 // ((1 << depth)-1)
                alpha = components[1] if color == 4 else 0 if components == transparent_key else 255
                pixel = (gray, gray, gray, alpha)
            else:
                pixel = components if color == 6 else components + (0 if components == transparent_key else 255,)
            pixels.append(pixel)
        previous = row
    return width, height, pixels
