#!/usr/bin/env bash
for r in "$@"; do python3 - $r <<'PY'
import sys,re
r=sys.argv[1]; v=[int(m.group(1)) for l in open('/opt/b_%s/run.log'%r,errors='ignore') for m in [re.match(r'\[cyc\] (\d+)',l)] if m]
i=next(k for k,x in enumerate(v) if x>5000000 and k>3)
w=v[i+1:i+5]; print('%-6s janela alinhada: %s  media=%.2fM (%.1f fps)'%(r,' '.join('%.2f'%(x/1e6) for x in w),sum(w)/len(w)/1e6, 46.875e6/(sum(w)/len(w))))
PY
done
