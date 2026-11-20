#!/usr/bin/env python3
"""Perfil por amostragem (D2_SAMPLE_PROF) agrupado por arquivo-fonte (via addr2line do ELF do mesmo build, com -g).
Uso: python3 tools/profsum_arquivos.py <elf> <log> [n]
Mostra a porcentagem de amostras por arquivo e as funcoes mais quentes de cada um dos maiores."""
import sys, re, bisect, subprocess, collections, os
elf, log = sys.argv[1], sys.argv[2]
top = int(sys.argv[3]) if len(sys.argv) > 3 else 30
NM = '/opt/libdragon/bin/mips64-elf-nm'; A2L = '/opt/libdragon/bin/mips64-elf-addr2line'
syms = []
for l in subprocess.run([NM, '-S', '-n', '-C', elf], capture_output=True, text=True).stdout.split('\n'):
    p = l.split(None, 3)
    if len(p) == 4 and p[2] in 'TtWw': syms.append((int(p[0], 16) & 0xFFFFFFFF, int(p[1], 16), p[3]))
syms.sort(); addrs = [s[0] for s in syms]
rx = re.compile(r'^\[prof\] ([0-9a-f]{8}) (\d+)\s*$')
byfn = collections.Counter(); fnaddr = {}
tot = 0
for l in open(log, encoding='utf-8', errors='replace'):
    m = rx.match(l)
    if not m: continue
    a, c = int(m.group(1), 16), int(m.group(2)); tot += c
    i = bisect.bisect_right(addrs, a) - 1
    if i >= 0 and a < syms[i][0] + max(syms[i][1], 32):
        byfn[syms[i][2]] += c; fnaddr[syms[i][2]] = syms[i][0]
    else:
        byfn['?'] += c
names = [n for n in byfn if n in fnaddr]
out = subprocess.run([A2L, '-e', elf] + ['%x' % fnaddr[n] for n in names], capture_output=True, text=True).stdout.split('\n')
fnfile = {}
for n, f in zip(names, out):
    f = f.split(':')[0]
    if 'libdragon' in f or f.startswith('/opt') or '/src/' in f and 'D2_N64' not in f: f = 'libdragon/' + os.path.basename(f)
    else: f = os.path.basename(f) if f and f != '??' else '?'
    fnfile[n] = f
byfile = collections.Counter(); fns = collections.defaultdict(list)
for n, c in byfn.items():
    f = fnfile.get(n, '?'); byfile[f] += c; fns[f].append((c, n))
print('amostras=%d' % tot)
for f, c in byfile.most_common(top):
    best = ', '.join('%s %.1f' % (n.split('(')[0][:28], 100 * k / tot) for k, n in sorted(fns[f], reverse=True)[:4])
    print('%6.2f%%  %-22s %s' % (100 * c / tot, f[:22], best))
