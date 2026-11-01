#!/usr/bin/env python3
import struct

with open('filesystem_d2/DRIVER2/LEVELS/CHICAGO.LEV', 'rb') as f:
    f.seek(8)
    citylump_data = f.read(32)
    lumps = [struct.unpack_from('<ii', citylump_data, i * 8) for i in range(4)]
    data1_off, data1_sz = lumps[0]
    
    f.seek(data1_off + 8)
    end_off = data1_off + data1_sz
    while f.tell() < end_off:
        hdr = f.read(8)
        if len(hdr) < 8:
            break
        lt, lsz = struct.unpack('<II', hdr)
        buf = f.read(lsz)
        if lt == 34: # LUMP_TEXTUREINFO
            print(f'Found LUMP_TEXTUREINFO, sz={lsz}')
            tp_amt = struct.unpack_from('<I', buf, 0)[0]
            texamt_tot = struct.unpack_from('<I', buf, 4)[0]
            print(f'tp_amt: {tp_amt}, texamt_tot: {texamt_tot}')
            ptr = 8 + (tp_amt + 1) * 8
            tpage_details = {}
            for i in range(tp_amt):
                texamt = struct.unpack_from('<I', buf, ptr)[0]
                ptr += 4
                subtexs = []
                for j in range(texamt):
                    tid, noff, x, y, w, h = struct.unpack_from('<HHBBBB', buf, ptr + j * 8)
                    subtexs.append({'id': tid, 'nameoff': noff, 'x': x, 'y': y, 'w': w, 'h': h})
                tpage_details[i] = subtexs
                ptr += texamt * 8
            nperms = struct.unpack_from('<I', buf, ptr)[0]
            print(f'nperms: {nperms}')
            permlist = []
            for i in range(nperms):
                tp_id, tp_sz = struct.unpack_from('<ii', buf, ptr + 4 + i * 8)
                permlist.append((tp_id, tp_sz))
                print(f'  perm[{i}]: tpage_id={tp_id}, sz={tp_sz} (sectors: {(tp_sz+2047)//2048})')
            
            ptr += 4 + 16 * 8
            nspec = struct.unpack_from('<I', buf, ptr)[0]
            print(f'nspecpages: {nspec}')
            speclist = []
            for i in range(nspec):
                tp_id, tp_sz = struct.unpack_from('<ii', buf, ptr + 4 + i * 8)
                speclist.append((tp_id, tp_sz))
                print(f'  spec[{i}]: tpage_id={tp_id}, sz={tp_sz} (sectors: {(tp_sz+2047)//2048})')
            
            # Imprimir detalhes de tset=0 e tset=5 (usados nos modelos 234, 235, 83, 108)
            for tset in [0, 5, 48]:
                if tset in tpage_details:
                    print(f"\n--- Subtextures in tset={tset} (count={len(tpage_details[tset])}) ---")
                    for sub in tpage_details[tset]:
                        print(f"  tid={sub['id']}: x={sub['x']}, y={sub['y']}, w={sub['w']}, h={sub['h']}")
            break
        rem = (4 - (lsz % 4)) % 4
        if rem:
            f.read(rem)
