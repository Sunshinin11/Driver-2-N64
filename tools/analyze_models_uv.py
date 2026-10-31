import struct, os, sys

def analyze():
    lev_path = "filesystem_d2/DRIVER2/LEVELS/CHICAGO.LEV"
    with open(lev_path, "rb") as f:
        hdr = f.read(8)
        citylump_data = f.read(32)
        lumps = []
        for i in range(4):
            x, y = struct.unpack_from("<ii", citylump_data, i * 8)
            lumps.append((x, y))

        f.seek(lumps[0][0])
        d1_buf = f.read(lumps[0][1])

        f.seek(lumps[2][0])
        d2_buf = f.read(lumps[2][1])

        # Parse Lump 34 (LUMP_TEXTUREINFO) from DATA1
        p = 8
        texinfo_buf = None
        while p + 8 <= len(d1_buf):
            lt, sz = struct.unpack_from("<II", d1_buf, p)
            if lt == 34:
                texinfo_buf = d1_buf[p+8 : p+8+sz]
                break
            p += 8 + ((sz + 3) & ~3)

        tpage_amount, _ = struct.unpack_from("<II", texinfo_buf, 0)
        tp_offset = 8
        ptr = tp_offset + (tpage_amount + 1) * 8
        all_textures = []
        for i in range(tpage_amount):
            amt = struct.unpack_from("<I", texinfo_buf, ptr)[0]
            ptr += 4
            texs = []
            for j in range(amt):
                tid, nameoff, tx, ty, tw, th = struct.unpack_from("<HHBBBB", texinfo_buf, ptr + j * 8)
                texs.append((tid, nameoff, tx, ty, tw, th))
            all_textures.append(texs)
            ptr += amt * 8

        nperms = struct.unpack_from("<I", texinfo_buf, ptr)[0]
        permlist = []
        for i in range(16):
            px, py = struct.unpack_from("<ii", texinfo_buf, ptr + 4 + i * 8)
            permlist.append((px, py))
        perm_tsets = set(permlist[i][0] for i in range(nperms))

        # Parse Lump 1 (LUMP_MODELS) from DATA2
        p = 8
        mdl_buf = None
        while p + 8 <= len(d2_buf):
            lt, sz = struct.unpack_from("<II", d2_buf, p)
            if lt == 1:
                mdl_buf = d2_buf[p+8 : p+8+sz]
                break
            p += 8 + ((sz + 3) & ~3)

        model_amts = struct.unpack_from("<I", mdl_buf, 0)[0]
        mds_p = 4
        models = {}
        for mid in range(model_amts):
            msize = struct.unpack_from("<I", mdl_buf, mds_p)[0]
            mds_p += 4
            if msize > 0:
                mdata = mdl_buf[mds_p : mds_p + msize]
                # struct MODEL:
                # u_short shape_flags (2)
                # u_short flags2 (2)
                # short instance_number (2)
                # u_char tri_verts (1)
                # u_char zBias (1)
                # short bounding_sphere (2)
                # u_short num_point_normals (2)
                # u_short num_vertices (2)
                # u_short num_polys (2)
                # int vertices (4)
                # int poly_block (4)
                shape_flags, flags2, inst_num, tri_verts, zBias, bsphere, npt_norm, nverts, npolys, v_off, p_off = struct.unpack_from("<HHhBBhHHHII", mdata, 0)
                models[mid] = {
                    "shape_flags": shape_flags,
                    "flags2": flags2,
                    "inst_num": inst_num,
                    "nverts": nverts,
                    "npolys": npolys,
                    "mdata": mdata,
                    "p_off": p_off
                }
            mds_p += msize

        poly_sizes = [
            0, 0, 0, 0, 24, 24, 0, 0,
            0, 0, 20, 20, 0, 0, 0, 0,
            0, 0, 0, 20, 20, 20, 24, 24
        ]

        # Inspect Building Model 1
        tm = models[1]
        print(f"\n--- Inspecting Building Model 1 (npolys={tm['npolys']}, nverts={tm['nverts']}, poff={tm['p_off']:#x}) ---")
        pdata = tm["mdata"]
        poff = tm["p_off"]
        for p in range(tm["npolys"]):
            ptype = pdata[poff] & 0x1F
            tset = pdata[poff + 1]
            tid = pdata[poff + 2]
            if ptype in (4, 5, 11, 19, 21):
                u0, v0, u1, v1, u2, v2, u3, v3 = struct.unpack_from("<8B", pdata, poff + 8)
                print(f"  poly {p:2d}: ptype={ptype:2d} tset={tset:2d} (perm={tset in perm_tsets}) tid={tid:2d} UV: ({u0:3d},{v0:3d})-({u1:3d},{v1:3d})-({u2:3d},{v2:3d})-({u3:3d},{v3:3d}) size={abs(u1-u0)}x{abs(v2-v0)}")
            elif ptype in (10, 20):
                u0, v0, u1, v1, u2, v2 = struct.unpack_from("<6B", pdata, poff + 8)
                print(f"  poly {p:2d}: ptype={ptype:2d} tri tset={tset:2d} (perm={tset in perm_tsets}) tid={tid:2d} UV: ({u0:3d},{v0:3d})-({u1:3d},{v1:3d})-({u2:3d},{v2:3d})")
            if ptype < len(poly_sizes) and poly_sizes[ptype] > 0:
                poff += poly_sizes[ptype]
            else:
                break

        # Inspect Tile Model 4
        tt = models[4]
        print(f"\n--- Inspecting Tile Model 4 (npolys={tt['npolys']}, nverts={tt['nverts']}, poff={tt['p_off']:#x}) ---")
        pdata = tt["mdata"]
        poff = tt["p_off"]
        for p in range(tt["npolys"]):
            ptype = pdata[poff] & 0x1F
            tset = pdata[poff + 1]
            tid = pdata[poff + 2]
            if ptype in (4, 5, 11, 19, 21):
                u0, v0, u1, v1, u2, v2, u3, v3 = struct.unpack_from("<8B", pdata, poff + 8)
                print(f"  poly {p:2d}: ptype={ptype:2d} tset={tset:2d} (perm={tset in perm_tsets}) tid={tid:2d} UV: ({u0:3d},{v0:3d})-({u1:3d},{v1:3d})-({u2:3d},{v2:3d})-({u3:3d},{v3:3d}) size={abs(u1-u0)}x{abs(v2-v0)}")
            elif ptype in (10, 20):
                u0, v0, u1, v1, u2, v2 = struct.unpack_from("<6B", pdata, poff + 8)
                print(f"  poly {p:2d}: ptype={ptype:2d} tri tset={tset:2d} (perm={tset in perm_tsets}) tid={tid:2d} UV: ({u0:3d},{v0:3d})-({u1:3d},{v1:3d})-({u2:3d},{v2:3d})")
            if ptype < len(poly_sizes) and poly_sizes[ptype] > 0:
                poff += poly_sizes[ptype]
            else:
                break

analyze()
