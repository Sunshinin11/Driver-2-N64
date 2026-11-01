#!/usr/bin/env python3
import struct

with open('filesystem_d2/DRIVER2/LEVELS/CHICAGO.LEV', 'rb') as f:
    f.seek(8 + 8)
    tpage_off, tpage_sz = struct.unpack('<ii', f.read(8))
    f.seek(tpage_off)
    npal = struct.unpack('<I', f.read(4))[0]
    print(f'npalettes in perm tpage 0: {npal}')
    pal_data = f.read(npal * 32)
    # cada paleta tem 32 bytes (16 cores de 16 bits BGR555)
    print(f'Total pal_data size: {len(pal_data)} bytes (npal * 16 cores * 2 bytes = {npal * 32})')
    # O restante é RLE dos pixels
    rem = tpage_sz - 4 - npal * 32
    print(f'Remaining RLE data: {rem} bytes')

