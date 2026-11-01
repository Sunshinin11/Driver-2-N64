#!/usr/bin/env python3
import struct

with open('filesystem_d2/DRIVER2/LEVELS/CHICAGO.LEV', 'rb') as f:
    f.seek(8)
    citylump_data = f.read(32)
    lumps = [struct.unpack_from('<ii', citylump_data, i * 8) for i in range(4)]
    for idx in [0, 2]: # DATA1 e DATA2
        off, sz = lumps[idx]
        print(f"Lump {idx}: off={off}, sz={sz}")
        f.seek(off + 8)
        end = off + sz
        while f.tell() + 8 <= end:
            hdr = f.read(8)
            lt, lsz = struct.unpack('<II', hdr)
            print(f"  sub-lump: type={lt}, sz={lsz} at off={f.tell()-8}")
            f.seek(lsz + ((4 - (lsz % 4)) % 4), 1)
