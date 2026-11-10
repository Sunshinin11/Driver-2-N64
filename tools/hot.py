#!/usr/bin/env python3
# gera a lista de funcoes quentes (ordenadas por amostras do profiler) para --section-ordering-file
import sys,subprocess,bisect
elf, prof, out, budget = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
syms=[]
for l in subprocess.run(['/opt/libdragon/bin/mips64-elf-nm','-S','--size-sort',elf],capture_output=True,text=True).stdout.split('\n'):
    p=l.split()
    if len(p)==4 and p[2] in 'Tt': syms.append((int(p[0],16),int(p[1],16),p[3]))
syms.sort(); addrs=[s[0] for s in syms]
cnt={}
for l in open(prof):
    a,c=l.split(); a=int(a,16); i=bisect.bisect_right(addrs,a)-1
    if i>=0 and a<syms[i][0]+syms[i][1]: cnt[syms[i][2]]=cnt.get(syms[i][2],0)+int(c)
size={s[2]:s[1] for s in syms}
tot=sum(cnt.values()); acc=0; used=0; names=[]
for n,c in sorted(cnt.items(),key=lambda x:-x[1]):
    if used+size[n]>budget: continue
    names.append(n); used+=size[n]; acc+=c
print('funcoes=%d bytes=%d cobertura=%.1f%%'%(len(names),used,100*acc/tot))
with open(out,'w') as f:
    f.write('.text : {\n')
    for n in names: f.write('  *(.text.%s)\n'%n)
    f.write('}\n')
