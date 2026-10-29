// Renderizador N64 (RDP) da camada PsyX: recebe os triangulos ja transformados (coordenadas de tela do PS1) e desenha com rdpq.
// As texturas do PS1 vivem na VRAM (1024x512 de 16 bits). Para cada triangulo texturizado, a regiao usada da pagina de textura e
// decodificada (CI4/CI8/RGBA16 no formato do RDP) para um cache pequeno e carregada na TMEM; a paleta (CLUT) vai junto.
#define D2_NOPALCACHE 1   // o cache de paletas nao ganhou nada (medido)
// Janela de 64x64 na TMEM + bancos de paleta (medido em 28/10: 8,37M -> 6,12M ticks/quadro na rua de Chicago).
// Para voltar ao caminho antigo: D2_EXTRA=-DD2_NO_TEXWIN
#ifndef D2_NO_TEXWIN
#define D2_TEXWIN 1
#define D2_PALBANK 1
#endif
#include <libdragon.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "psx/types.h"
#include "psx/libgte.h"
#include "psx/libgpu.h"
#include "PsyX/PsyX_public.h"
#include "PsyX/PsyX_globals.h"
#include "PsyX/PsyX_render.h"

extern "C" {
extern unsigned short vram[];      // psyx_backend.cpp (valores nativos de 16 bits, ja no formato do PS1)
extern DRAWENV activeDrawEnv;
extern DISPENV activeDispEnv;
void d2_sys_note_frame(void);      // sys_n64.c
void d2_log_push(const char *);
int d2_sys_vbl_count(void);
}

#ifdef D2_RDP_DEBUG
#define D2DBG debugf
#else
#define D2DBG(...) ((void)0)
#endif
#define SCREEN_W 320
#define SCREEN_H 240

// ---------------------------------------------------------------- estado de cena
static surface_t *s_fb;
static bool s_scene;
int d2_rdp_frames;
extern "C" int d2_rdp_scene_active(void) { return s_scene; }
extern "C" int d2_rdp_frame_count(void) { return d2_rdp_frames; }

TextureID g_whiteTexture = 1, g_vramTexture = 2;
static TexFormat s_texFormat;
static bool s_textured, s_offscreen;
static BlendMode s_blend;
static const GrVertex *s_verts;
static int s_numVerts;
static int s_modeKey = -1;
static int s_benchTris, s_benchTex;
static int s_triCount, s_texTriCount, s_subTri, s_calls; static unsigned s_areaH[6];
static int s_scx0, s_scy0, s_scx1 = SCREEN_W, s_scy1 = SCREEN_H;
static int s_clipKey[6] = { -1 };   // ultimo recorte aplicado (GR_SetupClipMode so reconfigura quando muda)
static bool s_rdpInit;

static void rdp_init_once(void)
{
    if (s_rdpInit) return;
    s_rdpInit = true;
    rdpq_init();
}

// ---------------------------------------------------------------- versoes da VRAM (para invalidar o cache de texturas)
// grade de 16 x 16 celulas: 64 palavras de largura x 32 linhas de altura
static unsigned int s_cellVer[16 * 16];
static unsigned int s_vramWrites;
extern "C" void d2_vram_touch(int x, int y, int w, int h)
{
    s_vramWrites++;
    if (w <= 0 || h <= 0) return;
    int cx0 = x >> 6, cx1 = (x + w - 1) >> 6, cy0 = y >> 5, cy1 = (y + h - 1) >> 5;
    if (cx0 < 0) cx0 = 0; if (cy0 < 0) cy0 = 0; if (cx1 > 15) cx1 = 15; if (cy1 > 15) cy1 = 15;
    for (int cy = cy0; cy <= cy1; cy++)
        for (int cx = cx0; cx <= cx1; cx++) s_cellVer[cy * 16 + cx]++;
}
static unsigned int cell_signature(int wx, int wy, int ww, int wh)
{
    int cx0 = (wx & 1023) >> 6, cx1 = ((wx + ww - 1) & 1023) >> 6, cy0 = (wy & 511) >> 5, cy1 = ((wy + wh - 1) & 511) >> 5;
    unsigned int sig = 0;
    for (int cy = cy0; cy <= cy1; cy++)
        for (int cx = cx0; cx <= cx1; cx++) sig += s_cellVer[cy * 16 + cx];
    return sig;
}

// ---------------------------------------------------------------- cache de regioes de textura decodificadas
#define TEX_SLOTS 64
#define TEX_BYTES 2048
struct TexSlot
{
    unsigned int key0, key1;     // identificam a regiao
    unsigned int sig;
    unsigned int gen;            // muda a cada nova decodificacao do slot
    unsigned int use;            // carimbo do ultimo uso (o RDP pode ainda nao ter lido os dados)
    unsigned short wpad, h;      // largura (com padding) e altura em texels
    unsigned char valid;
    unsigned char data[TEX_BYTES] __attribute__((aligned(16)));
};
static TexSlot *s_slots;
static unsigned int s_dbgHit, s_dbgMiss, s_dbgTex, s_dbgTexStep; static int s_dbgPath; static unsigned int s_dbgPM[4], s_dbgPT[4];
#define PAL_RING 64
static unsigned short s_palRing[PAL_RING][256] __attribute__((aligned(16)));
static unsigned int s_palUse[PAL_RING];
static int s_palIdx;
static unsigned int s_useCounter, s_doneCounter;     // carimbos de uso x ultimo ponto em que o RDP comprovadamente terminou
static unsigned int s_endStamp[2];
static void rdp_wait_idle(void) { rspq_wait(); s_doneCounter = s_useCounter; }
static int s_palClut = -1, s_palFmt = -1;
static unsigned int s_palVer;

static inline unsigned short conv_color(unsigned short v)
{
    // PS1: xBBBBBGGGGGRRRRR (0x0000 = transparente) -> RDP: RRRRRGGGGGBBBBBA
    return (unsigned short)(((v & 31) << 11) | (((v >> 5) & 31) << 6) | (((v >> 10) & 31) << 1) | (v != 0));
}

struct PalEnt { int clut; unsigned int sig, gen, use, vw; int valid; unsigned short pal[16] __attribute__((aligned(16))); };
#define PAL_CACHE 256
static PalEnt s_palCache[PAL_CACHE];


struct UploadState
{
    TexSlot *slot;
    unsigned int gen;
    int clut, fmt;
    unsigned int palVer;
    struct PalEnt *palEnt;
    unsigned int palGen;
} s_lastUp;
static unsigned int s_genCounter;

// ---------------------------------------------------------------- VRAM "sombra" (4 bits): x >= 512 ja no formato do RDP (nibbles trocados)
// 512 palavras x 512 linhas; convertida por celula (64 x 32 palavras) sob demanda, sempre que a celula da VRAM mudou
#define SH_W 512
static unsigned short s_shadow[SH_W * 512] __attribute__((aligned(64)));
static unsigned int s_shVer[16 * 16];
static bool s_shInit;
static unsigned int s_shLastUse;
static int s_upOu, s_upOv;                         // origem (em texels) da regiao que ficou na TMEM: coordenadas de textura = (u - ou, v - ov)
static TexSlot s_shTag;
static int s_winBase = -1;          // base (0/512 bytes) da imagem de textura ativa para a janela; -1 = outra imagem
static unsigned int s_cBankHitX, s_dCntP, s_dTicks, s_tTicks, s_nTri, s_nClip, s_clipTicks, s_upTicks, s_nSub, s_palT, s_winHitT, s_winMissT, s_regT, s_vwFrame, s_nHitVw;
static inline unsigned int cnt0(void) { unsigned int c; __asm__ volatile("mfc0 %0,$9" : "=r"(c)); return c; }
static unsigned int s_cLdSh, s_cLdReg, s_cLdWin, s_cTlut, s_cWinHit, s_cCalls;   // contadores por quadro (uploads de textura / paleta)                            // so o endereco e usado (marca "TMEM carregada a partir da sombra")
static struct { unsigned int k0, k1, sig; } s_shLast;

static void shadow_convert_cell(int cx, int cy)
{
    for (int r = 0; r < 32; r++)
    {
        const int y = cy * 32 + r;
        const unsigned int *src = (const unsigned int *)(vram + (y << 10) + cx * 64);
        unsigned int *dst = (unsigned int *)(s_shadow + y * SH_W + (cx - 8) * 64);
        for (int k = 0; k < 32; k++)
        {
            unsigned int x = src[k];
            x = ((x & 0x00FF00FFu) << 8) | ((x >> 8) & 0x00FF00FFu);
            dst[k] = ((x & 0x0F0F0F0Fu) << 4) | ((x >> 4) & 0x0F0F0F0Fu);
        }
        data_cache_hit_writeback(dst, 128);
    }
}
static unsigned int s_shSyncedAt = 0xFFFFFFFFu;   // s_vramWrites na ultima sincronizacao completa da sombra
static void shadow_sync(int wx, int wy, int ww, int wh);
// converte (uma vez) todas as celulas da metade direita da VRAM que mudaram desde a ultima chamada; sem escrita de VRAM nao faz nada
static inline void shadow_sync_all(void)
{
    if (s_shSyncedAt == s_vramWrites) return;
    shadow_sync(512, 0, 512, 512);
    s_shSyncedAt = s_vramWrites;
}
static void shadow_sync(int wx, int wy, int ww, int wh)      // celulas tocadas pela regiao (em palavras de VRAM)
{
    if (!s_shInit) { for (int i = 0; i < 256; i++) s_shVer[i] = 0xFFFFFFFFu; s_shInit = true; }
    int cx0 = (wx & 1023) >> 6, cx1 = ((wx + ww - 1) & 1023) >> 6, cy0 = (wy & 511) >> 5, cy1 = ((wy + wh - 1) & 511) >> 5;
    for (int cy = cy0; cy <= cy1; cy++)
        for (int cx = cx0; cx <= cx1; cx++)
        {
            const int i = cy * 16 + cx;
            if (s_shVer[i] == s_cellVer[i]) continue;
            if (s_shLastUse > s_doneCounter) rdp_wait_idle();       // o RDP ainda pode estar lendo a versao antiga
            shadow_convert_cell(cx, cy);
            s_shVer[i] = s_cellVer[i];
        }
}

// decodifica a regiao (u0,v0,w,h com passo step) da pagina (px,py) para o slot; retorna false se nao couber
static TexSlot *get_region(int px, int py, int fmt, int u0, int v0, int w, int h, int step, int *outW, int *outH, int *outWpad)
{
    const int sw = (w + step - 1) / step, sh = (h + step - 1) / step;
    int wpad;
    if (fmt == TF_4_BIT) wpad = (sw + 15) & ~15;
    else if (fmt == TF_8_BIT) wpad = (sw + 7) & ~7;
    else wpad = (sw + 3) & ~3;
    int bytes = fmt == TF_4_BIT ? (wpad / 2) * sh : fmt == TF_8_BIT ? wpad * sh : wpad * 2 * sh;
    if (bytes > TEX_BYTES) return 0;                       // paletas ocupam a metade alta da TMEM
    *outW = sw; *outH = sh; *outWpad = wpad;

    unsigned int k0 = (unsigned)px | ((unsigned)py << 10) | ((unsigned)fmt << 20) | ((unsigned)step << 24);
    unsigned int k1 = (unsigned)u0 | ((unsigned)v0 << 8) | ((unsigned)(w - 1) << 16) | ((unsigned)(h - 1) << 24);
    unsigned int hsh = (k0 * 0x9E3779B1u) ^ (k1 * 0x85EBCA6Bu);
    hsh ^= hsh >> 15; hsh *= 0x2C1B3C6Du; hsh ^= hsh >> 13;
    const unsigned int base = hsh % TEX_SLOTS;

    // palavras de VRAM que a regiao toca (para a assinatura)
    int wx, ww;
    if (fmt == TF_4_BIT) { wx = px + (u0 >> 2); ww = ((u0 + w - 1) >> 2) - (u0 >> 2) + 1; }
    else if (fmt == TF_8_BIT) { wx = px + (u0 >> 1); ww = ((u0 + w - 1) >> 1) - (u0 >> 1) + 1; }
    else { wx = px + u0; ww = w; }
    unsigned int sig = cell_signature(wx, py + v0, ww, h);

    TexSlot *s = 0;
    for (int i = 0; i < 4; i++)
    {
        TexSlot *c = &s_slots[(base + i) % TEX_SLOTS];
        if (c->valid && c->key0 == k0 && c->key1 == k1 && c->sig == sig) { c->use = ++s_useCounter; s_dbgHit++; return c; }
    }
    // precisa decodificar: so reutiliza um slot cujos dados o RDP ja terminou de ler
    for (int i = 0; i < 4 && !s; i++)
    {
        TexSlot *c = &s_slots[(base + i) % TEX_SLOTS];
        if (!c->valid || c->use <= s_doneCounter) s = c;
    }
    if (!s) { rdp_wait_idle(); s = &s_slots[base]; }

    // decodifica
    s_dbgMiss++; s_dbgTex += sw * sh; s_dbgPM[fmt & 3]++; s_dbgPT[fmt & 3] += sw * sh; if (step > 1) s_dbgTexStep += sw * sh;
    unsigned char *dst = s->data;
    for (int y = 0; y < sh; y++)
    {
        const unsigned short *row = vram + (((py + v0 + y * step) & 511) << 10);
        if (fmt == TF_4_BIT)
        {
            unsigned char *d = dst + y * (wpad / 2);
            int x = 0;
            if (step == 1 && (u0 & 3) == 0)
            {
                // 4 texels (uma palavra da VRAM) por vez: troca os nibbles de cada byte (o RDP quer o texel da esquerda no nibble alto)
                const unsigned short *src = row + ((px + (u0 >> 2)) & 1023);
                const int words = sw >> 2;
                unsigned short *d16 = (unsigned short *)d;
                for (int k = 0; k < words; k++)
                {
                    unsigned int wv = src[k];
                    unsigned int lo = wv & 0xFF, hi = wv >> 8;
                    d16[k] = (unsigned short)((((lo & 15) << 4 | lo >> 4) << 8) | ((hi & 15) << 4 | hi >> 4));
                }
                x = words << 2;
            }
            for (; x < sw; x++)
            {
                int u = u0 + x * step;
                unsigned int word = row[(px + (u >> 2)) & 1023];
                unsigned int n = (word >> ((u & 3) * 4)) & 15;
                if (x & 1) d[x >> 1] |= (unsigned char)n; else d[x >> 1] = (unsigned char)(n << 4);
            }
            for (int pb = (sw + 1) >> 1; pb < (wpad >> 1); pb++) d[pb] = 0;
        }
        else if (fmt == TF_8_BIT)
        {
            unsigned char *d = dst + y * wpad;
            int x = 0;
            if (step == 1 && (u0 & 1) == 0)
            {
                const unsigned short *src = row + ((px + (u0 >> 1)) & 1023);
                const int words = sw >> 1;
                unsigned short *d16 = (unsigned short *)d;
                for (int k = 0; k < words; k++) { unsigned int wv = src[k]; d16[k] = (unsigned short)((wv << 8) | (wv >> 8)); }
                x = words << 1;
            }
            for (; x < sw; x++)
            {
                int u = u0 + x * step;
                unsigned int word = row[(px + (u >> 1)) & 1023];
                d[x] = (unsigned char)(word >> ((u & 1) * 8));
            }
            for (int pb = sw; pb < wpad; pb++) d[pb] = 0;
        }
        else
        {
            unsigned short *d = (unsigned short *)(dst + y * wpad * 2);
            for (int x = 0; x < sw; x++) d[x] = conv_color(row[(px + u0 + x * step) & 1023]);
            for (int x = sw; x < wpad; x++) d[x] = 0;
        }
    }
    data_cache_hit_writeback(dst, (bytes + 15) & ~15);
    s->key0 = k0; s->key1 = k1; s->sig = sig; s->valid = 1; s->gen = ++s_genCounter; s->use = ++s_useCounter;
    return s;
}

// ---------------------------------------------------------------- modos do RDP
static void set_mode(bool textured, int fmt, BlendMode blend)
{
    int key = textured ? (1 + fmt) * 8 + (int)blend : (int)blend;
    if (key == s_modeKey) return;
    s_modeKey = key;
    s_lastUp.slot = 0;     // o estado de textura se perde ao trocar de modo (palette/tlut)

    rdpq_set_mode_standard();
    rdpq_mode_antialias(AA_NONE);
    rdpq_mode_dithering(DITHER_NONE_NONE);
    rdpq_mode_filter(FILTER_POINT);
    int pa = 0, add = 0;
    switch (blend)
    {
        case BM_AVERAGE:           pa = 128; break;   // 0.5 B + 0.5 F
        case BM_SUBTRACT:          pa = 110; break;   // B - F (aproximado: escurece)
        case BM_ADD:               pa = 170; add = 1; break;
        case BM_ADD_QUATER_SOURCE: pa = 64;  add = 1; break;
        default: break;
    }
    if (textured)
    {
        // texels transparentes (indice 0) tem alfa 0 => tanto o alpha-compare (opaco) quanto a mistura (alfa do texel x constante) os ignoram
        if (pa) rdpq_mode_combiner(RDPQ_COMBINER1((TEX0, ZERO, SHADE, ZERO), (TEX0, ZERO, PRIM, ZERO)));
        else    { rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE); rdpq_mode_alphacompare(1); }
        rdpq_mode_tlut(fmt == TF_16_BIT ? TLUT_NONE : TLUT_RGBA16);
    }
    else
    {
        if (pa) rdpq_mode_combiner(RDPQ_COMBINER1((ZERO, ZERO, ZERO, SHADE), (ZERO, ZERO, ZERO, PRIM)));
        else    rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
    }
    if (pa)
    {
        rdpq_set_prim_color(RGBA32(255, 255, 255, pa));
        if (add) rdpq_mode_blender(RDPQ_BLENDER((IN_RGB, IN_ALPHA, MEMORY_RGB, ONE)));
        else     rdpq_mode_blender(RDPQ_BLENDER((IN_RGB, IN_ALPHA, MEMORY_RGB, INV_MUX_ALPHA)));
    }
    else rdpq_mode_blender(0);
}


#ifdef D2_TEXWIN
// ---------------------------------------------------------------- bancos de paleta na TMEM (CI4): 16 bancos de 16 cores; o banco 0 fica
// para o caminho antigo, 1..15 sao um cache LRU (clut -> banco). Acerto = so troca o banco no TILE0 (1 comando).
static struct { int clut; unsigned int gen, lru; } s_bank[16];
static unsigned int s_bankLru;
static int s_curBank = -1;            // banco configurado no TILE0 da janela (-1 = desconhecido)
static void banks_invalidate(void) { for (int i = 0; i < 16; i++) s_bank[i].clut = -1; }
static int s_pbClut = -1; static unsigned int s_pbVw;
static void upload_pal_bank(int clut)
{
    if (clut == s_pbClut && s_pbVw == s_vramWrites && s_curBank > 0 && s_bank[s_curBank].clut == clut) { s_bank[s_curBank].lru = ++s_bankLru; return; }
    s_pbClut = clut; s_pbVw = s_vramWrites;
    PalEnt *e = &s_palCache[(clut ^ (clut >> 8)) & (PAL_CACHE - 1)];
    const int cx = (clut & 63) * 16, cy = clut >> 6;
    bool ok = e->valid && e->clut == clut;
    if (ok && e->vw != s_vramWrites)
    {
        const unsigned int sig = cell_signature(cx, cy, 16, 1);
        if (sig != e->sig) ok = false; else e->vw = s_vramWrites;
    }
    if (!ok)
    {
        if (e->valid && e->use > s_doneCounter) rdp_wait_idle();      // o RDP ainda pode estar lendo esta paleta
        const unsigned short *src = vram + ((cy & 511) << 10);
        for (int i = 0; i < 16; i++) e->pal[i] = conv_color(src[(cx + i) & 1023]);
        data_cache_hit_writeback(e->pal, 32);
        e->clut = clut; e->sig = cell_signature(cx, cy, 16, 1); e->vw = s_vramWrites; e->valid = 1; e->gen = ++s_genCounter;
    }
    int b = 0;
    for (int i = 1; i < 16; i++) if (s_bank[i].clut == clut && s_bank[i].gen == e->gen) { b = i; break; }
    if (b) s_cBankHitX++;
    else
    {
        b = 1;
        for (int i = 2; i < 16; i++) if (s_bank[i].lru < s_bank[b].lru) b = i;
        rdpq_tex_upload_tlut(e->pal, b * 16, 16); s_cTlut++; s_winBase = -1;
        e->use = ++s_useCounter;
        s_bank[b].clut = clut; s_bank[b].gen = e->gen;
    }
    s_bank[b].lru = ++s_bankLru;
    if (b != s_curBank)
    {
        rdpq_tileparms_t tp = {}; tp.palette = b;
        rdpq_set_tile(TILE0, FMT_CI4, 0, 32, &tp);
        s_curBank = b;
    }
}
#endif

// ---------------------------------------------------------------- D2_TEXWIN: janela fixa de 64x64 texels (CI4, 2 KB) na TMEM
// Em vez de carregar o retangulo exato de cada triangulo, carrega uma janela de 64x64 da VRAM sombra e mantem enquanto os
// triangulos seguintes couberem nela (mesma pagina, VRAM nao alterada). A carga e um unico LOAD_TILE (tiles fixos).
static bool s_leanTiles;              // TILE0 (CI4 64x64) e TILE6 (carga I8) configurados para a janela
static bool s_tmemWin;                // a TMEM (metade baixa) contem a janela s_win
static struct { int px, py, wu0, wv0; unsigned int sig, vw; } s_win;
static TexSlot s_winTag;
#ifdef D2_TEXWIN
#ifndef D2_WINALIGN
#define D2_WINALIGN 32
#endif
static bool upload_pal(int clut);
static bool upload_win(int px, int py, int clut, int u0, int v0, int w, int h)
{
    s_cCalls++;
    bool hit = s_tmemWin && s_win.px == px && s_win.py == py && u0 >= s_win.wu0 && u0 + w <= s_win.wu0 + 64 && v0 >= s_win.wv0 && v0 + h <= s_win.wv0 + 64;
    if (hit && s_win.vw != s_vramWrites)
    {
#ifdef D2_DPROF
        s_nHitVw++;
#endif
        hit = false;          // a VRAM mudou desde a carga (raro durante o jogo): recarrega
    }
#ifdef D2_DPROF
    const unsigned int tp0 = cnt0();
#endif
#ifdef D2_PALBANK
    upload_pal_bank(clut);
#else
    upload_pal(clut);
#endif
#ifdef D2_DPROF
    s_palT += cnt0() - tp0;
#endif
    if (hit) { s_cWinHit++; s_upOu = s_win.wu0; s_upOv = s_win.wv0; s_lastUp.slot = &s_winTag; return true; }

    // origem: alinhada a D2_WINALIGN se o triangulo couber; senao a 16; senao no proprio triangulo
    int wu0 = u0 & ~(D2_WINALIGN - 1); if (u0 + w - wu0 > 64) wu0 = u0 & ~15; if (u0 + w - wu0 > 64) wu0 = u0 & ~1; if (wu0 + 64 > 256) wu0 = 192;
    int wv0 = v0 & ~(D2_WINALIGN - 1); if (v0 + h - wv0 > 64) wv0 = v0 & ~15; if (v0 + h - wv0 > 64) wv0 = v0;      if (wv0 + 64 > 256) wv0 = 192;
    shadow_sync_all();
    s_win.px = px; s_win.py = py; s_win.wu0 = wu0; s_win.wv0 = wv0; s_win.vw = s_vramWrites;
    s_shLastUse = ++s_useCounter;
    if (!s_leanTiles)
    {
        s_winBase = -1;
        rdpq_set_tile(TILE6, FMT_I8, 0, 32, NULL);
        rdpq_set_tile(TILE0, FMT_CI4, 0, 32, NULL);
        rdpq_set_tile_size(TILE0, 0, 0, 64, 64);
        s_leanTiles = true;
        s_curBank = 0;
#ifdef D2_PALBANK
        upload_pal_bank(clut);       // TILE0 foi refeito com o banco 0: reaplica o banco da paleta atual
#endif
    }
    // coordenadas de carga em bytes da sombra (2 texels CI4 por byte): coluna = ((px-512)*4 + wu0)/2
    // o RDP limita coordenadas de carga a < 1024: a imagem comeca no byte 0 ou 512 da linha (troca so quando muda)
    int bx = ((px - 512) * 4 + wu0) >> 1; const int by = py + wv0;
    const int base = bx >= 512 ? 512 : 0;
    if (base != s_winBase) { rdpq_set_texture_image_raw(0, PhysicalAddr((const char *)s_shadow + base), FMT_I8, SH_W * 2, 512); s_winBase = base; }
    bx -= base;
    rdpq_load_tile(TILE6, bx, by, bx + 32, by + 64);
    s_cLdWin++;
    s_tmemWin = true;
    s_upOu = wu0; s_upOv = wv0; s_lastUp.slot = &s_winTag;
    return true;
}
#endif

// carrega a regiao (u0,v0,w,h, passo step) da pagina de textura na TMEM (paleta inclusa). Retorna false se nao coube.
static bool upload_region(int page, int clut, int u0, int v0, int w, int h, int step)
{
    const int px = (page & 15) * 64, py = ((page >> 4) & 1) * 256;
#ifdef D2_TEXWIN
    if (s_texFormat == TF_4_BIT && step == 1 && px >= 512 && w <= 64 && h <= 64)
    {
#ifdef D2_DPROF
        const unsigned int h0 = s_cWinHit, tw0 = cnt0(); const bool rr = upload_win(px, py, clut, u0, v0, w, h);
        if (s_cWinHit != h0) s_winHitT += cnt0() - tw0; else s_winMissT += cnt0() - tw0;
        return rr;
#else
        return upload_win(px, py, clut, u0, v0, w, h);
#endif
    }
#ifdef D2_DPROF
    struct RT { unsigned int t; ~RT() { s_regT += cnt0() - t; } } rt_{cnt0()};
#endif
#endif
    s_upOu = u0; s_upOv = v0;
    int sw, sh, wpad;
    bool useShadow = false;
    int ua = 0, e0 = 0, e1 = 0;
    if (s_texFormat == TF_4_BIT && step == 1 && px >= 512)
    {
        ua = u0 & ~15;
        e0 = (u0 - ua) & ~1; e1 = ((u0 - ua) + w + 1) & ~1;
        useShadow = ((((e1 - e0) >> 1) + 7) & ~7) * h <= 2048;
    }
    TexSlot *slot = useShadow ? &s_shTag : get_region(px, py, s_texFormat, u0, v0, w, h, step, &sw, &sh, &wpad);
    if (!slot) return false;
    unsigned int shK0 = 0, shK1 = 0, shSig = 0;
    if (useShadow)
    {
        const int wx = px + (u0 >> 2), ww = ((u0 + w - 1) >> 2) - (u0 >> 2) + 1;
        shadow_sync(wx, py + v0, ww, h);
        shSig = cell_signature(wx, py + v0, ww, h);
        shK0 = (unsigned)px | ((unsigned)py << 10) | ((unsigned)u0 << 20);
        shK1 = (unsigned)v0 | ((unsigned)w << 8) | ((unsigned)h << 20);
        s_shLastUse = ++s_useCounter;
#ifdef D2_LEANLOAD
        s_upOu = ua + ((u0 - ua) & ~1);   // origem da carga direta (mesma em acerto e em carga nova)
#else
        s_upOu = ua;
#endif
    }

    const bool paletted = s_texFormat != TF_16_BIT;
#ifndef D2_NOPALCACHE
    if (s_texFormat == TF_4_BIT)
    {
        // paletas de 16 cores: cache por CLUT (convertidas uma vez; invalidadas pela versao das celulas da VRAM)
        const int cx = (clut & 63) * 16, cy = clut >> 6;
        PalEnt *e = &s_palCache[(clut ^ (clut >> 8)) & (PAL_CACHE - 1)];
        const unsigned int sig = cell_signature(cx, cy, 16, 1);
        if (!e->valid || e->clut != clut || e->sig != sig)
        {
            if (e->use > s_doneCounter) rdp_wait_idle();          // o RDP ainda pode estar lendo esta paleta
            const unsigned short *src = vram + ((cy & 511) << 10);
            for (int i = 0; i < 16; i++) e->pal[i] = conv_color(src[(cx + i) & 1023]);
            data_cache_hit_writeback(e->pal, 32);
            e->clut = clut; e->sig = sig; e->valid = 1; e->gen = ++s_genCounter;
        }
        if (s_lastUp.slot == 0 || s_lastUp.fmt != s_texFormat || s_lastUp.palEnt != e || s_lastUp.palGen != e->gen)
        {
            rdpq_tex_upload_tlut(e->pal, 0, 16); s_cTlut++; s_winBase = -1;
            e->use = ++s_useCounter;
            s_lastUp.fmt = s_texFormat; s_lastUp.palEnt = e; s_lastUp.palGen = e->gen; s_lastUp.clut = clut;
        }
    }
#else
    if (false) {}
#endif
    else if (paletted && (s_lastUp.clut != clut || s_lastUp.fmt != s_texFormat || s_lastUp.palVer != s_vramWrites || s_lastUp.slot == 0))
    {
        const int cx = (clut & 63) * 16, cy = clut >> 6;
        const unsigned short *src = vram + ((cy & 511) << 10);
        const int n = s_texFormat == TF_4_BIT ? 16 : 256;
        s_palIdx = (s_palIdx + 1) % PAL_RING;
        if (s_palUse[s_palIdx] > s_doneCounter) rdp_wait_idle();
        unsigned short *pal = s_palRing[s_palIdx];
        for (int i = 0; i < n; i++) pal[i] = conv_color(src[(cx + i) & 1023]);
        data_cache_hit_writeback(pal, n * 2);
        rdpq_tex_upload_tlut(pal, 0, n); s_cTlut++; s_winBase = -1;
#ifdef D2_PALBANK
        if (n > 16) banks_invalidate();
#endif
        s_palUse[s_palIdx] = ++s_useCounter;
        s_lastUp.clut = clut; s_lastUp.fmt = s_texFormat; s_lastUp.palVer = s_vramWrites; s_lastUp.palEnt = 0;
    }
    if (useShadow)
    {
        if (s_lastUp.slot != slot || s_shLast.k0 != shK0 || s_shLast.k1 != shK1 || s_shLast.sig != shSig)
        {
#ifdef D2_LEANLOAD
            // carga direta da sombra (como a janela): bytes [e0/2, e1/2) das linhas v0..v0+h, origem da textura em ua+e0
            const int e0 = (u0 - ua) & ~1, e1 = ((u0 - ua) + w + 1) & ~1, nb = (e1 - e0) >> 1, pitch = (nb + 7) & ~7;
            int bx = (px - 512) * 2 + ((ua + e0) >> 1); const int by = py + v0, base = bx >= 512 ? 512 : 0;
            if (base != s_winBase) { rdpq_set_texture_image_raw(0, PhysicalAddr((const char *)s_shadow + base), FMT_I8, SH_W * 2, 512); s_winBase = base; }
            bx -= base;
            rdpq_set_tile(TILE6, FMT_I8, 0, pitch, NULL);
            rdpq_load_tile(TILE6, bx, by, bx + nb, by + h);
            rdpq_set_tile(TILE0, FMT_CI4, 0, pitch, NULL);
            rdpq_set_tile_size(TILE0, 0, 0, e1 - e0, h);
            s_upOu = ua + e0;
            s_cLdSh++; s_leanTiles = false; s_tmemWin = false;
#else
            surface_t surf = surface_make(s_shadow + (py + v0) * SH_W + ((px - 512) + (ua >> 2)), FMT_CI4, 256 - ua, 256 - v0, SH_W * 2);
            rdpq_texparms_t parms = {};
            rdpq_tex_upload_sub(TILE0, &surf, &parms, u0 - ua, 0, (u0 - ua) + w, h); s_cLdSh++; s_leanTiles = false; s_tmemWin = false;
#endif
            s_lastUp.slot = slot; s_shLast.k0 = shK0; s_shLast.k1 = shK1; s_shLast.sig = shSig;
        }
    }
    else if (s_lastUp.slot != slot || s_lastUp.gen != slot->gen)
    {
#ifdef D2_LEANLOAD
        // carga direta do slot (dados ja no layout final, linhas de 'rowb' bytes multiplos de 8): 4 comandos fixos, sem o tex_loader
        const int rowb = s_texFormat == TF_4_BIT ? wpad / 2 : s_texFormat == TF_8_BIT ? wpad : wpad * 2;
        if (s_texFormat == TF_16_BIT)
        {
            rdpq_set_texture_image_raw(0, PhysicalAddr(slot->data), FMT_RGBA16, wpad, sh);
            rdpq_set_tile(TILE6, FMT_RGBA16, 0, rowb, NULL);
            rdpq_load_tile(TILE6, 0, 0, wpad, sh);
            rdpq_set_tile(TILE0, FMT_RGBA16, 0, rowb, NULL);
        }
        else
        {
            rdpq_set_texture_image_raw(0, PhysicalAddr(slot->data), FMT_I8, rowb, sh);
            rdpq_set_tile(TILE6, FMT_I8, 0, rowb, NULL);
            rdpq_load_tile(TILE6, 0, 0, rowb, sh);
            rdpq_set_tile(TILE0, s_texFormat == TF_4_BIT ? FMT_CI4 : FMT_CI8, 0, rowb, NULL);
        }
        rdpq_set_tile_size(TILE0, 0, 0, sw, sh);
        s_cLdReg++; s_leanTiles = false; s_tmemWin = false; s_winBase = -1;
#else
        surface_t surf = surface_make(slot->data, s_texFormat == TF_4_BIT ? FMT_CI4 : s_texFormat == TF_8_BIT ? FMT_CI8 : FMT_RGBA16, wpad, sh,
                                      s_texFormat == TF_4_BIT ? wpad / 2 : s_texFormat == TF_8_BIT ? wpad : wpad * 2);
        rdpq_texparms_t parms = {};
        rdpq_tex_upload_sub(TILE0, &surf, &parms, 0, 0, sw, sh); s_cLdReg++; s_leanTiles = false; s_tmemWin = false;
#endif
        s_lastUp.slot = slot; s_lastUp.gen = slot->gen;
    }
    return true;
}

#ifdef D2_TEXWIN
// paleta de 16 cores (mesma regra do caminho antigo: recarrega se mudou CLUT, VRAM ou modo)
static bool upload_pal(int clut)
{
    if (s_lastUp.clut == clut && s_lastUp.fmt == s_texFormat && s_lastUp.palVer == s_vramWrites && s_lastUp.slot != 0) return false;
    const int cx = (clut & 63) * 16, cy = clut >> 6;
    const unsigned short *src = vram + ((cy & 511) << 10);
    s_palIdx = (s_palIdx + 1) % PAL_RING;
    if (s_palUse[s_palIdx] > s_doneCounter) rdp_wait_idle();
    unsigned short *pal = s_palRing[s_palIdx];
    for (int i = 0; i < 16; i++) pal[i] = conv_color(src[(cx + i) & 1023]);
    data_cache_hit_writeback(pal, 32);
    rdpq_tex_upload_tlut(pal, 0, 16); s_cTlut++; s_winBase = -1;
    s_palUse[s_palIdx] = ++s_useCounter;
    s_lastUp.clut = clut; s_lastUp.fmt = s_texFormat; s_lastUp.palVer = s_vramWrites; s_lastUp.palEnt = 0;
    return true;
}
#endif

struct DV { float x, y, u, v, r, g, b, a; };

// recorta o poligono (em ordem) contra um semi-plano no espaco UV: eixo 0 = u, 1 = v; keepGE: mantem valores >= bound (senao <= bound)
static int clip_poly(const DV *in, int n, DV *out, int axis, float bound, bool keepGE)
{
    int m = 0;
    for (int i = 0; i < n; i++)
    {
        const DV &a = in[i], &b = in[(i + 1) % n];
        float va = (axis ? a.v : a.u) - bound, vb = (axis ? b.v : b.u) - bound;
        if (!keepGE) { va = -va; vb = -vb; }
        const bool ina = va >= 0, inb = vb >= 0;
        if (ina) out[m++] = a;
        if (ina != inb)
        {
            float t = va / (va - vb);
            DV c;
            c.x = a.x + (b.x - a.x) * t; c.y = a.y + (b.y - a.y) * t; c.u = a.u + (b.u - a.u) * t; c.v = a.v + (b.v - a.v) * t;
            c.r = a.r + (b.r - a.r) * t; c.g = a.g + (b.g - a.g) * t; c.b = a.b + (b.b - a.b) * t; c.a = a.a + (b.a - a.a) * t;
            out[m++] = c;
        }
    }
    return m;
}

extern "C" void __rdpq_autosync_use(uint32_t res);
// versao enxuta de rdpq_triangle_rsp: sem floorf/floor/divisao por vertice (w e sempre 1.0)
static inline int fl4(float v) { return (int)(v * 4.0f + 16384.0f) - 16384; }      // floor(v*4) para |v| < 4096
static inline void emit_tri(bool tex, const DV &a, const DV &b, const DV &c, float ou, float ov, float sc, float sx, float sy)
{
    uint32_t res = AUTOSYNC_PIPE;
    if (tex) res |= AUTOSYNC_TILE(0) | AUTOSYNC_TMEM(0);
    __rdpq_autosync_use(res);
    const DV *d[3] = { &a, &b, &c };
    const float s32 = 32.0f * sc;
    for (int i = 0; i < 3; i++)
    {
        const DV *v = d[i];
        int16_t x = (int16_t)fl4(v->x * sx), y = (int16_t)fl4(v->y * sy);
        uint32_t r = v->r * 255.0f, g = v->g * 255.0f, bl = v->b * 255.0f, al = v->a * 255.0f;
        uint32_t rgba = (r << 24) | (g << 16) | (bl << 8) | al;
        int16_t s = 0, t = 0;
        if (tex) { s = (v->u - ou) * s32; t = (v->v - ov) * s32; }
        rspq_write(RDPQ_OVL_ID, RDPQ_CMD_TRIANGLE_DATA, 32 * i, (x << 16) | (y & 0xFFFF), 0, rgba, (s << 16) | (t & 0xFFFF), tex ? 0x10000 : 0, tex ? 0x10000 : 0);
    }
    if (tex) rdpq_write(20, RDPQ_OVL_ID, RDPQ_CMD_TRIANGLE, 0xC000 | ((RDPQ_CMD_TRI | 6) << 8));
    else     rdpq_write(12, RDPQ_OVL_ID, RDPQ_CMD_TRIANGLE, 0xC000 | ((RDPQ_CMD_TRI | 4) << 8));
}

// versao inteira: coordenadas ja em 1/4 de pixel, cor 0..255, u/v em texels, mult = 32/step
static inline void emit_tri_i(bool tex, const int *X4, const int *Y4, const uint32_t *rgba, const GrVertex *const *gv, int ou, int ov, int mult)
{
    uint32_t res = AUTOSYNC_PIPE;
    if (tex) res |= AUTOSYNC_TILE(0) | AUTOSYNC_TMEM(0);
    __rdpq_autosync_use(res);
    for (int i = 0; i < 3; i++)
    {
        uint32_t xy = ((uint32_t)(int16_t)X4[i] << 16) | ((uint32_t)Y4[i] & 0xFFFF);
        if (tex)
        {
            int s = ((int)gv[i]->u - ou) * mult, t = ((int)gv[i]->v - ov) * mult;
            rspq_write(RDPQ_OVL_ID, RDPQ_CMD_TRIANGLE_DATA, 32 * i, xy, 0, rgba[i], ((uint32_t)(int16_t)s << 16) | ((uint32_t)t & 0xFFFF), 0x10000, 0x10000);
        }
        else rspq_write(RDPQ_OVL_ID, RDPQ_CMD_TRIANGLE_DATA, 32 * i, xy, 0, rgba[i], 0, 0, 0);
    }
    if (tex) rdpq_write(20, RDPQ_OVL_ID, RDPQ_CMD_TRIANGLE, 0xC000 | ((RDPQ_CMD_TRI | 6) << 8));
    else     rdpq_write(12, RDPQ_OVL_ID, RDPQ_CMD_TRIANGLE, 0xC000 | ((RDPQ_CMD_TRI | 4) << 8));
}

// ---------------------------------------------------------------- contrato GR_*
extern "C" {

#ifdef D2_GTEPROF
extern "C" int __real_GTE_operator(int);
unsigned int d2_gp[4]; unsigned int d2_gpf[64][2];
extern "C" int __wrap_GTE_operator(int op)
{
    unsigned int t0, t1; __asm__ volatile("mfc0 %0,$9" : "=r"(t0));
    int r = __real_GTE_operator(op);
    __asm__ volatile("mfc0 %0,$9" : "=r"(t1));
    d2_gp[0] += t1 - t0; d2_gp[1]++; d2_gpf[op & 63][0]++; d2_gpf[op & 63][1] += t1 - t0; return r;
}
#endif
char PsyX_BeginScene(void)
{
    if (s_scene) return 0;
    rdp_init_once();
    s_fb = display_get();
    if (s_endStamp[d2_rdp_frames & 1] > s_doneCounter) s_doneCounter = s_endStamp[d2_rdp_frames & 1];   // o quadro de 2 EndScene atras ja foi exibido => RDP terminou
    rdpq_attach(s_fb, NULL);
    rdpq_set_scissor(0, 0, SCREEN_W, SCREEN_H);
    s_scx0 = 0; s_scy0 = 0; s_scx1 = SCREEN_W; s_scy1 = SCREEN_H; s_clipKey[0] = -2;
    uint8_t r = 0, g = 0, b = 0;
    if (activeDrawEnv.isbg) { r = activeDrawEnv.r0; g = activeDrawEnv.g0; b = activeDrawEnv.b0; }
    rdpq_set_mode_fill(RGBA32(r, g, b, 255));
    rdpq_fill_rectangle(0, 0, SCREEN_W, SCREEN_H);
    s_modeKey = -1;
    s_scene = true;
    D2DBG("[rdp] BeginScene disp=%d,%d %dx%d clip=%d,%d %dx%d vbl=%d\n", activeDispEnv.disp.x, activeDispEnv.disp.y, activeDispEnv.disp.w, activeDispEnv.disp.h, activeDrawEnv.clip.x, activeDrawEnv.clip.y, activeDrawEnv.clip.w, activeDrawEnv.clip.h, d2_sys_vbl_count());
    return 1;
}

void PsyX_EndScene(void)
{
#ifdef D2_GTEPROF
    if ((d2_rdp_frames % 30) == 0) { debugf("[gte] ops=%u ticks=%u\n", d2_gp[1], d2_gp[0]); for (int i = 0; i < 64; i++) if (d2_gpf[i][0]) debugf("[gtef] %02x n=%u t=%u\n", i, d2_gpf[i][0], d2_gpf[i][1]); }
    memset(d2_gpf, 0, sizeof d2_gpf);
    d2_gp[0] = d2_gp[1] = 0;
#endif
    if (!s_scene) { d2_sys_note_frame(); return; }
    s_scene = false;
    D2DBG("[rdp] EndScene f=%d vbl=%d tris=%d tex=%d\n", d2_rdp_frames, d2_sys_vbl_count(), s_triCount, s_texTriCount);
    if ((d2_rdp_frames % 30) == 0) { static unsigned int lastc; unsigned int cc; __asm__ volatile("mfc0 %0,$9" : "=r"(cc)); debugf("[cyc] %u ticks/frame\n", (cc - lastc) / 30); lastc = cc; }
    if ((d2_rdp_frames % 30) == 0) {  debugf("[fps] f=%d vbl=%d sub=%d calls=%d tris=%d tex=%d hit=%u miss=%u texels=%u stepped=%u pm=%u,%u,%u pt=%u,%u,%u\n", d2_rdp_frames, d2_sys_vbl_count(), s_subTri, s_calls, s_triCount, s_texTriCount, s_dbgHit, s_dbgMiss, s_dbgTex, s_dbgTexStep, s_dbgPM[0],s_dbgPM[1],s_dbgPM[2],s_dbgPT[0],s_dbgPT[1],s_dbgPT[2]); }
    s_dbgHit = s_dbgMiss = s_dbgTex = s_dbgTexStep = 0; memset(s_dbgPM,0,sizeof s_dbgPM); memset(s_dbgPT,0,sizeof s_dbgPT);
    if ((d2_rdp_frames % 30) == 0) debugf("[tex] ld_sombra=%u ld_regiao=%u ld_janela=%u janela_hit=%u/%u tlut=%u banco_hit=%u diretos=%u dticks=%u tcore=%u\n[tri] tris=%u clip=%u clipticks=%u upticks=%u sub=%u\n[up] palT=%u winHitT=%u winMissT=%u regT=%u vramWrites=%u\n", s_cLdSh, s_cLdReg, s_cLdWin, s_cWinHit, s_cCalls, s_cTlut, s_cBankHitX, s_dCntP, s_dTicks, s_tTicks, s_nTri, s_nClip, s_clipTicks, s_upTicks, s_nSub, s_palT, s_winHitT, s_winMissT, s_regT, s_vramWrites - s_vwFrame);
    s_cLdSh = s_cLdReg = s_cLdWin = s_cTlut = s_cWinHit = s_cCalls = 0; s_cBankHitX = 0; s_dCntP = s_dTicks = s_tTicks = s_nTri = s_nClip = s_clipTicks = s_upTicks = s_nSub = 0; s_palT = s_winHitT = s_winMissT = s_regT = s_nHitVw = 0; s_vwFrame = s_vramWrites; s_palT = s_winHitT = s_winMissT = s_regT = 0; s_vwFrame = s_vramWrites;
    s_benchTris = s_triCount; s_benchTex = s_texTriCount;
    s_triCount = s_texTriCount = s_subTri = s_calls = 0;
    rdpq_detach_show();
    {   // medicao alinhada ao jogo: ticks medios por quadro em janelas de 32 passos do jogo (CameraCnt), independente da velocidade
        extern int CameraCnt;
        static int lastCC = -1; static unsigned int t0; unsigned int now; __asm__ volatile("mfc0 %0,$9" : "=r"(now));
        if (CameraCnt < lastCC || lastCC < 0) { lastCC = CameraCnt; t0 = now; }
        else if ((CameraCnt >> 5) != (lastCC >> 5) && CameraCnt > 0) { debugf("[bench] cc=%d ticks=%u tris=%d tex=%d\n", CameraCnt & ~31, (now - t0) / (unsigned)(CameraCnt - lastCC), s_benchTris, s_benchTex); lastCC = CameraCnt; t0 = now; }
    }
    s_endStamp[d2_rdp_frames & 1] = s_useCounter;
    d2_rdp_frames++;
    d2_sys_note_frame();
}

// aborta o quadro em andamento (usado antes de mostrar texto de diagnostico)
void d2_rdp_abort_scene(void)
{
    if (!s_scene) return;
    s_scene = false;
    rdpq_detach();
}

void GR_Clear(int x, int y, int w, int h, unsigned char r, unsigned char g, unsigned char b)
{
    if (!s_scene) return;
    int rx = x - activeDispEnv.disp.x, ry = y - activeDispEnv.disp.y;
    int x0 = rx < 0 ? 0 : rx, y0 = ry < 0 ? 0 : ry;
    int x1 = rx + w > SCREEN_W ? SCREEN_W : rx + w, y1 = ry + h > SCREEN_H ? SCREEN_H : ry + h;
    if (x1 <= x0 || y1 <= y0) return;
    rdpq_set_mode_fill(RGBA32(r, g, b, 255));
    rdpq_fill_rectangle(x0, y0, x1, y1);
    s_modeKey = -1;
}

void GR_SwapWindow() {}
void GR_UpdateVRAM() {}
void GR_StoreFrameBuffer(int, int, int, int) {}
void GR_ReadFramebufferDataToVRAM() {}
void GR_SaveVRAM(const char *, int, int, int, int, int) {}

TextureID GR_CreateRGBATexture(int, int, u_char *) { return 3; }
void GR_DestroyTexture(TextureID) {}
void GR_SetTexture(TextureID id, TexFormat fmt)
{
    s_textured = id == g_vramTexture;
    s_texFormat = fmt;
}
void GR_SetBlendMode(BlendMode b) { s_blend = b; }
void GR_SetPolygonOffset(float) {}
void GR_SetStencilMode(int) {}
void GR_EnableDepth(int) {}
void GR_SetScissorState(int) {}
void GR_SetOffscreenState(const RECT16 *, int enable) { s_offscreen = enable != 0; }
void GR_SetupClipMode(const RECT16 *r, int enable)
{
    {
        const int k[6] = { enable ? r->x : -1, enable ? r->y : -1, enable ? r->w : -1, enable ? r->h : -1, activeDispEnv.disp.x | (activeDispEnv.disp.w << 16), activeDispEnv.disp.y | (activeDispEnv.disp.h << 16) };
        if (s_scene && memcmp(k, s_clipKey, sizeof k) == 0) return;
        memcpy(s_clipKey, k, sizeof k);
    }
    int x0 = 0, y0 = 0, x1 = SCREEN_W, y1 = SCREEN_H;
    if (enable)
    {
        const float fx = activeDispEnv.disp.w > 0 ? (float)SCREEN_W / (float)activeDispEnv.disp.w : 1.0f;
        const float fy = activeDispEnv.disp.h > 0 ? (float)SCREEN_H / (float)activeDispEnv.disp.h : 1.0f;
        x0 = (int)((r->x - activeDispEnv.disp.x) * fx); y0 = (int)((r->y - activeDispEnv.disp.y) * fy);
        x1 = x0 + (int)(r->w * fx + 0.5f); y1 = y0 + (int)(r->h * fy + 0.5f);
        if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > SCREEN_W) x1 = SCREEN_W; if (y1 > SCREEN_H) y1 = SCREEN_H;
        if (x1 <= x0 || y1 <= y0) { x0 = y0 = 0; x1 = y1 = 1; }
    }
    s_scx0 = x0; s_scy0 = y0; s_scx1 = x1; s_scy1 = y1;
    if (s_scene) rdpq_set_scissor(x0, y0, x1, y1);
}
void GR_SetViewPort(int, int, int, int) {}
void GR_SetOverrideTextureSize(int, int) {}
void GR_SetWireframe(int) {}
void GR_UpdateVertexBuffer(const GrVertex *v, int n) { s_verts = v; s_numVerts = n; }
void GR_PushDebugLabel(const char *) {}
void GR_PopDebugLabel() {}

// ---------------------------------------------------------------- nucleo por triangulo (usado pelo caminho antigo e pelo direto)
// PV: vertice ja em coordenadas do RDP (1/4 de pixel), cor RGBA pronta e UV da pagina
struct PV { int X4, Y4; uint32_t rgba; int s32, t32; };
static bool s_subq;                           // true enquanto desenha pedacos de um quad subdividido
static int s_subTu, s_subTv;                  // bloco de textura (alinhado) do pedaco atual
#define D2_SUBQ_EDGE(a) s_subq   // s32/t32 = u/v em 1/32 de texel (permite subdividir quads)
static bool s_ctxTex;                         // contexto do lote atual (definido por begin_batch)
static int s_ctxTileW, s_ctxTileH;
static int s_ctxCx0, s_ctxCx1, s_ctxCy0, s_ctxCy1;
static int s_ctxSx16 = 65536, s_ctxSy16 = 65536;

static bool ensure_slots(void)
{
    if (s_slots) return true;
    s_slots = (TexSlot *)malloc(sizeof(TexSlot) * TEX_SLOTS);
    if (!s_slots) return false;
    memset(s_slots, 0, sizeof(TexSlot) * TEX_SLOTS);
    s_lastUp.clut = -1;
    return true;
}

static inline void begin_batch(bool tex, BlendMode bm)
{
    set_mode(tex, (int)s_texFormat, bm);
    s_ctxTex = tex;
    s_ctxTileW = 64; s_ctxTileH = s_texFormat == TF_4_BIT ? 64 : s_texFormat == TF_8_BIT ? 32 : 16;
    s_ctxCx0 = s_scx0 * 4; s_ctxCx1 = s_scx1 * 4; s_ctxCy0 = s_scy0 * 4; s_ctxCy1 = s_scy1 * 4;
    // escala tela do PS1 -> 320x240: so recalcula quando o tamanho do display muda (divisoes em float custam caro)
    static int lw = -1, lh = -1;
    if (activeDispEnv.disp.w != lw || activeDispEnv.disp.h != lh)
    {
        lw = activeDispEnv.disp.w; lh = activeDispEnv.disp.h;
        const float sx = lw > 0 ? (float)SCREEN_W / (float)lw : 1.0f;
        const float sy = lh > 0 ? (float)SCREEN_H / (float)lh : 1.0f;
        s_ctxSx16 = (int)(sx * 65536.0f + 0.5f); s_ctxSy16 = (int)(sy * 65536.0f + 0.5f);
    }
}

static void tri_core(const PV *A, const PV *B, const PV *C, int page, int clut)
{
#ifdef D2_XP_NOEMIT
#define rspq_write(...) ((void)0)
#define rdpq_write(...) ((void)0)
#endif
    const PV *v[3] = { A, B, C };
    int X4[3] = { A->X4, B->X4, C->X4 }, Y4[3] = { A->Y4, B->Y4, C->Y4 };
    uint32_t rgba[3] = { A->rgba, B->rgba, C->rgba };
    int mnx = X4[0], mxx = X4[0], mny = Y4[0], mxy = Y4[0];
    if (X4[1] < mnx) mnx = X4[1]; if (X4[1] > mxx) mxx = X4[1]; if (Y4[1] < mny) mny = Y4[1]; if (Y4[1] > mxy) mxy = Y4[1];
    if (X4[2] < mnx) mnx = X4[2]; if (X4[2] > mxx) mxx = X4[2]; if (Y4[2] < mny) mny = Y4[2]; if (Y4[2] > mxy) mxy = Y4[2];
    // fora do scissor?
    if (mxx < s_ctxCx0 || mnx >= s_ctxCx1 || mxy < s_ctxCy0 || mny >= s_ctxCy1) return;
    if (mxx - mnx > 16000 || mxy - mny > 16000) return;

    s_triCount++;
    if (!s_ctxTex)
    {
        uint32_t res = AUTOSYNC_PIPE;
        __rdpq_autosync_use(res);
        for (int i = 0; i < 3; i++)
            rspq_write(RDPQ_OVL_ID, RDPQ_CMD_TRIANGLE_DATA, 32 * i, ((uint32_t)(int16_t)X4[i] << 16) | ((uint32_t)Y4[i] & 0xFFFF), 0, rgba[i], 0, 0, 0);
        rdpq_write(12, RDPQ_OVL_ID, RDPQ_CMD_TRIANGLE, 0xC000 | ((RDPQ_CMD_TRI | 4) << 8));
        return;
    }
    s_texTriCount++;

    int smin = A->s32, smax = smin, tmin = A->t32, tmax = tmin;
    for (int i = 1; i < 3; i++)
    {
        if (v[i]->s32 < smin) smin = v[i]->s32; if (v[i]->s32 > smax) smax = v[i]->s32;
        if (v[i]->t32 < tmin) tmin = v[i]->t32; if (v[i]->t32 > tmax) tmax = v[i]->t32;
    }
    // texels usados: de floor(min) ate o texel que contem max (coordenada inteira = o proprio texel)
    const int umin = smin >> 5, vmin = tmin >> 5;
    int umax = (smax & 31) ? (smax >> 5) : (smax >> 5) - ((smax > smin) && D2_SUBQ_EDGE(A) ? 1 : 0), vmax = (tmax & 31) ? (tmax >> 5) : (tmax >> 5) - ((tmax > tmin) && D2_SUBQ_EDGE(A) ? 1 : 0);
    if (umax < umin) umax = umin; if (vmax < vmin) vmax = vmin;
    const int uw = umax - umin + 1, vh = vmax - vmin + 1;
    const int tileW = s_ctxTileW, tileH = s_ctxTileH;

    // quanto da tela cobre vs. quanto de textura: define o passo (como um "mip" por amostragem de ponto)
    int sarea = ((mxx - mnx) * (mxy - mny)) >> 4;
    if (sarea < 1) sarea = 1;
    const int tarea = uw * vh;
    int step = 1;
    if (tarea > tileW * tileH) { if (tarea >= 64 * sarea) step = 8; else if (tarea >= 16 * sarea) step = 4; else if (tarea >= 4 * sarea) step = 2; }

    if (s_subq)
    {
        // pedaco de quad subdividido: usa o bloco alinhado inteiro (a mesma regiao para todos os pedacos do bloco = acerto no cache)
        const int w = s_subTu + tileW > 256 ? 256 - s_subTu : tileW, h = s_subTv + tileH > 256 ? 256 - s_subTv : tileH;
        if (!upload_region(page, clut, s_subTu, s_subTv, w, h, 1)) return;
        __rdpq_autosync_use(AUTOSYNC_PIPE | AUTOSYNC_TILE(0) | AUTOSYNC_TMEM(0));
        for (int i = 0; i < 3; i++)
        {
            const int s = v[i]->s32 - s_upOu * 32, t = v[i]->t32 - s_upOv * 32;
            rspq_write(RDPQ_OVL_ID, RDPQ_CMD_TRIANGLE_DATA, 32 * i, ((uint32_t)(int16_t)X4[i] << 16) | ((uint32_t)Y4[i] & 0xFFFF), 0, rgba[i], ((uint32_t)(int16_t)s << 16) | ((uint32_t)t & 0xFFFF), 0x10000, 0x10000);
        }
        rdpq_write(20, RDPQ_OVL_ID, RDPQ_CMD_TRIANGLE, 0xC000 | ((RDPQ_CMD_TRI | 6) << 8));
        return;
    }
    if (uw <= tileW * step && vh <= tileH * step)
    {
        // cabe em um bloco so
        int w = uw + 1, h = vh + 1;
        if (umin + w > 256) w = 256 - umin;
        if (vmin + h > 256) h = 256 - vmin;
#ifdef D2_DPROF
        s_nTri++; const unsigned int tu0 = cnt0();
#endif
        if (!upload_region(page, clut, umin, vmin, w, h, step))
        {
            w = uw; h = vh;      // a margem de +1 estoura a TMEM em blocos de 64x64 (4 bits): tenta sem ela
            if (!upload_region(page, clut, umin, vmin, w, h, step)) return;
        }
#ifdef D2_DPROF
        s_upTicks += cnt0() - tu0;
#endif
        const int ou = step == 1 ? s_upOu : umin, ov = step == 1 ? s_upOv : vmin, mult = 32 / step;
        __rdpq_autosync_use(AUTOSYNC_PIPE | AUTOSYNC_TILE(0) | AUTOSYNC_TMEM(0));
        for (int i = 0; i < 3; i++)
        {
            const int s = step == 1 ? v[i]->s32 - ou * 32 : (v[i]->s32 - ou * 32) * mult / 32, t = step == 1 ? v[i]->t32 - ov * 32 : (v[i]->t32 - ov * 32) * mult / 32;
            rspq_write(RDPQ_OVL_ID, RDPQ_CMD_TRIANGLE_DATA, 32 * i, ((uint32_t)(int16_t)X4[i] << 16) | ((uint32_t)Y4[i] & 0xFFFF), 0, rgba[i], ((uint32_t)(int16_t)s << 16) | ((uint32_t)t & 0xFFFF), 0x10000, 0x10000);
        }
        rdpq_write(20, RDPQ_OVL_ID, RDPQ_CMD_TRIANGLE, 0xC000 | ((RDPQ_CMD_TRI | 6) << 8));
        return;
    }
    // grande demais: recorta o triangulo em blocos alinhados da textura (coordenadas ja na escala do RDP)
#ifdef D2_DPROF
    s_nClip++; const unsigned int tc0 = cnt0();
    if (d2_rdp_frames == 400) debugf("[clip] fmt=%d uv=%dx%d (u%d v%d) tela=%dx%d step=%d bm=%d\n", (int)s_texFormat, uw, vh, umin, vmin, (mxx - mnx) >> 2, (mxy - mny) >> 2, step, page); struct CT { unsigned int t; ~CT() { s_clipTicks += cnt0() - t; } } ct_{tc0};
#endif
    DV p[3];
    for (int i = 0; i < 3; i++)
    {
        p[i].x = X4[i] * 0.25f; p[i].y = Y4[i] * 0.25f; p[i].u = v[i]->s32 * (1.0f / 32); p[i].v = v[i]->t32 * (1.0f / 32);
        p[i].r = ((rgba[i] >> 24) + 0.5f) / 255.0f; p[i].g = (((rgba[i] >> 16) & 255) + 0.5f) / 255.0f; p[i].b = (((rgba[i] >> 8) & 255) + 0.5f) / 255.0f; p[i].a = 1.0f;
    }
    const int bw = tileW * step, bh = tileH * step;
    const int tu0 = (umin / bw) * bw, tv0 = (vmin / bh) * bh;
    for (int tv = tv0; tv <= vmax; tv += bh)
        for (int tu = tu0; tu <= umax; tu += bw)
        {
            DV a[10], b[10];
            int n = 3;
            memcpy(a, p, sizeof(p));
            n = clip_poly(a, n, b, 0, (float)tu, true);          if (n < 3) continue;
            n = clip_poly(b, n, a, 0, (float)(tu + bw), false);  if (n < 3) continue;
            n = clip_poly(a, n, b, 1, (float)tv, true);          if (n < 3) continue;
            n = clip_poly(b, n, a, 1, (float)(tv + bh), false);  if (n < 3) continue;
            int w = bw, h = bh;
            if (tu + w > 256) w = 256 - tu;
            if (tv + h > 256) h = 256 - tv;
            if (!upload_region(page, clut, tu, tv, w, h, step)) continue;
            for (int i = 1; i + 1 < n; i++) { emit_tri(true, a[0], a[i], a[i + 1], (float)(step == 1 ? s_upOu : tu), (float)(step == 1 ? s_upOv : tv), 1.0f / step, 1.0f, 1.0f); s_nSub++; }
        }
}

#ifdef D2_XP_NOEMIT
#undef rspq_write
#undef rdpq_write
#endif
static inline uint32_t shade_rgba(unsigned r, unsigned g, unsigned b)
{
    r = (r * 255) >> 7; g = (g * 255) >> 7; b = (b * 255) >> 7;
    if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
    return (r << 24) | (g << 16) | (b << 8) | 255;
}

void GR_DrawTriangles(int start, int count)
{
    if (!s_scene || s_offscreen || !s_verts || count <= 0) return;
    if (!ensure_slots()) return;
    s_calls++; s_subTri += count;
    begin_batch(s_textured && s_texFormat != TF_32_BIT_RGBA, s_blend);
    const int sx16 = s_ctxSx16, sy16 = s_ctxSy16;
    for (int t = 0; t < count; t++)
    {
        const GrVertex *g = &s_verts[start + t * 3];
        PV pv[3];
        for (int i = 0; i < 3; i++)
        {
            pv[i].X4 = ((int)g[i].x * sx16) >> 14;
            pv[i].Y4 = ((int)g[i].y * sy16) >> 14;
            pv[i].rgba = shade_rgba(g[i].r, g[i].g, g[i].b);
            pv[i].s32 = g[i].u << 5; pv[i].t32 = g[i].v << 5;
        }
        tri_core(&pv[0], &pv[1], &pv[2], g[0].page, g[0].clut);
    }
}


// Quad texturizado cujo UV (retangulo alinhado aos eixos) nao cabe na TMEM (ex.: ceu 128x84): divide o quad nas linhas dos blocos
// de textura (multiplos de tileW/tileH na pagina), interpolando posicao/cor. Substitui o recorte de poligonos em float.
static int bounds_list(int a32, int b32, int tile, int *out)
{
    // fracoes em ponto fixo 16.16 (0..65536) ao longo de a->b onde a coordenada cruza um multiplo de 'tile' texels (inclui 0 e 1)
    int n = 0; out[n++] = 0;
    const int lo = a32 < b32 ? a32 : b32, hi = a32 < b32 ? b32 : a32, step32 = tile * 32;
    if (hi > lo)
    {
        int tmp[16]; int m = 0;
        for (int b = (lo / step32 + 1) * step32; b < hi && m < 16; b += step32) tmp[m++] = (int)(((long long)(b - a32) << 16) / (b32 - a32));
        if (a32 > b32) for (int i = m - 1; i >= 0; i--) out[n++] = tmp[i]; else for (int i = 0; i < m; i++) out[n++] = tmp[i];
    }
    out[n++] = 65536;
    return n;
}
static inline int lerp16(int a, int b, int f) { return a + (int)(((long long)(b - a) * f) >> 16); }
static inline uint32_t lerp_rgba(uint32_t a, uint32_t b, int f)
{
    if (a == b) return a;
    uint32_t r = 255;
    for (int sh = 8; sh < 32; sh += 8) r |= (uint32_t)lerp16((a >> sh) & 255, (b >> sh) & 255, f) << sh;
    return r;
}
static inline PV bilerp(const PV *q, int fs, int ft)
{
    PV r;
    const int x0 = lerp16(q[0].X4, q[1].X4, fs), x1 = lerp16(q[2].X4, q[3].X4, fs);
    const int y0 = lerp16(q[0].Y4, q[1].Y4, fs), y1 = lerp16(q[2].Y4, q[3].Y4, fs);
    const int s0 = lerp16(q[0].s32, q[1].s32, fs), s1 = lerp16(q[2].s32, q[3].s32, fs);
    const int t0 = lerp16(q[0].t32, q[1].t32, fs), t1 = lerp16(q[2].t32, q[3].t32, fs);
    r.X4 = lerp16(x0, x1, ft); r.Y4 = lerp16(y0, y1, ft); r.s32 = lerp16(s0, s1, ft); r.t32 = lerp16(t0, t1, ft);
    r.rgba = lerp_rgba(lerp_rgba(q[0].rgba, q[1].rgba, fs), lerp_rgba(q[2].rgba, q[3].rgba, fs), ft);
    return r;
}
static bool subdiv_quad(const PV *q, int page, int clut)
{
    const int tw = s_ctxTileW, th = s_ctxTileH;
    // retangulo alinhado: caso A (u varia com s = 0->1, v com t = 0->2) ou caso B (u com t, v com s)
    const bool A = q[0].s32 == q[2].s32 && q[1].s32 == q[3].s32 && q[0].t32 == q[1].t32 && q[2].t32 == q[3].t32;
    const bool B = q[0].s32 == q[1].s32 && q[2].s32 == q[3].s32 && q[0].t32 == q[2].t32 && q[1].t32 == q[3].t32;
    if (!A && !B) return false;
    const int us0 = q[0].s32, us1 = A ? q[1].s32 : q[2].s32;     // u ao longo do eixo que varia
    const int vs0 = q[0].t32, vs1 = A ? q[2].t32 : q[1].t32;
    const int uspan = (us1 > us0 ? us1 - us0 : us0 - us1) >> 5, vspan = (vs1 > vs0 ? vs1 - vs0 : vs0 - vs1) >> 5;
    if (uspan + 2 <= tw && vspan + 2 <= th) return false;        // ja cabe
    int fu[18], fv[18];
    const int nu = bounds_list(us0, us1, tw, fu), nv = bounds_list(vs0, vs1, th, fv);
    s_subq = true;
    for (int j = 0; j + 1 < nv; j++)
        for (int i = 0; i + 1 < nu; i++)
        {
            // fracoes (s ao longo de 0->1, t ao longo de 0->2) do sub-quad
            int s0, s1, t0, t1;
            if (A) { s0 = fu[i]; s1 = fu[i + 1]; t0 = fv[j]; t1 = fv[j + 1]; }
            else   { s0 = fv[j]; s1 = fv[j + 1]; t0 = fu[i]; t1 = fu[i + 1]; }
            PV p[4] = { bilerp(q, s0, t0), bilerp(q, s1, t0), bilerp(q, s0, t1), bilerp(q, s1, t1) };
            {   // bloco alinhado que contem o pedaco (pelo centro do pedaco)
                const int cs = (p[0].s32 + p[1].s32 + p[2].s32 + p[3].s32) >> 7, ct = (p[0].t32 + p[1].t32 + p[2].t32 + p[3].t32) >> 7;
                s_subTu = (cs / tw) * tw; s_subTv = (ct / th) * th;
            }
            tri_core(&p[0], &p[1], &p[2], page, clut);
            tri_core(&p[1], &p[2], &p[3], page, clut);
        }
    s_subq = false;
    return true;
}

#ifdef D2_DIRECT
// ---------------------------------------------------------------- caminho direto: poligono do PS1 (OT) -> RDP, sem GrVertex/lotes
// Chamado por ProcessFlatPoly/ProcessGouraudPoly (PsyX_GPU.cpp). Devolve o tamanho do primitivo (em palavras) ou 0 para o caminho antigo.
extern TextureID overrideTexture;
void d2_flush_splits(void);           // PsyX_GPU.cpp: desenha o que ja estava em lotes (mantem a ordem da OT)
static unsigned int s_dCnt;
int d2_direct_poly_i(P_TAG *tag);
int d2_native_ft4(POLY_FT4 *p)
{
    if (!activeDrawEnv.dfe || !s_scene) return 9;
    if (!ensure_slots()) return 9;
    d2_flush_splits();
    const RECT16 &c = activeDrawEnv.clip;
    if (c.x != s_clipKey[0] || c.y != s_clipKey[1] || c.w != s_clipKey[2] || c.h != s_clipKey[3] ||
        (activeDispEnv.disp.x | (activeDispEnv.disp.w << 16)) != s_clipKey[4] ||
        (activeDispEnv.disp.y | (activeDispEnv.disp.h << 16)) != s_clipKey[5]) GR_SetupClipMode(&c, 1);
    activeDrawEnv.tpage = p->tpage;
    s_texFormat = (TexFormat)((p->tpage >> 7) & 3);
    begin_batch(s_texFormat != TF_32_BIT_RGBA, BM_NONE);
    const int ofx = activeDrawEnv.ofs[0] - activeDispEnv.disp.x;
    const int ofy = activeDrawEnv.ofs[1] - activeDispEnv.disp.y;
    const short *xy[4] = { &p->x0, &p->x1, &p->x2, &p->x3 };
    const unsigned char *uv[4] = { &p->u0, &p->u1, &p->u2, &p->u3 };
    const uint32_t col = shade_rgba(p->r0, p->g0, p->b0);
    PV q[4];
    for (int i = 0; i < 4; i++)
    {
        q[i].X4 = ((xy[i][0] + ofx) * s_ctxSx16) >> 14;
        q[i].Y4 = ((xy[i][1] + ofy) * s_ctxSy16) >> 14;
        q[i].rgba = col; q[i].s32 = uv[i][0] << 5; q[i].t32 = uv[i][1] << 5;
    }
    tri_core(&q[0], &q[1], &q[2], p->tpage, p->clut);
    tri_core(&q[1], &q[2], &q[3], p->tpage, p->clut);
    return 9;
}
int d2_direct_poly(P_TAG *tag)
{
#ifdef D2_DPROF
    const unsigned int t0 = cnt0(); const int r = d2_direct_poly_i(tag); s_dTicks += cnt0() - t0; s_dCntP++; return r;
}
int d2_direct_poly_i(P_TAG *tag)
{
#endif
    const int code = tag->code;
    const bool tex = (code & 0x04) != 0, quad = (code & 0x08) != 0, gour = (code & 0x10) != 0;
    const bool semi = (code & 0x02) != 0, shadeOn = (code & 0x01) == 0;
    const int len = gour ? (tex ? (quad ? 12 : 9) : (quad ? 8 : 6)) : (tex ? (quad ? 9 : 7) : (quad ? 5 : 4));
    if (tex && overrideTexture != 0) return 0;

    // decodifica os vertices (o layout muda com gouraud/textura)
    const unsigned int *w = (const unsigned int *)tag + P_LEN;     // primeira palavra = cor0 + code
    const int nv = quad ? 4 : 3;
    const int stride = (gour ? 1 : 0) + 1 + (tex ? 1 : 0);          // palavras por vertice
    int clut = 0, tpage = 0;
    short xs[4], ys[4]; unsigned char us[4] = {0,0,0,0}, vs[4] = {0,0,0,0}, cr[4], cg[4], cb[4];
    for (int i = 0; i < nv; i++)
    {
        const unsigned char *c = (const unsigned char *)(gour ? (w + i * stride) : w);
        const unsigned int *xy = gour ? (w + i * stride + 1) : (w + 1 + i * stride);
        const short *pxy = (const short *)xy;
        xs[i] = pxy[0]; ys[i] = pxy[1];
        cr[i] = c[0]; cg[i] = c[1]; cb[i] = c[2];
        if (tex)
        {
            const unsigned char *uvp = (const unsigned char *)(xy + 1);
            us[i] = uvp[0]; vs[i] = uvp[1];
            if (i == 0) clut = ((const unsigned short *)uvp)[1];
            if (i == 1) tpage = ((const unsigned short *)uvp)[1];
        }
    }
    if (tex)
    {
        activeDrawEnv.tpage = tpage;
        // POLY_FT3 nulo: so troca a tpage (truque oficial da SCE)
        if (!quad && !gour && xs[0] == -1 && ys[0] == -1 && xs[1] == -1 && ys[1] == -1 && xs[2] == -1 && ys[2] == -1) return len;
    }
    if (!activeDrawEnv.dfe || !s_scene) return len;             // fora da tela: o caminho antigo tambem descarta
    if (!ensure_slots()) return len;
    d2_flush_splits();

    // recorte (clip) do ambiente de desenho
    {
        const RECT16 &c = activeDrawEnv.clip;
        if (c.x != s_clipKey[0] || c.y != s_clipKey[1] || c.w != s_clipKey[2] || c.h != s_clipKey[3] ||
            (activeDispEnv.disp.x | (activeDispEnv.disp.w << 16)) != s_clipKey[4] || (activeDispEnv.disp.y | (activeDispEnv.disp.h << 16)) != s_clipKey[5])
            GR_SetupClipMode(&c, 1);
    }
    s_texFormat = tex ? (TexFormat)((tpage >> 7) & 3) : s_texFormat;
    const BlendMode bm = semi ? (BlendMode)(((activeDrawEnv.tpage >> 5) & 3) + 1) : BM_NONE;
    const bool texOk = tex && s_texFormat != TF_32_BIT_RGBA;
    begin_batch(texOk, bm);
    s_dCnt++;

    const int ofx = activeDrawEnv.ofs[0] - activeDispEnv.disp.x, ofy = activeDrawEnv.ofs[1] - activeDispEnv.disp.y;
    PV pv[4];
    for (int i = 0; i < nv; i++)
    {
        pv[i].X4 = ((int)(xs[i] + ofx) * s_ctxSx16) >> 14;
        pv[i].Y4 = ((int)(ys[i] + ofy) * s_ctxSy16) >> 14;
        pv[i].rgba = shadeOn ? shade_rgba(cr[i], cg[i], cb[i]) : shade_rgba(128, 128, 128);
        pv[i].s32 = us[i] << 5; pv[i].t32 = vs[i] << 5;
    }
#ifdef D2_DPROF
    const unsigned int t1 = cnt0();
#endif
#ifndef D2_NOSUBQ
    if (quad && texOk && subdiv_quad(pv, tpage, clut)) {}
    else
#endif
    if (0) {}
    else
    {
        tri_core(&pv[0], &pv[1], &pv[2], tpage, clut);
        if (quad) tri_core(&pv[1], &pv[2], &pv[3], tpage, clut);
    }
#ifdef D2_DPROF
    s_tTicks += cnt0() - t1;
#endif
    return len;
}
#endif

} // extern "C"
