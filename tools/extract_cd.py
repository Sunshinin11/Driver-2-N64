#!/usr/bin/env python3
"""Extrai do CD (.bin, ISO9660 Mode2/2352 ou 2048) os arquivos que o jogo precisa para filesystem_d2/DRIVER2/...  (vao para o DragonFS da ROM).
Extrai os dados do jogo e, opcionalmente, converte XA e FMV em arquivos nativos para o cartucho.
Uso: python3 tools/extract_cd.py --cd1 Driver2CD1.bin --out filesystem_d2 [--cities CHICAGO,HAVANA] [--lang-from <pasta com os .LTXT>]"""
import argparse, os, shutil, struct, sys
ap = argparse.ArgumentParser()
ap.add_argument("--cd1", required=True)
ap.add_argument("--out", required=True)
ap.add_argument("--cities", default="CHICAGO")
ap.add_argument("--lang-from", default="")
ap.add_argument("--media-only", action="store_true")
ap.add_argument("--xa-out", help="convert XA speech to WAV64 in this staging directory")
ap.add_argument("--fmv-out", help="convert selected cinematics into this staging directory")
ap.add_argument("--fmv-ids", default="all", help="comma-separated render IDs or all")
ap.add_argument("--cd2", help="optional raw CD2 image for additional cinematics")
a = ap.parse_args()
cities = set(c.strip().upper() for c in a.cities.split(",") if c.strip())
SKIP_DIRS = {"FMV", "XA"}
CITY_DIRS = {"LEVELS", "NLEVELS", "MLEVELS", "MNLEVELS"}
f = open(a.cd1, "rb")
size = os.path.getsize(a.cd1)
if size % 2352 == 0: SS, OFF = 2352, 24
elif size % 2048 == 0: SS, OFF = 2048, 0
else: sys.exit("tamanho de imagem inesperado")
def sec(n):
    f.seek(n * SS + OFF); return f.read(2048)
def walk(lba, sz, path, out):
    data = b"".join(sec(lba + i) for i in range((sz + 2047) // 2048))
    o = 0
    while o < len(data):
        l = data[o]
        if l == 0:
            o = (o // 2048 + 1) * 2048; continue
        elba = struct.unpack("<I", data[o+2:o+6])[0]; esz = struct.unpack("<I", data[o+10:o+14])[0]
        fl = data[o+25]; nl = data[o+32]; name = data[o+33:o+33+nl].decode("latin1")
        o += l
        if name in ("\x00", "\x01"): continue
        name = name.split(";")[0]
        if fl & 2: walk(elba, esz, path + name + "/", out)
        else: out.append((path + name, elba, esz))
def scan_index():
    pvd = sec(16)
    if pvd[1:6] != b"CD001":
        sys.exit("nao parece um CD ISO9660")
    root = pvd[156:190]
    entries = []
    walk(struct.unpack_from("<I", root, 2)[0], struct.unpack_from("<I", root, 10)[0], "", entries)
    return entries

files = scan_index()
if a.xa_out:
    from convert_xa_wav64 import convert_xa
    convert_xa(f, SS, files, a.xa_out)
if a.fmv_out:
    from convert_fmv_n64 import convert_fmv
    selection = None if a.fmv_ids == "all" else {int(n) for n in a.fmv_ids.split(",")}
    converted = convert_fmv(f, SS, files, a.fmv_out, selection)
    if a.cd2:
        cd1, cd1_sector, cd1_offset = f, SS, OFF
        with open(a.cd2, "rb") as f:
            SS, OFF = 2352, 24
            pending = None if selection is None else selection - converted
            converted |= convert_fmv(f, SS, scan_index(), a.fmv_out, pending)
        f, SS, OFF = cd1, cd1_sector, cd1_offset
    if selection is not None and selection - converted:
        sys.exit(f"FMV render IDs missing from the supplied discs: {sorted(selection - converted)}")
    print(f"FMV64: {len(converted)} videos")
if a.media_only:
    sys.exit(0)
n = tot = 0
for path, lba, sz in files:
    parts = path.split("/")
    if len(parts) == 1: continue  # SLUS_xxx.xx, SYSTEM.CNF: executavel do PS1, inutil no N64
    if len(parts) > 1 and parts[1] in SKIP_DIRS: continue
    if parts[-1].upper() == "FMV.EXE": continue
    if len(parts) > 2 and parts[1] in CITY_DIRS:
        if os.path.splitext(parts[2])[0].upper() not in cities: continue
    dst = os.path.join(a.out, *parts)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if os.path.exists(dst) and os.path.getsize(dst) == sz: n += 1; tot += sz; continue
    with open(dst, "wb") as w:
        left = sz; s = lba
        while left > 0:
            w.write(sec(s)[:min(left, 2048)]); left -= 2048; s += 1
    n += 1; tot += sz
if a.lang_from and os.path.isdir(a.lang_from):
    d = os.path.join(a.out, "DRIVER2", "LANG"); os.makedirs(d, exist_ok=True)
    for fn in os.listdir(a.lang_from):
        if fn.upper().endswith(".LTXT"): shutil.copy(os.path.join(a.lang_from, fn), d)
for stale in ("SLUS_011.61", "SYSTEM.CNF", "README.txt"):
    q = os.path.join(a.out, stale)
    if os.path.exists(q): os.remove(q)
print("extraidos: %d arquivos, %.1f MB (cidades: %s)" % (n, tot / 1048576.0, ",".join(sorted(cities))))
