import os, struct, zlib, subprocess

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
    with open(filename, 'wb') as f:
        f.write(png)

def build_proof_textures():
    os.makedirs('filesystem_vadpcm/TEX', exist_ok=True)
    os.makedirs('build_d2/tex_tmp', exist_ok=True)

    lev_path = 'filesystem_d2/DRIVER2/LEVELS/CHICAGO.LEV'
    with open(lev_path, 'rb') as f:
        f.seek(8)
        citylump_data = f.read(32)
        lumps = [struct.unpack_from('<ii', citylump_data, i * 8) for i in range(4)]
        tpage_off, tpage_size = lumps[1]
        f.seek(tpage_off)
        tpage_data = f.read(tpage_size)

    npal = struct.unpack_from('<I', tpage_data, 0)[0]
    pal_data = tpage_data[4 : 4 + npal * 32]
    rle_data = tpage_data[4 + npal * 32 : ]
    decomp = decomp_ps1_tpage(rle_data)

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

    def extract_and_convert(name, u_min, v_min, pal_idx, out_w, out_h):
        pal_raw = struct.unpack_from('<16H', pal_data, pal_idx * 32)
        pal_rgba = [ps1_to_rgba32(c) for c in pal_raw]
        
        out_rgba = bytearray(out_w * out_h * 4)
        scale_y = 64 / out_h
        scale_x = 64 / out_w
        for dy in range(out_h):
            for dx in range(out_w):
                v0 = int(v_min + dy * scale_y)
                v1 = int(v_min + min(63, dy * scale_y + 1))
                u = int(u_min + dx * scale_x)
                
                c0 = pal_rgba[get_texel(decomp, u, v0)]
                c1 = pal_rgba[get_texel(decomp, u, v1)]
                
                r = (c0[0] + c1[0]) // 2
                g = (c0[1] + c1[1]) // 2
                b = (c0[2] + c1[2]) // 2
                a = 255
                
                idx = (dy * out_w + dx) * 4
                out_rgba[idx : idx + 4] = bytes([r, g, b, a])
                
        png_path = f'build_d2/tex_tmp/{name}.png'
        save_png(png_path, out_w, out_h, out_rgba)
        
        sprite_path = f'filesystem_vadpcm/TEX/{name}.sprite'
        cmd = ['/opt/libdragon/bin/mksprite', '-f', 'CI8', '-c', '0', '-o', 'filesystem_vadpcm/TEX', png_path]
        subprocess.run(cmd, check=True)
        print(f'Generated {sprite_path} (size={os.path.getsize(sprite_path)} bytes)')

    # Building Model 1: tset 0, tid 4, UV (0, 88). 64x32 CI8
    extract_and_convert('bld1', 0, 88, 4, 64, 32)
    # Tile Model 4: tset 0, tid 11, UV (0, 24). 64x32 CI8
    extract_and_convert('tile4', 0, 24, 11, 64, 32)

if __name__ == '__main__':
    build_proof_textures()

