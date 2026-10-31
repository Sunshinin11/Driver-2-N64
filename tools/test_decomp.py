import struct, os, sys

def decomp_ps1_tpage(src_bytes):
    dest = bytearray(32768)
    ptr = 0x7FFF
    src_idx = 0
    while ptr >= 0 and src_idx < len(src_bytes):
        pix = src_bytes[src_idx]
        src_idx += 1
        if pix & 0x80:
            # signed char
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

def test():
    lev_path = "filesystem_d2/DRIVER2/LEVELS/CHICAGO.LEV"
    with open(lev_path, "rb") as f:
        f.seek(8)
        citylump_data = f.read(32)
        lumps = [struct.unpack_from("<ii", citylump_data, i * 8) for i in range(4)]
        tpage_off, tpage_size = lumps[1]

        f.seek(lumps[0][0])
        d1_buf = f.read(lumps[0][1])
        p = 8
        texinfo_buf = None
        while p + 8 <= len(d1_buf):
            lt, sz = struct.unpack_from("<II", d1_buf, p)
            if lt == 34:
                texinfo_buf = d1_buf[p+8 : p+8+sz]
                break
            p += 8 + ((sz + 3) & ~3)

        tpage_amount, _ = struct.unpack_from("<II", texinfo_buf, 0)
        ptr = 8 + (tpage_amount + 1) * 8
        for i in range(tpage_amount):
            amt = struct.unpack_from("<I", texinfo_buf, ptr)[0]
            ptr += 4 + amt * 8

        nperms = struct.unpack_from("<I", texinfo_buf, ptr)[0]
        permlist = [struct.unpack_from("<ii", texinfo_buf, ptr + 4 + i * 8) for i in range(nperms)]

        f.seek(tpage_off)
        tpage_data = f.read(tpage_size)

        cur_off = 0
        for i in range(nperms):
            tp_id, csize = permlist[i]
            npalettes = struct.unpack_from("<I", tpage_data, cur_off)[0]
            pix_data = tpage_data[cur_off + 4 + npalettes * 32 : cur_off + csize]
            decomp = decomp_ps1_tpage(pix_data)
            print(f"Perm {i:2d} (tset {tp_id:2d}): npal={npalettes:2d}, csize={csize:5d} -> decomp={len(decomp)} bytes (left={32768-len(decomp)})")
            cur_off += (csize + 2047) & ~2047

test()
