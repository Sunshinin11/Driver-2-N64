#!/usr/bin/env python3
import struct

with open('filesystem_d2/DRIVER2/LEVELS/CHICAGO.LEV', 'rb') as f:
    f.seek(957116 + 8) # pula cabecalho do sub-lump
    buf = f.read(3084)

ptr = 0
size_2048 = struct.unpack_from('<I', buf, ptr)[0] * 2048
ptr += 4
music_amb_sz = struct.unpack_from('<I', buf, ptr)[0]
ptr += 4 + music_amb_sz
num_areas = struct.unpack_from('<I', buf, ptr)[0]
ptr += 4
print(f"NumAreas: {num_areas}")
areas = []
for a in range(num_areas):
    gfx_off, mdl_off, mus_off, amb_off, mdl_sz, pad, num_tp, amb_sz, mus_sz, mus_smp_sz, mus_id, amb_id = struct.unpack_from('<HHHHBBBBBBBB', buf, ptr)
    areas.append({
        'area': a, 'gfx_off': gfx_off, 'mdl_off': mdl_off, 'num_tp': num_tp
    })
    ptr += 16

for a in range(num_areas):
    tps = list(buf[ptr : ptr + 16])
    ptr += 16
    print(f"Area {a:2d}: gfx_off={areas[a]['gfx_off']:5d} (sector {areas[a]['gfx_off']:5d}), num_tp={areas[a]['num_tp']}, tpages={tps[:areas[a]['num_tp']]}")
