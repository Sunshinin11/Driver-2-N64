#!/usr/bin/env python3
import struct
import os

PolySizes = [
    8,  12, 16, 24,
    20, 20, 28, 32,
    8,  12, 16, 16,
    0,  0,  0,  0,
    12, 12, 12, 16,
    20, 20, 24, 24,
    0,  0,  0,  0,
    0,  0,  0,  0,
    8,  12, 16, 24,
    20, 24, 28, 32,
    0,  0,  0,  0,
    0,  0,  0,  0,
    12, 12, 12, 16,
    20, 20, 24, 24,
]

lev_path = 'filesystem_d2/DRIVER2/LEVELS/CHICAGO.LEV'
with open(lev_path, 'rb') as f:
    f.seek(8 + 2 * 8)
    offs, sz = struct.unpack("<II", f.read(8))
    f.seek(offs + 8)
    ltype, lsize = struct.unpack("<II", f.read(8))
    if ltype != 1:
        print(f"Erro: esperado LUMP_MODELS (1), encontrado {ltype}")
        exit(1)
    lump = f.read(lsize)

num_models = struct.unpack_from("<I", lump, 0)[0]
models = {}
pos = 4
for i in range(num_models):
    msize = struct.unpack_from("<I", lump, pos)[0]
    pos += 4
    if msize > 0:
        mdata = lump[pos : pos + msize]
        (shape_flags, flags2, inst_num, tri_verts, zbias,
         bsphere, npn, nverts, npolys,
         voffs, polyoffs, noffs, pnoffs, colloffs) = struct.unpack_from("<HHhBBhHHHiiiii", mdata, 0)
        models[i] = {
            'id': i, 'size': msize, 'data': mdata,
            'inst_num': inst_num, 'nverts': nverts, 'npolys': npolys,
            'voffs': voffs, 'polyoffs': polyoffs,
            'shape_flags': shape_flags, 'flags2': flags2,
        }
        pos += msize

for mid in [234, 235, 83, 108, 1, 4]:
    if mid in models:
        m = models[mid]
        mdata = m['data']
        poffs = m['polyoffs']
        print(f"\n=== Model {mid}: npolys={m['npolys']}, nverts={m['nverts']}, flags=0x{m['shape_flags']:04x} ===")
        polys_summary = {}
        for p in range(m['npolys']):
            if poffs >= len(mdata):
                break
            ptype = mdata[poffs] & 31
            psize = PolySizes[ptype] if ptype < len(PolySizes) else 0
            if psize == 0:
                print(f"  P{p}: Unknown ptype {ptype} (raw: {mdata[poffs]}) at offset {poffs}")
                break
            
            # Decodificar de acordo com ptype
            # 11, 21, 23 sao quads texturizados
            # 4, 5, 20 sao outros
            tset = mdata[poffs + 1] if psize >= 2 else 0
            tid = mdata[poffs + 2] if psize >= 3 else 0
            polys_summary[ptype] = polys_summary.get(ptype, 0) + 1
            if p < 8:
                v_str = ""
                uv_str = ""
                if ptype in (11, 21, 23):
                    v0, v1, v2, v3 = struct.unpack_from("<BBBB", mdata, poffs + 4)
                    u0, v0_uv, u1, v1_uv, u2, v2_uv, u3, v3_uv = struct.unpack_from("<8B", mdata, poffs + 8)
                    v_str = f"v=({v0},{v1},{v2},{v3})"
                    uv_str = f"UV0=({u0},{v0_uv}) UV1=({u1},{v1_uv}) UV2=({u2},{v2_uv}) UV3=({u3},{v3_uv})"
                elif psize >= 8:
                    v_str = f"raw_hdr: {list(mdata[poffs:poffs+min(16, psize)])}"
                print(f"  P{p}: type={ptype:2d} sz={psize:2d} tset={tset:3d} tid={tid:3d} {v_str} {uv_str}")
            poffs += psize
        print(f"  Poly type counts: {polys_summary}")
