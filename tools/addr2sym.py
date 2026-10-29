#!/usr/bin/env python3
"""Procura o nome da funcao de um endereco usando o .map do build (build_d2/d2_n64.map), inclusive funcoes 'static'
(secoes .text.<nome>).  Uso: addr2sym.py mapfile 0x800173bc ..."""
import re, sys, bisect, subprocess
syms = {}
lines = open(sys.argv[1], errors="replace").read().split("\n")
i = 0
while i < len(lines):
    l = lines[i]
    m = re.match(r"^ \.text(?:\.[\w$.]+)?\s*$", l)         # nome longo: endereco na linha seguinte
    m2 = re.match(r"^ (\.text\.[^\s]+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)", l)
    if m2:
        a = int(m2.group(2), 16) & 0xffffffff
        syms[a] = (m2.group(1)[6:], int(m2.group(3), 16), m2.group(4))
    elif m and i + 1 < len(lines):
        n = re.match(r"^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)", lines[i + 1])
        if n:
            a = int(n.group(1), 16) & 0xffffffff
            syms[a] = (l.strip()[6:], int(n.group(2), 16), n.group(3))
    m3 = re.match(r"^\s+0x([0-9a-f]{8,16})\s+([A-Za-z_][\w.$]*)\s*$", l)
    if m3:
        a = int(m3.group(1), 16) & 0xffffffff
        if 0x80000000 <= a < 0x80800000 and a not in syms: syms[a] = (m3.group(2), 0, "")
    i += 1
ks = sorted(syms)
def dem(n):
    try: return subprocess.run(["c++filt", n], capture_output=True, text=True).stdout.strip() or n
    except Exception: return n
for arg in sys.argv[2:]:
    a = int(arg, 16) & 0xffffffff
    j = bisect.bisect_right(ks, a) - 1
    if j < 0: print(arg, "?"); continue
    n, sz, obj = syms[ks[j]]
    print("%s -> %s +0x%x  (%s)" % (arg, dem(n), a - ks[j], obj.split("/")[-1]))
