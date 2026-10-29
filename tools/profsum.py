#!/usr/bin/env python3
"""Agrega /tmp/prof.txt (pc contagem) por funcao usando tools/addr2sym.py. Uso: profsum.py [mapfile] [topN]"""
import subprocess, collections, sys, re
mp = sys.argv[1] if len(sys.argv) > 1 else 'build_d2/d2_n64.map'
top = int(sys.argv[2]) if len(sys.argv) > 2 else 40
prof = [l.split() for l in open('/tmp/prof.txt')]
cnt = {p[0]: int(p[1]) for p in prof}
tot = sum(cnt.values())
out = subprocess.run(['python3', 'tools/addr2sym.py', mp] + ['0x' + a for a in cnt], capture_output=True, text=True).stdout.strip().split('\n')
agg = collections.Counter()
for a, l in zip(cnt, out):
    m = re.match(r'0x\w+ -> (.+?) \+0x', l)
    agg[m.group(1) if m else '?'] += cnt[a]
print('amostras', tot)
for n, c in agg.most_common(top): print('%5.1f%% %s' % (100.0 * c / tot, n))
