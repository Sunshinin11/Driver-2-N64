#!/usr/bin/env python3
import struct

with open('filesystem_d2/DRIVER2/LEVELS/CHICAGO.LEV', 'rb') as f:
    lev_data = f.read()

print(f"Total CHICAGO.LEV size: {len(lev_data)} bytes")

# Procurar onde o spool lump ou as áreas definem gfx_offset
# Vamos verificar o lump das áreas (LUMP_AREAS ou similar em DATA1 ou DATA2)
# Vamos ler os lumps de DATA1 e DATA2
f_off = 8
citylump_data = lev_data[8:40]
lumps = [struct.unpack_from('<ii', citylump_data, i * 8) for i in range(4)]
print("Lumps:", lumps)

# Lump 3 é SPOOL: lumps[3]
spool_off, spool_sz = lumps[3]
print(f"Spool lump: offset={spool_off}, size={spool_sz}")

