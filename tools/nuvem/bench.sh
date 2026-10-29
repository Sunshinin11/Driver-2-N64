#!/usr/bin/env bash
# bench.sh rom1 rom2 ...  -> media de ticks/quadro em jogo (linhas [cyc] apos o quadro 1500 do harness), 2 em paralelo
D2_DIR="${D2_DIR:-/opt/d2}"
RUN_SH="${RUN_SH:-$D2_DIR/run.sh}"
BIN_IN="${BIN:-in_bench.txt}"
[ -f "$BIN_IN" ] || BIN_IN="$D2_DIR/$BIN_IN"

cd "$D2_DIR"
for r in "$@"; do
  ( D2_CAP_FROM=99999 "$RUN_SH" "$r.z64" "${BF:-3000}" 0 "$BIN_IN" "$D2_DIR/b_$r" >/dev/null ) &
  while [ $(jobs -r | wc -l) -ge 2 ]; do sleep 1; done
done
wait
for r in "$@"; do
  python3 - "$r" "$D2_DIR" <<'PY'
import sys,re,os
r=sys.argv[1]; d2=sys.argv[2]
p=os.path.join(d2, 'b_%s' % r, 'run.log')
L=open(p, errors='ignore').read().split('\n') if os.path.exists(p) else []
v=[int(m.group(1)) for l in L for m in [re.match(r'\[cyc\] (\d+)',l)] if m]
g=[x for x in v if x>4000000][2:]   # em jogo (descarta os 2 primeiros)
import statistics
print('%-8s n=%2d media=%.2fM min=%.2fM max=%.2fM'%(r,len(g),statistics.mean(g)/1e6,min(g)/1e6,max(g)/1e6) if g else r+' sem dados')
PY
done
