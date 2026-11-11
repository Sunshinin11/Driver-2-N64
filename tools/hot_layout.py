#!/usr/bin/env python3
"""Layout das funcoes quentes sem conflito no cache de instrucoes do VR4300 (16 KB, mapeamento direto, linhas de 32 bytes).
Entrada: ELF e perfil (PC ciclos) do MESMO build. Saida: hot.ld (ordem) + port/n64/d2_hotpad.cpp (espacos entre funcoes).
Escolhe ordem e espacos (multiplos de 32 bytes) por recozimento simulado, minimizando o peso das linhas que caem no mesmo
conjunto do cache (soma - maior), usando as linhas realmente executadas de cada funcao (ciclos do perfil por instrucao)."""
import sys, subprocess, bisect, random, math, collections
elf, prof, hot_out, pad_out = sys.argv[1:5]
budget = int(sys.argv[5]) if len(sys.argv) > 5 else 28000       # bytes das funcoes quentes
maxpad = int(sys.argv[6]) if len(sys.argv) > 6 else 24576       # bytes totais de espaco permitidos
seed = int(sys.argv[7]) if len(sys.argv) > 7 else 1
random.seed(seed)
NM = '/opt/libdragon/bin/mips64-elf-nm'
syms = []
for l in subprocess.run([NM, '-S', '-n', elf], capture_output=True, text=True).stdout.split('\n'):
    p = l.split()
    if len(p) == 4 and p[2] in 'Tt': syms.append((int(p[0], 16) & 0xFFFFFFFF, int(p[1], 16), p[3]))
syms.sort(); addrs = [s[0] for s in syms]
instr = collections.defaultdict(list)       # funcao -> [(offset, ciclos)]
tot = 0
for l in open(prof):
    a, c = l.split(); a = int(a, 16) & 0xFFFFFFFF; c = int(c); tot += c
    i = bisect.bisect_right(addrs, a) - 1
    if i >= 0 and a < syms[i][0] + syms[i][1]: instr[syms[i][2]].append((a - syms[i][0], c))
size = {s[2]: s[1] for s in syms}
cnt = {n: sum(c for _, c in v) for n, v in instr.items()}
names = []; used = 0; acc = 0
for n, c in sorted(cnt.items(), key=lambda x: -x[1]):
    sz = (size[n] + 31) & ~31
    if used + sz > budget or n.startswith('.') : continue
    names.append(n); used += sz; acc += c
print('funcoes=%d bytes=%d cobertura=%.1f%%' % (len(names), used, 100 * acc / tot))
# peso por linha de 32 bytes dentro de cada funcao (so as quentes: executadas de fato)
W = {}
for n in names:
    lw = collections.Counter()
    for off, c in instr[n]: lw[off >> 5] += c
    W[n] = list(lw.items())
SZ = {n: (size[n] + 31) & ~31 for n in names}
BASE = 0x400 >> 5
def cost(order, gaps):
    sets = collections.defaultdict(lambda: [0, 0])
    pos = BASE
    for n, g in zip(order, gaps):
        pos += g >> 5
        for li, w in W[n]:
            s = sets[(pos + li) & 511]; s[0] += w; s[1] = max(s[1], w)
        pos += SZ[n] >> 5
    return sum(a - b for a, b in sets.values()) + 0.0
order = list(names); gaps = [0] * len(order)
cur = cost(order, gaps); best = (cur, order[:], gaps[:]); T0 = cur / 20 + 1
print('custo inicial %.0f (%.1f%% do total)' % (cur, 100 * cur / tot))
N = 60000
for it in range(N):
    T = T0 * (1 - it / N) + 1
    o2 = order[:]; g2 = gaps[:]
    r = random.random()
    if r < 0.4:
        i, j = random.randrange(len(o2)), random.randrange(len(o2)); o2[i], o2[j] = o2[j], o2[i]
    elif r < 0.6:
        i = random.randrange(len(o2)); x = o2.pop(i); j = random.randrange(len(o2) + 1); o2.insert(j, x)
    else:
        i = random.randrange(len(g2)); g2[i] = random.choice((0, 0, 32 * random.randrange(0, 512)))
    if sum(g2) > maxpad: continue
    c = cost(o2, g2)
    if c < cur or random.random() < math.exp((cur - c) / T):
        order, gaps, cur = o2, g2, c
        if c < best[0]: best = (c, o2[:], g2[:])
c, order, gaps = best
print('custo final %.0f (%.1f%% do total) espacos=%d bytes' % (c, 100 * c / tot, sum(gaps)))
with open(hot_out, 'w') as f:
    f.write('.text : {\n')
    for k, (n, g) in enumerate(zip(order, gaps)):
        if g: f.write('  *(.text.d2pad%d)\n' % k)
        f.write('  *(.text.%s)\n' % n)
    f.write('}\n')
with open(pad_out, 'w') as f:
    f.write('// gerado por tools/hot_layout.py: espacos entre funcoes quentes (evita conflitos no cache de instrucoes)\n')
    for k, g in enumerate(gaps):
        if g: f.write('__asm__(".section .text.d2pad%d,\\"ax\\",@progbits\\n.skip %d\\n.text\\n");\n' % (k, g))
