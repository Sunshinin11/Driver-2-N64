#!/usr/bin/env python3
import os, sys, struct, zlib, subprocess

def level_lumps(lev):
    """Read DATA1/DATA2 through the level directory, not Chicago offsets."""
    lumps = {}
    for section in (0, 2):
        offset, size = struct.unpack_from('<II', lev, 8 + section * 8)
        ptr, end = offset + 8, offset + size
        if end > len(lev):
            raise ValueError('Level section extends beyond the file')
        while ptr + 8 <= end:
            kind, length = struct.unpack_from('<II', lev, ptr)
            if ptr + 8 + length > end:
                break
            lumps[kind] = ptr + 8
            ptr += 8 + length
    return lumps


def read_spooled_page(lev, spoolinfo_ptr, tset):
    """Match LoadInAreaTSets/UpdateSpoolPC: one CLUT sector, then raw pixels."""
    ptr = spoolinfo_ptr + 4  # model spool buffer size
    music_size = struct.unpack_from('<I', lev, ptr)[0]
    ptr += 4 + music_size
    area_count = struct.unpack_from('<I', lev, ptr)[0]
    area_data = ptr + 4
    area_pages = area_data + area_count * 16
    spool_offset = struct.unpack_from('<I', lev, 8 + 3 * 8)[0]
    found = None
    for area in range(area_count):
        record = area_data + area * 16
        sector = struct.unpack_from('<H', lev, record)[0]
        count = lev[record + 10]
        # Unused area records can be filled with 0xFF.
        if count > 16 or sector == 0xFFFF:
            continue
        for index in range(count):
            if lev[area_pages + area * 16 + index] != tset:
                continue
            page_offset = spool_offset + (sector + index * 17) * 2048
            if page_offset + 17 * 2048 > len(lev):
                raise ValueError('Texture page extends beyond the level file')
            npal = struct.unpack_from('<I', lev, page_offset)[0]
            if not 1 <= npal <= 32:
                raise ValueError(f'Invalid palette count {npal} for page {tset}')
            palettes = lev[page_offset + 4:page_offset + 4 + npal * 32]
            pixels = lev[page_offset + 2048:page_offset + 17 * 2048]
            page = (palettes, pixels)
            if found is not None and found != page:
                raise ValueError(f'Conflicting copies of virtual texture page {tset}')
            if found is None:
                print(f'  Spool page {tset}: area={area}, sector={sector + index * 17}, offset={page_offset}')
                found = page
    if found is None:
        raise ValueError(f'Page {tset} is not in the region spool; permanent pages need their compressed loader')
    return found

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

def save_png_indexed_4bit(filename, w, h, indices, pal_rgba):
    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)

    plte_data = bytearray()
    trns_data = bytearray()
    for r, g, b, a in pal_rgba:
        plte_data.extend([r, g, b])
        trns_data.append(a)

    raw = bytearray()
    for y in range(h):
        raw.append(0) # filter None
        for x in range(w):
            raw.append(indices[y * w + x])

    ihdr = struct.pack('>IIBBBBB', w, h, 8, 3, 0, 0, 0)
    png = (b'\x89PNG\r\n\x1a\n' +
           chunk(b'IHDR', ihdr) +
           chunk(b'PLTE', plte_data) +
           chunk(b'tRNS', trns_data) +
           chunk(b'IDAT', zlib.compress(bytes(raw))) +
           chunk(b'IEND', b''))

    os.makedirs(os.path.dirname(filename), exist_ok=True)
    with open(filename, 'wb') as f:
        f.write(png)

def extract_material(lev_path, tset, tid):
    with open(lev_path, 'rb') as f:
        lev = f.read()

    lumps = level_lumps(lev)
    tinfo_ptr = lumps.get(34)

    if not tinfo_ptr:
        raise RuntimeError("LUMP_TEXTUREINFO not found in DATA1")

    tpage_amount, texamount = struct.unpack_from('<ii', lev, tinfo_ptr)
    if tset < 0 or tset >= tpage_amount:
        raise ValueError(f"tset {tset} out of range [0, {tpage_amount})")

    curr = tinfo_ptr + 8 + (tpage_amount + 1) * 8
    target_tex = None
    for i in range(tpage_amount):
        num_tex = struct.unpack_from('<i', lev, curr)[0]
        curr += 4
        if i == tset:
            if tid < 0 or tid >= num_tex:
                raise ValueError(f"tid {tid} out of range [0, {num_tex}) for tset {tset}")
            for j in range(num_tex):
                tex_id, nameoff, x, y, w, h = struct.unpack_from('<HHBBBB', lev, curr)
                if j == tid:
                    target_tex = {'id': tex_id, 'nameoff': nameoff, 'x': x, 'y': y, 'width': w, 'height': h}
                curr += 8
            break
        else:
            curr += num_tex * 8

    print(f"Material (tset={tset}, tid={tid}) from TEXINF:")
    print(f"  x={target_tex['x']}, y={target_tex['y']}, width={target_tex['width']}, height={target_tex['height']}")

    # Check TMEM limit (4096 texels = 2048 bytes in CI4)
    texel_count = target_tex['width'] * target_tex['height']
    if texel_count > 4096:
        print(f"  AVISO: Material excede o limite da TMEM para CI4 de passe unico ({texel_count} > 4096 texels). Fallback solido.")
        return False

    # Region pages are NOT in the compressed permanent-page format.
    pal_bytes, decomp = read_spooled_page(lev, lumps[26], tset)
    if tid >= len(pal_bytes) // 32:
        raise ValueError(f'Palette {tid} does not exist in texture page {tset}')
    if target_tex['x'] + target_tex['width'] > 256 or target_tex['y'] + target_tex['height'] > 256:
        raise ValueError('TEXINF rectangle extends beyond its 256x256 page')

    # In PS1 Driver 2, tid maps to CLUT index tid (CLUT 6)
    pal_clut = struct.unpack_from('<16H', pal_bytes, tid * 32)
    pal_rgba = [ps1_to_rgba32(c) for c in pal_clut]

    w = target_tex['width']
    h = target_tex['height']
    x0 = target_tex['x']
    y0 = target_tex['y']

    indices = bytearray(w * h)
    for dy in range(h):
        for dx in range(w):
            u = x0 + dx
            v = y0 + dy
            idx = get_texel(decomp, u, v)
            indices[dy * w + dx] = idx

    png_path = f"docs/diag_textures/tex_s{tset}_t{tid}.png"
    save_png_indexed_4bit(png_path, w, h, indices, pal_rgba)
    print(f"  Salvo PNG em: {png_path} ({w}x{h}, {len(pal_rgba)} cores)")

    sprite_out = f"filesystem_vadpcm/TEX/tex_s{tset}_t{tid}.sprite"
    os.makedirs("filesystem_vadpcm/TEX", exist_ok=True)
    cmd = ["/opt/libdragon/bin/mksprite", "-c", "0", "-f", "CI4", "-o", "filesystem_vadpcm/TEX", png_path]
    print(f"  Executando mksprite: {' '.join(cmd)}")
    subprocess.run(cmd, check=True)
    if os.path.exists(sprite_out):
        sz = os.path.getsize(sprite_out)
        print(f"  Sprite CI4 gerado com sucesso: {sprite_out} ({sz} bytes)")
        # Stage in build_d2/fs/TEX if directory exists
        stage_dir = "build_d2/fs/TEX"
        if os.path.exists(stage_dir):
            import shutil
            staged_sprite = os.path.join(stage_dir, os.path.basename(sprite_out))
            # The build stages assets with hard links when possible.
            if not os.path.exists(staged_sprite) or not os.path.samefile(sprite_out, staged_sprite):
                shutil.copy2(sprite_out, staged_sprite)
        return True
    return False

def main():
    lev_path = 'filesystem_d2/DRIVER2/LEVELS/CHICAGO.LEV'
    tset = 5
    tid = 6
    if len(sys.argv) >= 3:
        tset = int(sys.argv[1])
        tid = int(sys.argv[2])
    extract_material(lev_path, tset, tid)

if __name__ == '__main__':
    main()

