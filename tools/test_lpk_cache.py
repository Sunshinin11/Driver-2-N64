"""Testa o carregador LEVPACK real em WebAssembly, sem SDK N64.
Requer clang++ com alvo wasm32, lld e Node.js. Nao mede desempenho do N64.
Usa arquivos temporarios; valida cache, LZ4 sobreposto e falhas de leitura.
"""
from pathlib import Path
import argparse, shutil, subprocess, tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--clang', default=shutil.which('clang++'))
parser.add_argument('--lld', default=shutil.which('ld.lld'))
args = parser.parse_args()
if not args.clang or not args.lld or not shutil.which('node'):
    parser.error('Informe --clang e --lld; Node.js tambem e necessario.')
root = Path(__file__).resolve().parents[1]
source = (root/'port/n64/fs_n64.cpp').read_text(encoding='utf-8')
# Compila as mesmas definicoes e funcoes usadas no console, com I/O simulado.
source = source[source.index('#ifndef D2_LPK_CACHE_BLOCKS'):source.index('// arquivos de dados inteiros')]
source = source.replace('#include <libdragon.h>', '')
preamble = r'''
typedef unsigned int uint32_t; typedef unsigned short uint16_t;
typedef unsigned long size_t;
#define NULL 0
struct FILE { unsigned pos; }; FILE file;
unsigned char rom[65536]; unsigned romSize, reads; unsigned ticks;
unsigned get_ticks() { return ++ticks; }
#define debugf(...) ((void)0)
extern "C" void *memcpy(void *d,const void *s,size_t n) { auto a=(unsigned char*)d; auto b=(const unsigned char*)s; for(size_t i=0;i<n;i++) a[i]=b[i]; return d; }
extern "C" void *memset(void *d,int v,size_t n) { auto a=(unsigned char*)d; for(size_t i=0;i<n;i++) a[i]=(unsigned char)v; return d; }
extern "C" void *memmove(void *d,const void *s,size_t n) { auto a=(unsigned char*)d; auto b=(const unsigned char*)s; if(a<b) for(size_t i=0;i<n;i++) a[i]=b[i]; else for(size_t i=n;i>0;i--) a[i-1]=b[i-1]; return d; }
size_t strlen(const char *s) { size_t n=0; while(s[n]) n++; return n; }
#define SEEK_SET 0
int fseek(FILE *f,long p,int) { if(p<0 || (unsigned)p>romSize) return -1; f->pos=(unsigned)p; return 0; }
size_t fread(void *d,size_t s,size_t n,FILE *f) { reads++; unsigned bytes=(unsigned)(s*n); if(bytes>romSize-f->pos) bytes=romSize-f->pos; memcpy(d,rom+f->pos,bytes); f->pos+=bytes; return s?bytes/s:0; }
FILE *fopen(const char*,const char*) { return &file; }
'''
harness = r'''
void resetCache() { memset(lpkCache,0,sizeof(lpkCache)); reads=0;
#if D2_LPK_CACHE_BLOCKS > 1
lpkCacheClock=0;
#endif
}
bool check(const unsigned char *p,unsigned len,unsigned char value) { if(!p) return false; for(unsigned i=0;i<len;i++) if(p[i]!=value) return false; return true; }
extern "C" int run() {
    lpkFp=&file; lpkTried=lpkOk=true; lpkNBlocks=3; lpkNPool=17; lpkDataOff=0;
    for(unsigned i=0;i<16384;i++) rom[i]=0x41;
    unsigned q=16384; rom[q++]=0x1f; rom[q++]=0x42; rom[q++]=1; rom[q++]=0;
    unsigned extra=16383-19; while(extra>=255) { rom[q++]=255; extra-=255; } rom[q++]=(unsigned char)extra;
    unsigned last=q; for(unsigned i=0;i<2048;i++) rom[q++]=0x43; romSize=q;
    lpkBlocks[0]=0x80000000u; lpkBlocks[1]=16384; lpkBlocks[2]=0x80000000u|last; lpkBlocks[3]=q;
    resetCache();
    if(!check(lpk_block(0),16384,0x41)) return 1;
    if(!check(lpk_block(1),16384,0x42)) return 2;
    if(!check(lpk_block(0),16384,0x41)) return 3;
    if(reads!=(D2_LPK_CACHE_BLOCKS==1?3u:2u)) return 4;
    if(!check(lpk_block(2),2048,0x43)) return 5;
    if(!check(lpk_block(0),16384,0x41)) return 6;
    if(reads!=(D2_LPK_CACHE_BLOCKS==1?5u:3u)) return 7;
    if(lpk_block(3)!=NULL) return 8;
    // Leitura de 4 KB atravessando setores mapeados para blocos diferentes.
    LpkFile f={}; f.size=4096; f.nsec=2; lpkMapFile=&f; lpkMapBuf[0]=0; lpkMapBuf[1]=8;
    unsigned char out[4096];
    if(lpk_read(&f,0,out,4096)!=4096 || !check(out,2048,0x41) || !check(out+2048,2048,0x42)) return 9;
    if(lpk_read(&f,4096,out,1)!=0 || lpk_read(&f,-1,out,1)!=0) return 10;
    // Falha nao pode publicar um bloco incompleto como acerto de cache.
    resetCache(); unsigned saved=romSize; romSize=last+100;
    if(lpk_block(2)!=NULL) return 11;
    romSize=saved; if(!check(lpk_block(2),2048,0x43)) return 12;
    resetCache(); rom[16384+2]=0; // offset LZ4 zero: invalido
    if(lpk_block(1)!=NULL) return 13;
    rom[16384+2]=1; if(!check(lpk_block(1),16384,0x42)) return 14;
#if D2_LPK_PROFILE
    if(!lpkStats.hits || !lpkStats.misses || !lpkStats.errors || !lpkStats.decoded || !lpkStats.ioTicks || !lpkStats.decodeTicks) return 15;
    d2_lpk_report(); if(lpkStats.hits || lpkStats.misses || lpkStats.errors) return 16;
#endif
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='d2-lpk-test-') as tmp:
    tmp=Path(tmp); cpp=tmp/'test.cpp'; cpp.write_text(preamble+source+harness,encoding='utf-8')
    for count in (1,2,3,4):
        for profile in (0,1):
            obj=tmp/'test.o'; wasm=tmp/'test.wasm'
            subprocess.run([args.clang,'--target=wasm32','-std=c++14','-O2','-ffreestanding','-fno-builtin','-fno-exceptions','-fno-rtti',f'-DD2_LPK_CACHE_BLOCKS={count}',f'-DD2_LPK_PROFILE={profile}','-c',str(cpp),'-o',str(obj)],check=True)
            subprocess.run([args.lld,'-flavor','wasm','--no-entry','--export=run',str(obj),'-o',str(wasm)],check=True)
            subprocess.run(['node','-e',"const fs=require('fs'); WebAssembly.instantiate(fs.readFileSync(process.argv[1])).then(({instance})=>{const r=instance.exports.run(); if(r) {console.error('failure',r);process.exit(1)}})",str(wasm)],check=True)
            print(f'PASS: blocks={count}, profile={profile}',flush=True)
