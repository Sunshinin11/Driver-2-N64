#!/usr/bin/env python3
import os, struct, zlib

def decomp_ps1_tpage(src_bytes):
    dest = bytearray(32768)
    ptr = 0x7FFF
    src_idx = 0
    while ptr >= 0 and src_idx < len(src_bytes):
        pix = src_bytes[src_idx]
        src_idx += 1
        if pix & 0x80:
            spix = (pix ^ 0x80) - 0x80
            count = -spix + 2
            p = src_bytes[src_idx]
            src_idx += 1
            for _ in range(count):
                if ptr >= 0:
                    dest[ptr] = p
                    ptr -= 1
        else:
            count = pix + 1
            for _ in range(count):
                if ptr >= 0:
                    dest[ptr] = src_bytes[src_idx]
                    src_idx += 1
                    ptr -= 1
    return dest

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

def ps1_to_rgba32(c16):
    r5 = c16 & 31
    g5 = (c16 >> 5) & 31
    b5 = (c16 >> 10) & 31
    r = (r5 << 3) | (r5 >> 2)
    g = (g5 << 3) | (g5 >> 2)
    b = (b5 << 3) | (b5 >> 2)
    a = 255 if c16 != 0 else 0
    return (r, g, b, a)

def get_texel(decomp, u, v):
    byte_idx = v * 128 + (u >> 1)
    b = decomp[byte_idx]
    if (u & 1) == 0:
        return b & 15
    else:
        return (b >> 4) & 15

def extract_subtex(decomp, pal_raw, u_min, v_min, w, h):
    pal_rgba = [ps1_to_rgba32(c) for c in pal_raw]
    out_rgba = bytearray(w * h * 4)
    for dy in range(h):
        for dx in range(w):
            u = u_min + dx
            v = v_min + dy
            idx = get_texel(decomp, u, v)
            c = pal_rgba[idx]
            p = (dy * w + dx) * 4
            out_rgba[p : p + 4] = bytes(c)
    return out_rgba

with open('filesystem_d2/DRIVER2/LEVELS/CHICAGO.LEV', 'rb') as f:
    # 1. TPage 0 (permanente)
    f.seek(8 + 8)
    tpage0_off, tpage0_sz = struct.unpack('<ii', f.read(8))
    f.seek(tpage0_off)
    npal0 = struct.unpack('<I', f.read(4))[0]
    pal0_bytes = f.read(npal0 * 32)
    rle0_bytes = f.read(tpage0_sz - 4 - npal0 * 32)
    decomp0 = decomp_ps1_tpage(rle0_bytes)

    # 2. TPage 5 (Área 7, setor 2614 do Spool)
    spool_off = 1040384
    tpage5_off = spool_off + 2614 * 2048
    f.seek(tpage5_off)
    npal5 = struct.unpack('<I', f.read(4))[0]
    pal5_bytes = f.read(npal5 * 32)
    rle5_bytes = f.read(17 * 2048 - 4 - npal5 * 32)
    decomp5 = decomp_ps1_tpage(rle5_bytes)

# Extrair Road M1 (Model 83 e 108: tset=0, tid=10, pal=10, UV: 64,128 a 128,192 -> 64x64)
pal0_10 = struct.unpack_from('<16H', pal0_bytes, 10 * 32)
road_rgba = extract_subtex(decomp0, pal0_10, 64, 128, 64, 64)
save_png('docs/diag_textures/road_m1_64x64_orig.png', 64, 64, road_rgba)
print("Saved docs/diag_textures/road_m1_64x64_orig.png")

# Extrair Building 234 (tset=5, tid=6, pal=6, UV: 64,0 a 128,64 -> 64x64)
pal5_6 = struct.unpack_from('<16H', pal5_bytes, 6 * 32)
bld234_rgba = extract_subtex(decomp5, pal5_6, 64, 0, 64, 64)
save_png('docs/diag_textures/bld234_64x64_orig.png', 64, 64, bld234_rgba)
print("Saved docs/diag_textures/bld234_64x64_orig.png")

# Extrair Building 235 (tset=5, tid=0, pal=0, UV: 0,0 a 64,64 -> 64x64)
pal5_0 = struct.unpack_from('<16H', pal5_bytes, 0 * 32)
bld235_rgba = extract_subtex(decomp5, pal5_0, 0, 0, 64, 64)
save_png('docs/diag_textures/bld235_64x64_orig.png', 64, 64, bld235_rgba)
print("Saved docs/diag_textures/bld235_64x64_orig.png")

