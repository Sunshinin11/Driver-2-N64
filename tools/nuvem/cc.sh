#!/usr/bin/env bash
# cc.sh rom1 rom2 ... : ticks/quadro medios nas janelas cc=CC0..CC1 do jogo (mesmo estado do jogo em todas as versoes)
D2_DIR="${D2_DIR:-/opt/d2}"
CC0=${CC0:-96}; CC1=${CC1:-320}
for r in "$@"; do python3 - "$r" "$CC0" "$CC1" "$D2_DIR" <<'PY'
import sys,re,os
r,a,b,d2=sys.argv[1],int(sys.argv[2]),int(sys.argv[3]),sys.argv[4]
d={}
p=os.path.join(d2, 'b_%s' % r, 'run.log')
if os.path.exists(p):
    for l in open(p, errors='ignore'):
        m=re.match(r'\[bench\] cc=(\d+) ticks=(\d+)',l)
        if m: d[int(m.group(1))]=int(m.group(2))
w=[d[k] for k in sorted(d) if a<=k<=b]
print('%-6s cc %d..%d n=%d  media=%.3fM (%.1f fps)  max=%.2fM'%(r,a,b,len(w),sum(w)/len(w)/1e6,46.875e6/(sum(w)/len(w)),max(w)/1e6) if w else r+' sem dados (cc max=%s)'%(max(d) if d else '-'))
PY
done
