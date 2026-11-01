#!/usr/bin/env python3
import struct

spool_off = 1040384
tpage5_off = spool_off + 2614 * 2048

with open('filesystem_d2/DRIVER2/LEVELS/CHICAGO.LEV', 'rb') as f:
    f.seek(tpage5_off)
    npal = struct.unpack('<I', f.read(4))[0]
    print(f"TPage 5 at off={tpage5_off}: npalettes={npal}")
    if npal > 0 and npal <= 64:
        pal_data = f.read(npal * 32)
        print(f"Read {len(pal_data)} bytes of palette data ({npal} palettes)")
        # RLE segue as paletas!
        rle_sample = f.read(32)
        print(f"First 16 bytes of RLE: {list(rle_sample[:16])}")
