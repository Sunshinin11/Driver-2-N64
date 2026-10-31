#!/usr/bin/env python3
"""
tools/mkbuilding.py - Extrai e valida modelos de predios do LUMP_MODELS do Driver 2
Lê arquivos .LEV (ex: CHICAGO.LEV) e mapeia os modelos que contem poligonos de predio (ptype 11, 21, 23).
"""

import struct
import sys
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

def parse_lev_models(lev_path):
    if not os.path.exists(lev_path):
        print(f"Arquivo nao encontrado: {lev_path}")
        return

    with open(lev_path, "rb") as f:
        # Lê citylump 2 da tabela inicial
        f.seek(8 + 2 * 8)
        offs, sz = struct.unpack("<II", f.read(8))
        f.seek(offs + 8) # pula cabecalho de bloco
        ltype, lsize = struct.unpack("<II", f.read(8))
        if ltype != 1:
            print(f"Erro: esperado LUMP_MODELS (1), encontrado {ltype}")
            return
        lump = f.read(lsize)

    num_models = struct.unpack_from("<I", lump, 0)[0]
    print(f"=== {lev_path} ===")
    print(f"Total de slots de modelo no pacote: {num_models}")

    pos = 4
    models = {}
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

    print(f"Modelos nao-vazios: {len(models)}")

    # Modelos com poligonos de predio (ptype 11, 21, 23)
    bld_models = {}
    total_quads = 0
    total_verts = 0
    type_counts = {11: 0, 21: 0, 23: 0}

    for mid, m in models.items():
        if m['npolys'] == 0:
            continue
        mdata = m['data']
        poffs = m['polyoffs']
        polys = []
        for _ in range(m['npolys']):
            if poffs >= len(mdata): break
            ptype = mdata[poffs] & 31
            psize = PolySizes[ptype] if ptype < len(PolySizes) else 0
            if psize == 0: break
            if ptype in (11, 21, 23):
                v0, v1, v2, v3 = struct.unpack_from("<BBBB", mdata, poffs + 4)
                polys.append((ptype, v0, v1, v2, v3))
                type_counts[ptype] += 1
            poffs += psize

        if len(polys) > 0:
            root = m
            while root['inst_num'] != -1 and root['inst_num'] in models:
                root = models[root['inst_num']]

            # Check disqualifying flags (must be pure building)
            disq = False
            if m['shape_flags'] & (0x4000 | 0x80 | 0x400 | 0x2000 | 0x8 | 0x1000):
                disq = True
            if m['flags2'] & (0x800 | 0x1000 | 0x2000 | 0x400 | 0x200 | 0x1 | 0x20 | 0x40 | 0x80 | 0x4000 | 0x8000):
                disq = True

            if not disq:
                bld_models[mid] = (m, root, polys)
                total_quads += len(polys)
                total_verts += root['nverts']

    print(f"Modelos de predios puros encontrados: {len(bld_models)} (excluidos: {len(models) - len(bld_models)} no caminho antigo)")
    print(f"Total de quadripolos de predio: {total_quads} ({total_quads * 2} triangulos)")
    print(f"Distribuicao por tipo: ptype 11={type_counts[11]}, ptype 21={type_counts[21]}, ptype 23={type_counts[23]}")
    unique_roots = set()
    total_unique_verts = 0
    for mid, (m, root, polys) in bld_models.items():
        rid = root['id']
        if rid not in unique_roots:
            unique_roots.add(rid)
            total_unique_verts += root['nverts']

    print(f"Modelos raiz unicos: {len(unique_roots)}")
    print(f"Total de vertices unicos: {total_unique_verts}")
    vtx_bytes = total_unique_verts * 16
    print(f"Memoria necessaria para T3DVertPacked: {vtx_bytes} bytes ({vtx_bytes / 1024.0:.1f} KB)")
    max_v = max(m[1]['nverts'] for m in bld_models.values())
    max_q = max(len(m[2]) for m in bld_models.values())
    print(f"Max vertices em um modelo: {max_v}")
    print(f"Max quads em um modelo: {max_q}")
    print(f"Media de quads por modelo: {total_quads / len(bld_models):.1f}")

    sorted_bld = sorted(bld_models.items(), key=lambda x: len(x[1][2]), reverse=True)
    print("\nTop 10 modelos de predio com mais poligonos:")
    for mid, (m, root, polys) in sorted_bld[:10]:
        print(f"  Model {mid:4d}: {len(polys):3d} quads, {root['nverts']:3d} verts (inst={m['inst_num']})")

if __name__ == "__main__":
    lev = sys.argv[1] if len(sys.argv) > 1 else "filesystem_d2/DRIVER2/LEVELS/CHICAGO.LEV"
    parse_lev_models(lev)
