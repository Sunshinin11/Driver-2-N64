#!/usr/bin/env python3
"""Resumo por funcao do perfil por amostragem (D2_SAMPLE_PROF).
Uso: python3 tools/profsum_amostras.py <elf> <log> [saida_perfil.txt] [n]
Extrai as linhas "[prof] <endereco> <amostras>" do log, agrega por simbolo do ELF (mesmo build) e imprime as n maiores.
Se saida_perfil.txt for dado, grava "endereco amostras" (formato de tools/hot_layout.py)."""
import sys, re, bisect, subprocess, collections
elf, log = sys.argv[1], sys.argv[2]
out = sys.argv[3] if len(sys.argv) > 3 and sys.argv[3] != '-' else None
top = int(sys.argv[4]) if len(sys.argv) > 4 else 40
NM = '/opt/libdragon/bin/mips64-elf-nm'
syms = []
for l in subprocess.run([NM, '-S', '-n', '-C', elf], capture_output=True, text=True).stdout.split('\n'):
    p = l.split(None, 3)
    if len(p) == 4 and p[2] in 'TtWw':
        syms.append((int(p[0], 16) & 0xFFFFFFFF, int(p[1], 16), p[3]))
syms.sort(); addrs = [s[0] for s in syms]
rx = re.compile(r'^\[prof\] ([0-9a-f]{8}) (\d+)\s*$')
samples = []
for l in open(log, encoding='utf-8', errors='replace'):
    m = rx.match(l)
    if m: samples.append((int(m.group(1), 16), int(m.group(2))))
tot = sum(c for _, c in samples)
by = collections.Counter()
for a, c in samples:
    i = bisect.bisect_right(addrs, a) - 1
    name = syms[i][2] if i >= 0 and a < syms[i][0] + max(syms[i][1], 32) else '?%08x' % a
    by[name] += c
print('amostras=%d funcoes=%d' % (tot, len(by)))
acc = 0
for n, c in by.most_common(top):
    acc += c
    print('%6.2f%% %6.2f%%  %s' % (100 * c / tot, 100 * acc / tot, n[:110]))
if out:
    with open(out, 'w') as f:
        for a, c in samples: f.write('%08x %d\n' % (a, c))
