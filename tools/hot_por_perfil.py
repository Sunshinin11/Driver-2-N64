#!/usr/bin/env python3
"""hot.ld a partir do perfil por amostragem (D2_SAMPLE_PROF): as funcoes mais quentes em ordem decrescente de amostras,
contiguas no inicio do .text, ate o orcamento de bytes. Objetivo: layout ESTAVEL (mudar codigo frio nao desloca o quente)
e as mais quentes juntas nos primeiros 16 KiB do I-cache. Sem espacos entre funcoes.
Uso: python3 tools/hot_por_perfil.py <elf> <perfil.txt "endereco amostras"> <saida hot.ld> [orcamento_bytes=65536]"""
import sys, bisect, subprocess, collections
elf, prof, out = sys.argv[1:4]
budget = int(sys.argv[4]) if len(sys.argv) > 4 else 65536
NM = '/opt/libdragon/bin/mips64-elf-nm'
syms = []
for l in subprocess.run([NM, '-S', '-n', elf], capture_output=True, text=True).stdout.split('\n'):
    p = l.split()
    if len(p) == 4 and p[2] in 'TtWw': syms.append((int(p[0], 16) & 0xFFFFFFFF, int(p[1], 16), p[3]))
syms.sort(); addrs = [s[0] for s in syms]
cnt = collections.Counter(); size = {}
tot = 0
for l in open(prof):
    a, c = l.split(); a = int(a, 16); c = int(c); tot += c
    i = bisect.bisect_right(addrs, a) - 1
    if i >= 0 and a < syms[i][0] + syms[i][1]:
        cnt[syms[i][2]] += c; size[syms[i][2]] = syms[i][1]
used = acc = 0; names = []
for n, c in cnt.most_common():
    sz = (size[n] + 7) & ~7
    if used + sz > budget: continue
    names.append(n); used += sz; acc += c
with open(out, 'w') as f:
    f.write('.text : {\n')
    for n in names: f.write('  *(.text.%s)\n' % n)
    f.write('}\n')
print('funcoes=%d bytes=%d cobertura=%.1f%%' % (len(names), used, 100 * acc / tot))
