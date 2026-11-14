"""Instrumenta somente uma arvore isolada preparada por prepare_ab_a30b79d_completo."""
from pathlib import Path
import argparse,json,hashlib

def instrument(root):
    manifest_path=root/'AB_CONTENT_MANIFEST.json'
    manifest=json.loads(manifest_path.read_text())
    if manifest['base']!='a30b79da425814c27c1a5405fdb670ac32d69d6c': raise RuntimeError('Base inesperada')
    if (root/'port/n64/d2_content_diag.h').exists(): raise RuntimeError('Arvore ja instrumentada')
    def read(name):return (root/name).read_text()
    def write(name,s):(root/name).write_text(s,encoding='utf-8',newline='\n')
    def replace(name,old,new):
        s=read(name)
        if s.count(old)!=1:raise RuntimeError('Ancora inesperada: '+name+' '+old[:80])
        write(name,s.replace(old,new,1))
    write('port/n64/d2_content_diag.h',r'''#pragma once
#include <stdint.h>
enum { DIAG_FS, DIAG_FIND, DIAG_MAP, DIAG_SEEK, DIAG_READ, DIAG_DECODE, DIAG_COPY, DIAG_CALLBACK, DIAG_MIX, DIAG_COUNT };
extern uint32_t d2_diag_ticks[DIAG_COUNT], d2_diag_calls[DIAG_COUNT];
static inline uint32_t d2_diag_now() { uint32_t t; __asm__ volatile("mfc0 %0,$9" : "=r"(t)); return t; }
struct D2ContentScope {
    int id; uint32_t start;
    explicit D2ContentScope(int id_) : id(id_), start(d2_diag_now()) { d2_diag_calls[id]++; }
    ~D2ContentScope() { d2_diag_ticks[id] += d2_diag_now()-start; }
};
extern "C" int d2_diag_xa_active(void);
extern "C" void d2_diag_frame(int frame);
''')
    write('port/n64/d2_content_diag.cpp',r'''#include <libdragon.h>
#include <string.h>
#include "d2_content_diag.h"
uint32_t d2_diag_ticks[DIAG_COUNT], d2_diag_calls[DIAG_COUNT];
extern "C" void d2_diag_frame(int frame) {
    static uint32_t last, frames, xaFrames;
    uint32_t now=d2_diag_now();
    if (!last) last=now;
    frames++; if (d2_diag_xa_active()) xaFrames++;
    if (frames<30) return;
    debugf("[content] f=%d n=%u span_ticks=%u fs_total=%u find=%u map=%u seek=%u read=%u decode=%u copy=%u\n",
        frame,frames,now-last,d2_diag_ticks[0],d2_diag_ticks[1],d2_diag_ticks[2],d2_diag_ticks[3],d2_diag_ticks[4],d2_diag_ticks[5],d2_diag_ticks[6]);
    debugf("[content_calls] fs=%u find=%u map=%u seek=%u read=%u decode=%u copy=%u\n",
        d2_diag_calls[0],d2_diag_calls[1],d2_diag_calls[2],d2_diag_calls[3],d2_diag_calls[4],d2_diag_calls[5],d2_diag_calls[6]);
    debugf("[content_audio] callback_ticks=%u mix_ticks=%u callback_calls=%u mix_calls=%u xa_frames=%u/%u\n",
        d2_diag_ticks[DIAG_CALLBACK],d2_diag_ticks[DIAG_MIX],d2_diag_calls[DIAG_CALLBACK],d2_diag_calls[DIAG_MIX],xaFrames,frames);
    memset(d2_diag_ticks,0,sizeof(d2_diag_ticks)); memset(d2_diag_calls,0,sizeof(d2_diag_calls));
    last=now; frames=xaFrames=0;
}
''')
    name='port/n64/fs_n64.cpp'
    s=read(name)
    s='#include "d2_content_diag.h"\n'+s
    # Wrappers defined before the LEVPACK namespace; the original calls use them.
    anchor='namespace {'
    wrappers='''static int d2_diag_seek(FILE *fp, long off, int whence) {
    D2ContentScope timer(DIAG_SEEK); return fseek(fp,off,whence);
}
static size_t d2_diag_read(void *dst, size_t size, size_t count, FILE *fp) {
    D2ContentScope timer(DIAG_READ); return fread(dst,size,count,fp);
}
'''
    s=s.replace('fseek(', 'd2_diag_seek(').replace('fread(', 'd2_diag_read(')
    s=s.replace(anchor,wrappers+anchor,1)
    scopes=[('bool lz4_decode(const unsigned char *src, int slen, unsigned char *dst, int dlen)','DIAG_DECODE'),
            ('bool lpk_use_map(const LpkFile *f)','DIAG_MAP'),
            ('const LpkFile *lpk_find(const char *path)','DIAG_FIND'),
            ('extern "C" int d2_pack_load(const char *path, char *dst, int offset, int size)','DIAG_FS'),
            ('extern "C" int d2_lev_header(const char *path, void *dst, int off, int n)','DIAG_FS'),
            ('int d2_read_sectors(char *dest, int sector, int nsectors)','DIAG_FS')]
    for signature,category in scopes:
        anchor=signature+'\n{'
        if s.count(anchor)!=1:raise RuntimeError('Funcao inesperada: '+signature)
        s=s.replace(anchor,anchor+'\n    D2ContentScope diagScope('+category+');',1)
    anchor='memcpy(out + done, blk + (pid % 8) * 2048 + so, chunk);'
    if s.count(anchor)!=1:raise RuntimeError('Copia nao encontrada')
    s=s.replace(anchor,'{ D2ContentScope copyScope(DIAG_COPY); '+anchor+' }')
    write(name,s)
    name='port/n64/psyx_backend.cpp';s=read(name)
    s='#include "d2_content_diag.h"\n'+s
    s=s.replace('    d2_sys_pump_vbl_cb();','    { D2ContentScope cbScope(DIAG_CALLBACK); d2_sys_pump_vbl_cb(); }')
    s=s.replace('mixer_try_play();','{ D2ContentScope mixScope(DIAG_MIX); mixer_try_play(); }')
    # Insert after the XA state is declared, inside the existing C linkage block.
    anchor='static float xa_gain = 1.0f;'
    if s.count(anchor)!=1:raise RuntimeError('Estado XA nao encontrado')
    s=s.replace(anchor,anchor+'\nextern "C" int d2_diag_xa_active(void) { return xa_wave && !xa_paused && mixer_ch_playing(XA_CHANNEL); }')
    write(name,s)
    name='port/n64/psyx_rdp.cpp';s=read(name)
    s='#include "d2_content_diag.h"\n'+s
    anchor='    s_scene = false;\n    D2DBG("[rdp] EndScene'
    if s.count(anchor)!=1:raise RuntimeError('EndScene nao encontrado')
    s=s.replace(anchor,'    s_scene = false;\n    d2_diag_frame(d2_rdp_frames);\n    D2DBG("[rdp] EndScene',1)
    write(name,s)
    replace('Makefile.d2','$(HOT_PAD)\n','$(HOT_PAD) port/n64/d2_content_diag.cpp\n')
    files=['Makefile.d2','port/n64/fs_n64.cpp','port/n64/psyx_backend.cpp','port/n64/psyx_rdp.cpp','port/n64/d2_content_diag.h','port/n64/d2_content_diag.cpp']
    manifest['diagnostic_files']={n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in files}
    manifest['diagnostic_notes']=['Timers inclusivos: fs_total inclui subtarefas; find inclui init; map inclui sua leitura. Nao somar categorias sobrepostas.',
        'mix_ticks mede tempo de CPU dentro de mixer_try_play, incluindo esperas; nao mede toda a execucao assincrona do RSP.',
        'CP0 ticks a 46.875 MHz; logs por 30 quadros; primeira janela pode incluir loading.',
        'Instrumentacao muda o layout e custa tempo. Nao atribui efeitos de layout por si so.']
    write('AB_CONTENT_MANIFEST.json',json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--out',required=True,type=Path)
    instrument(parser.parse_args().out)
