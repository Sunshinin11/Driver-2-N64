#!/usr/bin/env python3
import struct, os, zlib

def load_png_raw(filename):
    with open(filename, 'rb') as f:
        data = f.read()
    # parse IDAT
    p = 8
    raw = bytearray()
    w, h = 0, 0
    while p < len(data):
        sz, tag = struct.unpack_from('>I4s', data, p)
        if tag == b'IHDR':
            w, h = struct.unpack_from('>II', data, p + 8)
        elif tag == b'IDAT':
            raw.extend(data[p + 8 : p + 8 + sz])
        p += 12 + sz
    decomp = zlib.decompress(raw)
    # unfilter scanlines
    pixels = bytearray(w * h * 4)
    line_sz = w * 4 + 1
    for y in range(h):
        line = decomp[y * line_sz + 1 : (y + 1) * line_sz]
        pixels[y * w * 4 : (y + 1) * w * 4] = line
    return w, h, pixels

def save_png(filename, w, h, rgba_bytes):
    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)

    raw = b''
    for y in range(h):
        raw += b'\x00' + rgba_bytes[y * w * 4 : (y + 1) * w * 4]
    
    ihdr = struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b'')
    os.makedirs(os.path.dirname(filename), exist_ok=True)
    with open(filename, 'wb') as f:
        f.write(png)

w, h, px = load_png_raw('docs/diag_textures/bld234_64x64_orig.png')
# Crop top 64x32
top = px[0 : 64 * 32 * 4]
save_png('docs/diag_textures/bld234_top32.png', 64, 32, top)

# Crop bot 64x32
bot = px[64 * 32 * 4 : 64 * 64 * 4]
save_png('docs/diag_textures/bld234_bot32.png', 64, 32, bot)

print("Saved bld234_top32.png and bld234_bot32.png")
