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
#include "d2_origin.h"

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

// Fase 4: Instrumentacao detalhada de cobertura e diagnostico
static unsigned int s_ft4Total = 0;
static unsigned int s_ft4Rsp = 0;
static unsigned int s_ft4Cpu = 0;
static unsigned int s_ft4Ci4Eligible = 0;
static unsigned int s_ft4RejDim = 0;
static unsigned int s_ft4RejFmt = 0;
static unsigned int s_ft4RejClip = 0;
static unsigned int s_ft4RejScale = 0;
static unsigned int s_ft4RejUp = 0;
static unsigned int s_ft3Total = 0;
static unsigned int s_directFt4Total = 0;
static unsigned int s_ovlSwitches = 0;
static uint32_t s_lastOvlSent = 0xFFFFFFFF;

static inline void track_ovl_switch(uint32_t ovl) {
    if (ovl != s_lastOvlSent) {
        s_ovlSwitches++;
        s_lastOvlSent = ovl;
    }
}
#if D2_RSP_GPU
#include <rsp.h>
#include <rspq.h>
DEFINE_RSP_UCODE(rsp_d2gpu);
static uint32_t s_d2gpuOvl = 0;
static int s_rspOfx = 0x7FFFFFFF;
static int s_rspOfy = 0x7FFFFFFF;
static int s_rspTexOu = 0x7FFFFFFF;
static int s_rspTexOv = 0x7FFFFFFF;
static unsigned int s_rspF3Count = 0;
static unsigned int s_rspF4Count = 0;
static unsigned int s_rspFT4Count = 0;
static unsigned int s_ft4Fallback = 0;

enum {
    D2G_CMD_SET_OFFSET     = 0,
    D2G_CMD_SET_TEX_ORIGIN = 1,
    D2G_CMD_POLY_F3        = 2,
    D2G_CMD_POLY_F4        = 3,
    D2G_CMD_POLY_FT3       = 4,
    D2G_CMD_POLY_FT4       = 5,
    D2G_CMD_DRAW_END       = 6,
    D2G_CMD_SET_TILE_PAL   = 7,
};
#endif

static void rdp_init_once(void)
{
    if (s_rdpInit) return;
    s_rdpInit = true;
    rdpq_init();
#if D2_RSP_GPU
    s_d2gpuOvl = rspq_overlay_register(&rsp_d2gpu);
    debugf("[rsp_ovl] registered rsp_d2gpu ucode ovl_id=0x%08lx\n", (unsigned long)s_d2gpuOvl);
#endif
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
#define TEX_BYTES 4096
struct TexSlot
{
    unsigned int key0, key1, key2;     // identificam a regiao e variante aditiva/CLUT
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

static int s_curRgbaMode = 0;
static BlendMode s_curBlend = BM_NONE;

static inline unsigned short conv_color(unsigned short v)
{
    // PS1: xBBBBBGGGGGRRRRR (0x0000 = transparente) -> RDP: RRRRRGGGGGBBBBBA
    return (unsigned short)(((v & 31) << 11) | (((v >> 5) & 31) << 6) | (((v >> 10) & 31) << 1) | (v != 0));
}

static inline uint32_t psx_to_rgba32(unsigned short v)
{
    if (v == 0) return 0;
    const int r5 = v & 31, g5 = (v >> 5) & 31, b5 = (v >> 10) & 31;
    const int r = (r5 << 3) | (r5 >> 2);
    const int g = (g5 << 3) | (g5 >> 2);
    const int b = (b5 << 3) | (b5 >> 2);
    const int maxc = r > g ? (r > b ? r : b) : (g > b ? g : b);
    if (maxc == 0) return 0;
    int r_norm = (r * 255) / maxc;
    int g_norm = (g * 255) / maxc;
    int b_norm = (b * 255) / maxc;
    if (r_norm > 255) r_norm = 255;
    if (g_norm > 255) g_norm = 255;
    if (b_norm > 255) b_norm = 255;
    return ((uint32_t)r_norm << 24) | ((uint32_t)g_norm << 16) | ((uint32_t)b_norm << 8) | (uint32_t)maxc;
}

struct PalEnt { int clut; unsigned int sig, gen, use, vw; int valid, bank; unsigned short pal[16] __attribute__((aligned(16))); };
#define PAL_CACHE 1024
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
#ifdef D2_DPROF
static unsigned int s_rejScissor, s_rejScreenClip, s_rejUvClip, s_rejUpload, s_rejDegen;
#endif
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
static TexSlot *get_region(int px, int py, int fmt, int u0, int v0, int w, int h, int step, int *outW, int *outH, int *outWpad, int clut, int rgba_mode)
{
    const bool is_rgba32 = (rgba_mode != 0);
    const int sw = (w + step - 1) / step, sh = (h + step - 1) / step;
    int wpad;
    if (is_rgba32) wpad = (sw + 1) & ~1;
    else if (fmt == TF_4_BIT) wpad = (sw + 15) & ~15;
    else if (fmt == TF_8_BIT) wpad = (sw + 7) & ~7;
    else wpad = (sw + 3) & ~3;
    int bytes = is_rgba32 ? (wpad * 4 * sh) : (fmt == TF_4_BIT ? (wpad / 2) * sh : fmt == TF_8_BIT ? wpad * sh : wpad * 2 * sh);
    if (w <= 0 || h <= 0) return 0;
    if (is_rgba32)
    {
        if (((sw * 2 + 7) & ~7) * sh > 2048) return 0;
        if (bytes > TEX_BYTES) return 0;
    }
    else
    {
        if (bytes > 2048) return 0;   // com paleta: a metade alta da TMEM e da TLUT
    }
    *outW = sw; *outH = sh; *outWpad = wpad;

    unsigned int k0 = (unsigned)px | ((unsigned)py << 10) | ((unsigned)fmt << 20) | ((unsigned)step << 24) | ((unsigned)rgba_mode << 28);
    unsigned int k1 = ((unsigned)u0 & 0xFF) | (((unsigned)v0 & 0xFF) << 8) | (((unsigned)(w > 0 ? w - 1 : 0) & 0xFF) << 16) | (((unsigned)(h > 0 ? h - 1 : 0) & 0xFF) << 24);
    unsigned int k2 = is_rgba32 ? ((unsigned int)(clut & 0xFFFF) | ((unsigned int)rgba_mode << 16)) : 0;
    unsigned int hsh = (k0 * 0x9E3779B1u) ^ (k1 * 0x85EBCA6Bu) ^ (k2 * 0xC2B2AE35u);
    hsh ^= hsh >> 15; hsh *= 0x2C1B3C6Du; hsh ^= hsh >> 13;
    const unsigned int base = hsh % TEX_SLOTS;

    // palavras de VRAM que a regiao toca (para a assinatura)
    int wx, ww;
    if (fmt == TF_4_BIT) { wx = px + (u0 >> 2); ww = ((u0 + w - 1) >> 2) - (u0 >> 2) + 1; }
    else if (fmt == TF_8_BIT) { wx = px + (u0 >> 1); ww = ((u0 + w - 1) >> 1) - (u0 >> 1) + 1; }
    else { wx = px + u0; ww = w; }
    unsigned int sig = cell_signature(wx, py + v0, ww, h);
    if (is_rgba32 && fmt != TF_16_BIT)
    {
        const int cx = (clut & 63) * 16, cy = clut >> 6;
        sig ^= cell_signature(cx, cy, fmt == TF_4_BIT ? 16 : 256, 1);
    }

    TexSlot *s = 0;
    for (int i = 0; i < 4; i++)
    {
        TexSlot *c = &s_slots[(base + i) % TEX_SLOTS];
        if (c->valid && c->key0 == k0 && c->key1 == k1 && c->key2 == k2 && c->sig == sig) { c->use = ++s_useCounter; s_dbgHit++; return c; }
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
    if (is_rgba32)
    {
        uint32_t *d32 = (uint32_t *)dst;
        const int cx = (clut & 63) * 16, cy = clut >> 6;
        const unsigned short *pal = (fmt != TF_16_BIT) ? (vram + ((cy & 511) << 10)) : NULL;
        for (int y = 0; y < sh; y++)
        {
            const unsigned short *row = vram + (((py + v0 + y * step) & 511) << 10);
            uint32_t *d = d32 + y * wpad;
            if (fmt == TF_4_BIT)
            {
                for (int x = 0; x < sw; x++)
                {
                    int u = u0 + x * step;
                    unsigned int word = row[(px + (u >> 2)) & 1023];
                    int idx = (word >> ((u & 3) * 4)) & 15;
                    unsigned short psx_col = pal[(cx + idx) & 1023];
                    d[x] = psx_to_rgba32(psx_col);
                }
            }
            else if (fmt == TF_8_BIT)
            {
                for (int x = 0; x < sw; x++)
                {
                    int u = u0 + x * step;
                    unsigned int word = row[(px + (u >> 1)) & 1023];
                    int idx = (word >> ((u & 1) * 8)) & 255;
                    unsigned short psx_col = pal[(cx + idx) & 1023];
                    d[x] = psx_to_rgba32(psx_col);
                }
            }
            else
            {
                for (int x = 0; x < sw; x++)
                {
                    int u = u0 + x * step;
                    unsigned short psx_col = row[(px + u) & 1023];
                    d[x] = psx_to_rgba32(psx_col);
                }
            }
            for (int pb = sw; pb < wpad; pb++) d[pb] = 0;
        }
    }
    else
    {
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
    }
    data_cache_hit_writeback(dst, (bytes + 15) & ~15);
    s->key0 = k0; s->key1 = k1; s->key2 = k2; s->sig = sig; s->valid = 1; s->gen = ++s_genCounter; s->use = ++s_useCounter;
    return s;
}

#ifdef D2_TEXWIN
static bool s_leanTiles;              // TILE0 (CI4 64x64) e TILE6 (carga I8) configurados para a janela
static bool s_tmemWin;                // a TMEM (metade baixa) contem a janela s_win
static bool s_tmemDirty = true;       // TMEM ou TILE alterado: requer sincronizacao na proxima primitiva
// ---------------------------------------------------------------- bancos de paleta na TMEM (CI4): 16 bancos de 16 cores; o banco 0 fica
// para o caminho antigo, 1..15 sao um cache LRU (clut -> banco). Acerto = so troca o banco no TILE0 (1 comando).
static struct { int clut; unsigned int gen, lru; } s_bank[16];
static unsigned int s_bankLru;
static int s_curBank = -1;            // banco configurado no TILE0 da janela (-1 = desconhecido)
static int s_pbClut = -1; static unsigned int s_pbVw;
static void banks_invalidate(void)
{
    for (int i = 0; i < 16; i++) s_bank[i].clut = -1;
    s_curBank = -1;
    s_pbClut = -1;
    s_lastUp.clut = -1;
    s_lastUp.palEnt = 0;
    s_lastUp.palGen = 0;
}
#endif

// ---------------------------------------------------------------- modos do RDP
#ifdef D2_ORIGIN
static bool s_lastSetModeTextured = false;
static int s_lastSetModeFmt = 0;
static int s_lastSetModeBlend = 0;
#endif
static void set_mode(bool textured, int fmt, BlendMode blend)
{
#ifdef D2_ORIGIN
    s_lastSetModeTextured = textured;
    s_lastSetModeFmt = fmt;
    s_lastSetModeBlend = (int)blend;
#endif
    int rgba_mode = 0;
    if (blend == BM_ADD) rgba_mode = 1;
    else if (blend == BM_ADD_QUATER_SOURCE) rgba_mode = 2;
    else if (blend == BM_SUBTRACT) rgba_mode = 3;

    if (rgba_mode != s_curRgbaMode)
    {
        s_lastUp.clut = -1;
        s_lastUp.palEnt = 0;
#ifdef D2_TEXWIN
#ifdef D2_PALBANK
        banks_invalidate();
#endif
#endif
        s_curRgbaMode = rgba_mode;
    }
    s_curBlend = blend;
    int key = textured ? (1 + fmt) * 8 + (int)blend : (int)blend;
    if (key == s_modeKey) return;
    s_modeKey = key;
    s_lastUp.slot = 0;     // o estado de textura se perde ao trocar de modo (palette/tlut)
    s_lastUp.clut = -1;
    s_lastUp.palEnt = 0;
#ifdef D2_TEXWIN
    s_leanTiles = false;
    s_tmemWin = false;
    s_winBase = -1;
#ifdef D2_PALBANK
    s_curBank = -1;
    s_pbClut = -1;
#endif
#endif

    rdpq_set_mode_standard();
    rdpq_mode_antialias(AA_NONE);
    rdpq_mode_dithering(DITHER_NONE_NONE);
    rdpq_mode_filter(FILTER_POINT);

    const bool isRgba32 = (rgba_mode != 0);
    const bool isSub = (blend == BM_SUBTRACT);

    int pa = 0;
    if (blend == BM_AVERAGE) pa = 128;

    if (isRgba32)
    {
        if (textured)
        {
            if (isSub)
                rdpq_mode_combiner(RDPQ_COMBINER1((ZERO, ZERO, ZERO, ZERO), (TEX0, ZERO, SHADE, ZERO)));
            else
                rdpq_mode_combiner(RDPQ_COMBINER1((TEX0, ZERO, SHADE, ZERO), (TEX0, ZERO, SHADE, ZERO)));
            rdpq_mode_alphacompare(1);
            rdpq_mode_tlut(TLUT_NONE);
            rdpq_mode_blender(RDPQ_BLENDER((IN_RGB, IN_ALPHA, MEMORY_RGB, INV_MUX_ALPHA)));
        }
        else
        {
            if (isSub)
                rdpq_mode_combiner(RDPQ_COMBINER1((ZERO, ZERO, ZERO, ZERO), (ZERO, ZERO, ZERO, SHADE)));
            else
                rdpq_mode_combiner(RDPQ_COMBINER1((ZERO, ZERO, ZERO, SHADE), (ZERO, ZERO, ZERO, SHADE)));
            rdpq_mode_alphacompare(1);
            rdpq_mode_blender(RDPQ_BLENDER((IN_RGB, IN_ALPHA, MEMORY_RGB, INV_MUX_ALPHA)));
        }
    }
    else if (textured)
    {
        if (pa) rdpq_mode_combiner(RDPQ_COMBINER1((TEX0, ZERO, SHADE, ZERO), (TEX0, ZERO, PRIM, ZERO)));
        else    rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE);
        rdpq_mode_alphacompare(1);
        rdpq_mode_tlut(fmt == TF_16_BIT ? TLUT_NONE : TLUT_RGBA16);
        if (pa)
        {
            rdpq_set_prim_color(RGBA32(255, 255, 255, pa));
            rdpq_mode_blender(RDPQ_BLENDER((IN_RGB, IN_ALPHA, MEMORY_RGB, INV_MUX_ALPHA)));
        }
        else rdpq_mode_blender(0);
    }
    else
    {
        if (pa)
        {
            rdpq_mode_combiner(RDPQ_COMBINER1((ZERO, ZERO, ZERO, SHADE), (ZERO, ZERO, ZERO, PRIM)));
            rdpq_set_prim_color(RGBA32(255, 255, 255, pa));
            rdpq_mode_blender(RDPQ_BLENDER((IN_RGB, IN_ALPHA, MEMORY_RGB, INV_MUX_ALPHA)));
        }
        else
        {
            rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
            rdpq_mode_blender(0);
        }
    }
}


#ifdef D2_TEXWIN
static void upload_pal_bank(int clut)
{
    const int pkey = clut;
    if (pkey == s_pbClut && s_pbVw == s_vramWrites && s_curBank > 0 && s_bank[s_curBank].clut == pkey) { s_bank[s_curBank].lru = ++s_bankLru; return; }
    s_pbClut = pkey; s_pbVw = s_vramWrites;
    PalEnt *e = &s_palCache[(pkey ^ (pkey >> 8)) & (PAL_CACHE - 1)];
    const int cx = (clut & 63) * 16, cy = (clut & 0xFFFF) >> 6;
    bool ok = e->valid && e->clut == pkey;
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
        e->clut = pkey; e->sig = cell_signature(cx, cy, 16, 1); e->vw = s_vramWrites; e->valid = 1; e->gen = ++s_genCounter;
    }
    int b = 0;
    if (e->bank > 0 && e->bank < 16 && s_bank[e->bank].clut == pkey && s_bank[e->bank].gen == e->gen)
    {
        b = e->bank;
    }
    else
    {
        for (int i = 1; i < 16; i++) {
            if (s_bank[i].clut == pkey && s_bank[i].gen == e->gen) {
                b = i;
                break;
            }
        }
    }
    if (b) s_cBankHitX++;
    else
    {
        track_ovl_switch(0);
        b = 1;
        for (int i = 2; i < 16; i++) if (s_bank[i].lru < s_bank[b].lru) b = i;
        rdpq_tex_upload_tlut(e->pal, b * 16, 16); s_cTlut++; s_winBase = -1;
        e->use = ++s_useCounter;
        s_bank[b].clut = pkey; s_bank[b].gen = e->gen;
        s_tmemDirty = true;
        s_lastUp.clut = -1;
    }
    e->bank = b;
    s_bank[b].lru = ++s_bankLru;
    if (b != s_curBank)
    {
#if D2_RSP_GPU
        if (s_d2gpuOvl)
        {
            track_ovl_switch(s_d2gpuOvl);
            rspq_write(s_d2gpuOvl, D2G_CMD_SET_TILE_PAL, 0, (uint32_t)b);
            s_curBank = b;
            s_tmemDirty = true;
        }
        else
#endif
        {
            track_ovl_switch(0);
            rdpq_tileparms_t tp = {}; tp.palette = b;
            rdpq_set_tile(TILE0, FMT_CI4, 0, 32, &tp);
            s_curBank = b;
            s_tmemDirty = true;
        }
    }
}
#endif

// ---------------------------------------------------------------- D2_TEXWIN: janela fixa de 64x64 texels (CI4, 2 KB) na TMEM
// Em vez de carregar o retangulo exato de cada triangulo, carrega uma janela de 64x64 da VRAM sombra e mantem enquanto os
// triangulos seguintes couberem nela (mesma pagina, VRAM nao alterada). A carga e um unico LOAD_TILE (tiles fixos).
static struct { int px, py, wu0, wv0; unsigned int sig, vw; } s_win;
static TexSlot s_winTag;
#ifdef D2_TEXWIN
#ifndef D2_WINALIGN
#define D2_WINALIGN 16
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

    // Origem centralizada e alinhada a 16 para cobrir o maximo de quads vizinhos do mesmo modelo
    int opt_wu = (u0 + (w >> 1)) - 32;
    if (opt_wu < 0) opt_wu = 0;
    if (opt_wu > 192) opt_wu = 192;
    int wu0 = opt_wu & ~15;
    if (u0 < wu0) wu0 = u0 & ~15;
    if (u0 + w > wu0 + 64) wu0 = ((u0 + w - 64) + 15) & ~15;
    if (wu0 + 64 > 256) wu0 = 192;
    if (wu0 < 0) wu0 = 0;

    int opt_wv = (v0 + (h >> 1)) - 32;
    if (opt_wv < 0) opt_wv = 0;
    if (opt_wv > 192) opt_wv = 192;
    int wv0 = opt_wv & ~15;
    if (v0 < wv0) wv0 = v0 & ~15;
    if (v0 + h > wv0 + 64) wv0 = ((v0 + h - 64) + 15) & ~15;
    if (wv0 + 64 > 256) wv0 = 192;
    if (wv0 < 0) wv0 = 0;
    shadow_sync_all();
    s_win.px = px; s_win.py = py; s_win.wu0 = wu0; s_win.wv0 = wv0; s_win.vw = s_vramWrites;
    s_shLastUse = ++s_useCounter;
    if (!s_leanTiles)
    {
        track_ovl_switch(0);
        s_winBase = -1;
        rdpq_set_tile(TILE6, FMT_I8, 0, 32, NULL);
        rdpq_set_tile(TILE0, FMT_CI4, 0, 32, NULL);
        rdpq_set_tile_size(TILE0, 0, 0, 64, 64);
        s_leanTiles = true;
        s_curBank = 0;
#ifdef D2_PALBANK
        upload_pal_bank(clut);       // TILE0 foi refeito com o banco 0: reaplica o banco da paleta atual
#endif
        s_tmemDirty = true;
    }
    // coordenadas de carga em bytes da sombra (2 texels CI4 por byte): coluna = ((px-512)*4 + wu0)/2
    // o RDP limita coordenadas de carga a < 1024: a imagem comeca no byte 0 ou 512 da linha (troca so quando muda)
    int bx = ((px - 512) * 4 + wu0) >> 1; const int by = py + wv0;
    const int base = bx >= 512 ? 512 : 0;
    if (base != s_winBase) { track_ovl_switch(0); rdpq_set_texture_image_raw(0, PhysicalAddr((const char *)s_shadow + base), FMT_I8, SH_W * 2, 512); s_winBase = base; }
    bx -= base;
    track_ovl_switch(0);
    rdpq_load_tile(TILE6, bx, by, bx + 32, by + 64);
    s_cLdWin++;
    s_tmemWin = true;
    s_tmemDirty = true;
    s_upOu = wu0; s_upOv = wv0; s_lastUp.slot = &s_winTag;
    return true;
}
#endif

// carrega a regiao (u0,v0,w,h, passo step) da pagina de textura na TMEM (paleta inclusa). Retorna false se nao coube.
static bool upload_region(int page, int clut, int u0, int v0, int w, int h, int step)
{
    if (w <= 0 || h <= 0) return false;
    const int px = (page & 15) * 64, py = ((page >> 4) & 1) * 256;
#ifdef D2_TEXWIN
    if (!s_curRgbaMode && s_texFormat == TF_4_BIT && step == 1 && px >= 512 && w <= 64 && h <= 64)
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
    if (!s_curRgbaMode && s_texFormat == TF_4_BIT && step == 1 && px >= 512)
    {
        ua = u0 & ~15;
        e0 = (u0 - ua) & ~1; e1 = ((u0 - ua) + w + 1) & ~1;
        useShadow = ((((e1 - e0) >> 1) + 7) & ~7) * h <= 2048;
    }
    TexSlot *slot = useShadow ? &s_shTag : get_region(px, py, s_texFormat, u0, v0, w, h, step, &sw, &sh, &wpad, clut, s_curRgbaMode);
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

    if (!s_curRgbaMode)
    {
        const bool paletted = s_texFormat != TF_16_BIT;
        const int pkey = clut;
#ifndef D2_NOPALCACHE
        if (s_texFormat == TF_4_BIT)
        {
            // paletas de 16 cores: cache por CLUT (convertidas uma vez; invalidadas pela versao das celulas da VRAM)
            const int cx = (clut & 63) * 16, cy = (clut & 0xFFFF) >> 6;
            PalEnt *e = &s_palCache[(pkey ^ (pkey >> 8)) & (PAL_CACHE - 1)];
            const unsigned int sig = cell_signature(cx, cy, 16, 1);
            if (!e->valid || e->clut != pkey || e->sig != sig)
            {
                if (e->use > s_doneCounter) rdp_wait_idle();          // o RDP ainda pode estar lendo esta paleta
                const unsigned short *src = vram + ((cy & 511) << 10);
                for (int i = 0; i < 16; i++) e->pal[i] = conv_color(src[(cx + i) & 1023]);
                data_cache_hit_writeback(e->pal, 32);
                e->clut = pkey; e->sig = sig; e->valid = 1; e->gen = ++s_genCounter;
            }
            if (s_lastUp.slot == 0 || s_lastUp.fmt != s_texFormat || s_lastUp.palEnt != e || s_lastUp.palGen != e->gen || s_lastUp.clut != pkey)
            {
                rdpq_tex_upload_tlut(e->pal, 0, 16); s_cTlut++; s_winBase = -1; s_bank[0].clut = -1;
                e->use = ++s_useCounter;
                s_lastUp.fmt = s_texFormat; s_lastUp.palEnt = e; s_lastUp.palGen = e->gen; s_lastUp.clut = pkey;
            }
        }
#else
        if (false) {}
#endif
        else if (paletted && (s_lastUp.clut != pkey || s_lastUp.fmt != s_texFormat || s_lastUp.palVer != s_vramWrites || s_lastUp.slot == 0))
        {
            const int cx = (clut & 63) * 16, cy = (clut & 0xFFFF) >> 6;
            const unsigned short *src = vram + ((cy & 511) << 10);
            const int n = s_texFormat == TF_4_BIT ? 16 : 256;
            s_palIdx = (s_palIdx + 1) % PAL_RING;
            if (s_palUse[s_palIdx] > s_doneCounter) rdp_wait_idle();
            unsigned short *pal = s_palRing[s_palIdx];
            for (int i = 0; i < n; i++) pal[i] = conv_color(src[(cx + i) & 1023]);
            data_cache_hit_writeback(pal, n * 2);
            rdpq_tex_upload_tlut(pal, 0, n); s_cTlut++; s_winBase = -1; if (n > 16) banks_invalidate(); else s_bank[0].clut = -1;
#ifdef D2_PALBANK
            if (n > 16) banks_invalidate();
#endif
            s_palUse[s_palIdx] = ++s_useCounter;
            s_lastUp.clut = pkey; s_lastUp.fmt = s_texFormat; s_lastUp.palVer = s_vramWrites; s_lastUp.palEnt = 0;
        }
        else if (s_texFormat == TF_8_BIT)
        {
            // paleta de 256 cores: reenvia a cada bloco (algo sobrescreve a metade alta da TMEM entre triangulos; corrigia o loading)
            rdpq_tex_upload_tlut(s_palRing[s_palIdx], 0, 256); s_cTlut++; banks_invalidate();
            s_palUse[s_palIdx] = ++s_useCounter;
        }
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
#ifdef D2_PALBANK
            s_curBank = -1; s_pbClut = -1;
#endif
#else
            surface_t surf = surface_make(s_shadow + (py + v0) * SH_W + ((px - 512) + (ua >> 2)), FMT_CI4, 256 - ua, 256 - v0, SH_W * 2);
            rdpq_texparms_t parms = {};
            rdpq_tex_upload_sub(TILE0, &surf, &parms, u0 - ua, 0, (u0 - ua) + w, h); s_cLdSh++; s_leanTiles = false; s_tmemWin = false;
#ifdef D2_PALBANK
            s_curBank = -1; s_pbClut = -1;
#endif
#endif
            s_lastUp.slot = slot; s_shLast.k0 = shK0; s_shLast.k1 = shK1; s_shLast.sig = shSig;
        }
    }
    else if (s_lastUp.slot != slot || s_lastUp.gen != slot->gen)
    {
        if (s_curRgbaMode)
        {
            surface_t surf = surface_make(slot->data, FMT_RGBA32, wpad, sh, wpad * 4);
            rdpq_texparms_t parms = {};
            rdpq_tex_upload_sub(TILE0, &surf, &parms, 0, 0, sw, sh);
            s_cLdReg++; s_leanTiles = false; s_tmemWin = false;
#ifdef D2_PALBANK
            s_curBank = -1; s_pbClut = -1;
#endif
        }
#ifdef D2_LEANLOAD
        else
        {
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
#ifdef D2_PALBANK
            s_curBank = -1; s_pbClut = -1;
#endif
        }
#else
        else
        {
            surface_t surf = surface_make(slot->data, s_texFormat == TF_4_BIT ? FMT_CI4 : s_texFormat == TF_8_BIT ? FMT_CI8 : FMT_RGBA16, wpad, sh,
                                          s_texFormat == TF_4_BIT ? wpad / 2 : s_texFormat == TF_8_BIT ? wpad : wpad * 2);
            rdpq_texparms_t parms = {};
            rdpq_tex_upload_sub(TILE0, &surf, &parms, 0, 0, sw, sh); s_cLdReg++; s_leanTiles = false; s_tmemWin = false;
#ifdef D2_PALBANK
            s_curBank = -1; s_pbClut = -1;
#endif
        }
#endif
        s_lastUp.slot = slot; s_lastUp.gen = slot->gen;
    }
    return true;
}

#ifdef D2_TEXWIN
// paleta de 16 ou 256 cores (recarrega se mudou CLUT, formato, VRAM ou modo)
static bool upload_pal(int clut)
{
    const int pkey = clut;
    if (s_lastUp.clut == pkey && s_lastUp.fmt == s_texFormat && s_lastUp.palVer == s_vramWrites && s_lastUp.slot != 0) return false;
    const int cx = (clut & 63) * 16, cy = (clut & 0xFFFF) >> 6;
    const unsigned short *src = vram + ((cy & 511) << 10);
    const int n = s_texFormat == TF_4_BIT ? 16 : 256;
    s_palIdx = (s_palIdx + 1) % PAL_RING;
    if (s_palUse[s_palIdx] > s_doneCounter) rdp_wait_idle();
    unsigned short *pal = s_palRing[s_palIdx];
    for (int i = 0; i < n; i++) pal[i] = conv_color(src[(cx + i) & 1023]);
    data_cache_hit_writeback(pal, n * 2);
    rdpq_tex_upload_tlut(pal, 0, n); s_cTlut++; s_winBase = -1; if (n > 16) banks_invalidate(); else s_bank[0].clut = -1;
#ifdef D2_PALBANK
    if (n > 16) banks_invalidate();
#endif
    s_palUse[s_palIdx] = ++s_useCounter;
    s_lastUp.clut = pkey; s_lastUp.fmt = s_texFormat; s_lastUp.palVer = s_vramWrites; s_lastUp.palEnt = 0;
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

// recorta o poligono contra um semi-plano no espaco de tela (X ou Y): eixo 0 = X, 1 = Y; keepGE: mantem >= bound
static int clip_poly_screen(const DV *in, int n, DV *out, int axis, float bound, bool keepGE)
{
    int m = 0;
    for (int i = 0; i < n; i++)
    {
        const DV &a = in[i], &b = in[(i + 1) % n];
        float va = (axis ? a.y : a.x) - bound, vb = (axis ? b.y : b.x) - bound;
        if (!keepGE) { va = -va; vb = -vb; }
        const bool ina = va >= 0, inb = vb >= 0;
        if (ina) out[m++] = a;
        if (ina != inb)
        {
            float t = va / (va - vb);
            DV c;
            c.x = a.x + (b.x - a.x) * t; c.y = a.y + (b.y - a.y) * t;
            c.u = a.u + (b.u - a.u) * t; c.v = a.v + (b.v - a.v) * t;
            c.r = a.r + (b.r - a.r) * t; c.g = a.g + (b.g - a.g) * t;
            c.b = a.b + (b.b - a.b) * t; c.a = a.a + (b.a - a.a) * t;
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
        uint32_t r = (uint32_t)(v->r * 255.0f + 0.5f);
        uint32_t g = (uint32_t)(v->g * 255.0f + 0.5f);
        uint32_t bl = (uint32_t)(v->b * 255.0f + 0.5f);
        uint32_t al = (uint32_t)(v->a * 255.0f + 0.5f);
        if (r > 255) r = 255; if (g > 255) g = 255; if (bl > 255) bl = 255; if (al > 255) al = 255;
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
    s_lastUp.slot = 0;
#ifdef D2_TEXWIN
    s_leanTiles = false;
    s_tmemWin = false;
    s_winBase = -1;
#ifdef D2_PALBANK
    banks_invalidate();
#endif
#endif
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
#ifdef D2_DPROF
    if ((d2_rdp_frames % 30) == 0) debugf("[discard] scissor=%u screen_clip=%u uv_clip=%u up_fail=%u degen=%u\n", s_rejScissor, s_rejScreenClip, s_rejUvClip, s_rejUpload, s_rejDegen);
    s_rejScissor = s_rejScreenClip = s_rejUvClip = s_rejUpload = s_rejDegen = 0;
#endif
    s_cLdSh = s_cLdReg = s_cLdWin = s_cTlut = s_cWinHit = s_cCalls = 0; s_cBankHitX = 0; s_dCntP = s_dTicks = s_tTicks = s_nTri = s_nClip = s_clipTicks = s_upTicks = s_nSub = 0; s_palT = s_winHitT = s_winMissT = s_regT = s_nHitVw = 0; s_vwFrame = s_vramWrites; s_palT = s_winHitT = s_winMissT = s_regT = 0; s_vwFrame = s_vramWrites;
    s_benchTris = s_triCount; s_benchTex = s_texTriCount;
    s_triCount = s_texTriCount = s_subTri = s_calls = 0;
#if D2_RSP_GPU
    if ((d2_rdp_frames % 30) == 0) {
        debugf("[rsp_ovl] ovl_id=0x%lx f3=%u f4=%u ft4_rsp=%u ft4_cpu=%u ft4_tot=%u ft3=%u dir_ft4=%u ovl_sw=%u\n",
            (unsigned long)s_d2gpuOvl, s_rspF3Count, s_rspF4Count, s_ft4Rsp, s_ft4Cpu, s_ft4Total, s_ft3Total, s_directFt4Total, s_ovlSwitches);
        debugf("[ft4_stat] rsp=%u cpu=%u ci4_elig=%u rej_dim=%u rej_fmt=%u rej_clip=%u rej_scale=%u rej_up=%u win_hit=%u win_miss=%u tlut_up=%u\n",
            s_ft4Rsp, s_ft4Cpu, s_ft4Ci4Eligible, s_ft4RejDim, s_ft4RejFmt, s_ft4RejClip, s_ft4RejScale, s_ft4RejUp, s_cWinHit, s_cLdWin, s_cTlut);
        s_ft4Rsp = s_ft4Cpu = s_ft4Total = s_ft3Total = s_directFt4Total = s_ft4Ci4Eligible = 0;
        s_ft4RejDim = s_ft4RejFmt = s_ft4RejClip = s_ft4RejScale = s_ft4RejUp = 0;
        s_ovlSwitches = 0;
    }
    if (s_d2gpuOvl) {
        track_ovl_switch(s_d2gpuOvl);
        rspq_write(s_d2gpuOvl, D2G_CMD_DRAW_END);
    }
#endif
    track_ovl_switch(0);
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
#ifdef D2_ORIGIN
static const void *s_curPrim = NULL;
#endif

static bool ensure_slots(void)
{
    if (s_slots) return true;
    s_slots = (TexSlot *)malloc(sizeof(TexSlot) * TEX_SLOTS);
    assertf(s_slots != NULL, "FALHA CRITICA: Memoria insuficiente no Heap para o cache de texturas (%d bytes)!\n", (int)(sizeof(TexSlot) * TEX_SLOTS));
    memset(s_slots, 0, sizeof(TexSlot) * TEX_SLOTS);
    s_lastUp.clut = -1;
    return true;
}

static inline void begin_batch(bool tex, BlendMode bm)
{
    set_mode(tex, (int)s_texFormat, bm);
    s_ctxTex = tex;
    if (tex && (bm == BM_ADD || bm == BM_ADD_QUATER_SOURCE || bm == BM_SUBTRACT))
    {
        s_ctxTileW = 32; s_ctxTileH = 32;
    }
    else
    {
        s_ctxTileW = 64; s_ctxTileH = s_texFormat == TF_4_BIT ? 64 : s_texFormat == TF_8_BIT ? 32 : 16;
    }
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

static bool s_noSizeLimit = false;

static void tri_core(const PV *A, const PV *B, const PV *C, int page, int clut)
{
    track_ovl_switch(0);
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
    if (mxx < s_ctxCx0 || mnx >= s_ctxCx1 || mxy < s_ctxCy0 || mny >= s_ctxCy1)
    {
#ifdef D2_DPROF
        s_rejScissor++;
#endif
        return;
    }

    // Descarte de triangulos de area zero (degenerados), como a GPU do PS1
    long long dx01 = (long long)X4[1] - X4[0], dy01 = (long long)Y4[1] - Y4[0];
    long long dx02 = (long long)X4[2] - X4[0], dy02 = (long long)Y4[2] - Y4[0];
    if (dx01 * dy02 - dx02 * dy01 == 0LL)
    {
#ifdef D2_DPROF
        s_rejDegen++;
#endif
        return;
    }

#ifdef D2_ORIGIN
    {
        int tw = (mxx - mnx) >> 2, th = (mxy - mny) >> 2;
        if (tw > 150 || th > 150) {
            short xs[4] = { (short)(X4[0] >> 2), (short)(X4[1] >> 2), (short)(X4[2] >> 2), 0 };
            short ys[4] = { (short)(Y4[0] >> 2), (short)(Y4[1] >> 2), (short)(Y4[2] >> 2), 0 };
            unsigned char us[4] = { (unsigned char)(v[0]->s32 >> 5), (unsigned char)(v[1]->s32 >> 5), (unsigned char)(v[2]->s32 >> 5), 0 };
            unsigned char vs[4] = { (unsigned char)(v[0]->t32 >> 5), (unsigned char)(v[1]->t32 >> 5), (unsigned char)(v[2]->t32 >> 5), 0 };
            d2_origin_check_large(s_curPrim, 0x24, page, clut, xs, ys, us, vs, 3);
        }
    }
#endif

    // Regra da GPU do PS1: descarta o triangulo quando a largura for > 1023 ou a altura > 511 em coordenadas PSX
    if (!s_noSizeLimit)
    {
        const int dX4 = mxx - mnx;
        const int dY4 = mxy - mny;
        if ((long long)dX4 * 16384LL > 1023LL * (long long)s_ctxSx16 ||
            (long long)dY4 * 16384LL > 511LL * (long long)s_ctxSy16)
        {
#ifdef D2_ORIGIN
            const D2OriginRecord *rec = d2_origin_lookup(s_curPrim);
            const char *func = rec ? rec->func_name : "unknown";
            int model = rec ? rec->model_type : -1;
            debugf("[SIZE-LIMIT-DISCARD] func=%s model=%d prim=%p dX4=%d dY4=%d (w=%d h=%d)\n",
                   func, model, s_curPrim, dX4, dY4, dX4 >> 2, dY4 >> 2);
#endif
            return;
        }
    }

    // Se o triangulo ultrapassa o range do RDP 12-bit ou tem dimensao extrema (>4000px),
    // recorta contra o viewport estendido para evitar que predios desaparecam ou gerem artefatos gigantes
    if (mnx < -256 || mxx > 1536 || mny < -256 || mxy > 1216 || mxx - mnx > 16000 || mxy - mny > 16000)
    {
        DV pin[3], ptmp1[8], ptmp2[8];
        for (int i = 0; i < 3; i++) {
            pin[i].x = X4[i] * 0.25f; pin[i].y = Y4[i] * 0.25f;
            pin[i].u = v[i]->s32 * (1.0f / 32.0f); pin[i].v = v[i]->t32 * (1.0f / 32.0f);
            pin[i].r = ((rgba[i] >> 24) & 255) / 255.0f;
            pin[i].g = ((rgba[i] >> 16) & 255) / 255.0f;
            pin[i].b = ((rgba[i] >> 8) & 255) / 255.0f;
            pin[i].a = (rgba[i] & 255) / 255.0f;
        }
        int np = 3;
        np = clip_poly_screen(pin, np, ptmp1, 0, -64.0f, true);
        if (np < 3) {
#ifdef D2_DPROF
            s_rejScreenClip++;
#endif
            return;
        }
        np = clip_poly_screen(ptmp1, np, ptmp2, 0, (float)(SCREEN_W + 64), false);
        if (np < 3) {
#ifdef D2_DPROF
            s_rejScreenClip++;
#endif
            return;
        }
        np = clip_poly_screen(ptmp2, np, ptmp1, 1, -64.0f, true);
        if (np < 3) {
#ifdef D2_DPROF
            s_rejScreenClip++;
#endif
            return;
        }
        np = clip_poly_screen(ptmp1, np, ptmp2, 1, (float)(SCREEN_H + 64), false);
        if (np < 3) {
#ifdef D2_DPROF
            s_rejScreenClip++;
#endif
            return;
        }

        for (int i = 1; i + 1 < np; i++) {
            PV sub[3];
            const DV *d[3] = { &ptmp2[0], &ptmp2[i], &ptmp2[i + 1] };
            for (int k = 0; k < 3; k++) {
                float kx = d[k]->x, ky = d[k]->y;
                if (kx < -64.0f) kx = -64.0f; else if (kx > (float)(SCREEN_W + 64)) kx = (float)(SCREEN_W + 64);
                if (ky < -64.0f) ky = -64.0f; else if (ky > (float)(SCREEN_H + 64)) ky = (float)(SCREEN_H + 64);
                sub[k].X4 = fl4(kx);
                sub[k].Y4 = fl4(ky);
                if (sub[k].X4 < -256) sub[k].X4 = -256; else if (sub[k].X4 > 1536) sub[k].X4 = 1536;
                if (sub[k].Y4 < -256) sub[k].Y4 = -256; else if (sub[k].Y4 > 1216) sub[k].Y4 = 1216;
                sub[k].rgba = ((uint32_t)(d[k]->r * 255.0f) << 24) |
                              ((uint32_t)(d[k]->g * 255.0f) << 16) |
                              ((uint32_t)(d[k]->b * 255.0f) << 8)  |
                              ((uint32_t)(d[k]->a * 255.0f) & 255);
                sub[k].s32 = (int)(d[k]->u * 32.0f + 0.5f);
                sub[k].t32 = (int)(d[k]->v * 32.0f + 0.5f);
            }
            int dx01 = sub[1].X4 - sub[0].X4, dy01 = sub[1].Y4 - sub[0].Y4;
            int dx02 = sub[2].X4 - sub[0].X4, dy02 = sub[2].Y4 - sub[0].Y4;
            if (dx01 * dy02 - dx02 * dy01 == 0) {
#ifdef D2_DPROF
                s_rejDegen++;
#endif
                continue;
            }
            tri_core(&sub[0], &sub[1], &sub[2], page, clut);
        }
        return;
    }

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
    if (umax > 255) umax = 255;
    if (vmax > 255) vmax = 255;
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
        if (!upload_region(page, clut, s_subTu, s_subTv, w, h, 1))
        {
#ifdef D2_DPROF
            s_rejUpload++;
#endif
            return;
        }
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
            if (!upload_region(page, clut, umin, vmin, w, h, step))
            {
#ifdef D2_DPROF
                s_rejUpload++;
#endif
                return;
            }
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
        p[i].r = ((rgba[i] >> 24) + 0.5f) / 255.0f; p[i].g = (((rgba[i] >> 16) & 255) + 0.5f) / 255.0f; p[i].b = (((rgba[i] >> 8) & 255) + 0.5f) / 255.0f; p[i].a = ((rgba[i] & 255) + 0.5f) / 255.0f;
    }
    const int bw = tileW * step, bh = tileH * step;
    const int tu0 = (umin / bw) * bw, tv0 = (vmin / bh) * bh;
    for (int tv = tv0; tv <= vmax && tv < 256; tv += bh)
        for (int tu = tu0; tu <= umax && tu < 256; tu += bw)
        {
            DV a[10], b[10];
            int n = 3;
            memcpy(a, p, sizeof(p));
            n = clip_poly(a, n, b, 0, (float)tu, true);          if (n < 3) {
#ifdef D2_DPROF
                s_rejUvClip++;
#endif
                continue;
            }
            n = clip_poly(b, n, a, 0, (float)(tu + bw), false);  if (n < 3) {
#ifdef D2_DPROF
                s_rejUvClip++;
#endif
                continue;
            }
            n = clip_poly(a, n, b, 1, (float)tv, true);          if (n < 3) {
#ifdef D2_DPROF
                s_rejUvClip++;
#endif
                continue;
            }
            n = clip_poly(b, n, a, 1, (float)(tv + bh), false);  if (n < 3) {
#ifdef D2_DPROF
                s_rejUvClip++;
#endif
                continue;
            }
            int w = bw, h = bh;
            if (tu + w > 256) w = 256 - tu;
            if (tv + h > 256) h = 256 - tv;
            if (w <= 0 || h <= 0) continue;
            if (!upload_region(page, clut, tu, tv, w, h, step)) {
#ifdef D2_DPROF
                s_rejUpload++;
#endif
                continue;
            }
            for (int i = 1; i + 1 < n; i++) { emit_tri(true, a[0], a[i], a[i + 1], (float)(step == 1 ? s_upOu : tu), (float)(step == 1 ? s_upOv : tv), 1.0f / step, 1.0f, 1.0f); s_nSub++; }
        }
}

#ifdef D2_XP_NOEMIT
#undef rspq_write
#undef rdpq_write
#endif
static const uint8_t s_shadeLut[256] = {
      0,   1,   3,   5,   7,   9,  11,  13,  15,  17,  19,  21,  23,  25,  27,  29,
     31,  33,  35,  37,  39,  41,  43,  45,  47,  49,  51,  53,  55,  57,  59,  61,
     63,  65,  67,  69,  71,  73,  75,  77,  79,  81,  83,  85,  87,  89,  91,  93,
     95,  97,  99, 101, 103, 105, 107, 109, 111, 113, 115, 117, 119, 121, 123, 125,
    127, 129, 131, 133, 135, 137, 139, 141, 143, 145, 147, 149, 151, 153, 155, 157,
    159, 161, 163, 165, 167, 169, 171, 173, 175, 177, 179, 181, 183, 185, 187, 189,
    191, 193, 195, 197, 199, 201, 203, 205, 207, 209, 211, 213, 215, 217, 219, 221,
    223, 225, 227, 229, 231, 233, 235, 237, 239, 241, 243, 245, 247, 249, 251, 253,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255
};
static inline uint32_t shade_rgba(unsigned r, unsigned g, unsigned b)
{
    return ((uint32_t)s_shadeLut[r & 255] << 24) |
           ((uint32_t)s_shadeLut[g & 255] << 16) |
           ((uint32_t)s_shadeLut[b & 255] << 8)  | 255;
}

static inline uint32_t calc_vertex_color(unsigned r, unsigned g, unsigned b, BlendMode blend, bool textured)
{
    const uint32_t sr = textured ? s_shadeLut[r & 255] : (r & 255);
    const uint32_t sg = textured ? s_shadeLut[g & 255] : (g & 255);
    const uint32_t sb = textured ? s_shadeLut[b & 255] : (b & 255);

    if (blend == BM_ADD || blend == BM_ADD_QUATER_SOURCE || blend == BM_SUBTRACT)
    {
        uint32_t a_s = sr > sg ? (sr > sb ? sr : sb) : (sg > sb ? sg : sb);
        if (a_s == 0) return 0;
        uint32_t r_norm = (sr * 255) / a_s;
        uint32_t g_norm = (sg * 255) / a_s;
        uint32_t b_norm = (sb * 255) / a_s;
        if (r_norm > 255) r_norm = 255;
        if (g_norm > 255) g_norm = 255;
        if (b_norm > 255) b_norm = 255;
        uint32_t alpha = (blend == BM_ADD_QUATER_SOURCE) ? (a_s >> 2) : a_s;
        return (r_norm << 24) | (g_norm << 16) | (b_norm << 8) | alpha;
    }
    return (sr << 24) | (sg << 16) | (sb << 8) | 255;
}

static inline void calc_primitive_colors(uint32_t *outRgba, int count, const uint8_t *cr, const uint8_t *cg, const uint8_t *cb, BlendMode bm, bool textured)
{
    if (bm == BM_ADD || bm == BM_ADD_QUATER_SOURCE)
    {
        uint32_t sr[4], sg[4], sb[4], as[4];
        uint32_t max_as = 0;
        int max_i = 0;
        for (int i = 0; i < count; i++)
        {
            const uint32_t r = cr ? cr[i] : 128;
            const uint32_t g = cg ? cg[i] : 128;
            const uint32_t b = cb ? cb[i] : 128;
            sr[i] = textured ? s_shadeLut[r & 255] : (r & 255);
            sg[i] = textured ? s_shadeLut[g & 255] : (g & 255);
            sb[i] = textured ? s_shadeLut[b & 255] : (b & 255);
            uint32_t a_s = sr[i] > sg[i] ? (sr[i] > sb[i] ? sr[i] : sb[i]) : (sg[i] > sb[i] ? sg[i] : sb[i]);
            as[i] = a_s;
            if (a_s > max_as)
            {
                max_as = a_s;
                max_i = i;
            }
        }
        uint32_t unif_rgb = 0;
        if (max_as > 0)
        {
            uint32_t r_norm = (sr[max_i] * 255) / max_as;
            uint32_t g_norm = (sg[max_i] * 255) / max_as;
            uint32_t b_norm = (sb[max_i] * 255) / max_as;
            if (r_norm > 255) r_norm = 255;
            if (g_norm > 255) g_norm = 255;
            if (b_norm > 255) b_norm = 255;
            unif_rgb = (r_norm << 24) | (g_norm << 16) | (b_norm << 8);
        }
        for (int i = 0; i < count; i++)
        {
            uint32_t alpha = (bm == BM_ADD_QUATER_SOURCE) ? (as[i] >> 2) : as[i];
            outRgba[i] = unif_rgb | alpha;
        }
    }
    else
    {
        for (int i = 0; i < count; i++)
        {
            const uint32_t r = cr ? cr[i] : 128;
            const uint32_t g = cg ? cg[i] : 128;
            const uint32_t b = cb ? cb[i] : 128;
            outRgba[i] = calc_vertex_color(r, g, b, bm, textured);
        }
    }
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
        uint32_t rgba[3];
        uint8_t cr[3] = { g[0].r, g[1].r, g[2].r };
        uint8_t cg[3] = { g[0].g, g[1].g, g[2].g };
        uint8_t cb[3] = { g[0].b, g[1].b, g[2].b };
        calc_primitive_colors(rgba, 3, cr, cg, cb, s_blend, s_textured);
        for (int i = 0; i < 3; i++)
        {
            pv[i].X4 = ((int)g[i].x * sx16) >> 14;
            pv[i].Y4 = ((int)g[i].y * sy16) >> 14;
            pv[i].rgba = rgba[i];
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
    uint32_t r = 0;
    for (int sh = 0; sh < 32; sh += 8) r |= (uint32_t)lerp16((a >> sh) & 255, (b >> sh) & 255, f) << sh;
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
            tri_core(&p[1], &p[3], &p[2], page, clut);
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
int d2_direct_poly(P_TAG *tag);
int d2_native_ft4(POLY_FT4 *p)
{
    if (!activeDrawEnv.dfe || !s_scene) return 9;
#ifdef D2_ORIGIN
    {
        short xs[4] = { p->x0, p->x1, p->x2, p->x3 };
        short ys[4] = { p->y0, p->y1, p->y2, p->y3 };
        unsigned char us[4] = { p->u0, p->u1, p->u2, p->u3 };
        unsigned char vs[4] = { p->v0, p->v1, p->v2, p->v3 };
        d2_origin_check_large(p, 0xB3, p->tpage, p->clut, xs, ys, us, vs, 4);
    }
#endif
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

    s_ft4Total++;
#if D2_RSP_TEX
    if (s_texFormat == TF_4_BIT) {
        s_ft4Ci4Eligible++;
    } else {
        s_ft4RejFmt++;
    }

    if (s_d2gpuOvl && s_texFormat == TF_4_BIT)
    {
        if (s_ctxSx16 != 65536 || s_ctxSy16 != 65536)
        {
            s_ft4RejScale++;
        }
        else
        {
            short minx = p->x0, maxx = p->x0, miny = p->y0, maxy = p->y0;
            if (p->x1 < minx) minx = p->x1; if (p->x1 > maxx) maxx = p->x1;
            if (p->x2 < minx) minx = p->x2; if (p->x2 > maxx) maxx = p->x2;
            if (p->x3 < minx) minx = p->x3; if (p->x3 > maxx) maxx = p->x3;
            if (p->y1 < miny) miny = p->y1; if (p->y1 > maxy) maxy = p->y1;
            if (p->y2 < miny) miny = p->y2; if (p->y2 > maxy) maxy = p->y2;
            if (p->y3 < miny) miny = p->y3; if (p->y3 > maxy) maxy = p->y3;
            int sx0 = (minx + ofx) << 2, sx1 = (maxx + ofx) << 2;
            int sy0 = (miny + ofy) << 2, sy1 = (maxy + ofy) << 2;
            if (sx1 < s_ctxCx0 || sx0 >= s_ctxCx1 || sy1 < s_ctxCy0 || sy0 >= s_ctxCy1) return 9;
            if (sx0 < -4096 || sx1 > 4095 || sy0 < -4096 || sy1 > 4095 || sx1 - sx0 > 16000 || sy1 - sy0 > 16000)
            {
                s_ft4RejClip++;
            }
            else
            {
                int umin = p->u0, umax = p->u0;
                if (p->u1 < umin) umin = p->u1; if (p->u1 > umax) umax = p->u1;
                if (p->u2 < umin) umin = p->u2; if (p->u2 > umax) umax = p->u2;
                if (p->u3 < umin) umin = p->u3; if (p->u3 > umax) umax = p->u3;

                int vmin = p->v0, vmax = p->v0;
                if (p->v1 < vmin) vmin = p->v1; if (p->v1 > vmax) vmax = p->v1;
                if (p->v2 < vmin) vmin = p->v2; if (p->v2 > vmax) vmax = p->v2;
                if (p->v3 < vmin) vmin = p->v3; if (p->v3 > vmax) vmax = p->v3;

                int uw = (umax > umin) ? (umax - umin) : 1;
                int vh = (vmax > vmin) ? (vmax - vmin) : 1;
                if (uw > 64 || vh > 64)
                {
                    s_ft4RejDim++;
                }
                else
                {
                    const int px = (p->tpage & 15) * 64, py = ((p->tpage >> 4) & 1) * 256;
                    if (px < 512)
                    {
                        s_ft4RejDim++;
                    }
                    else
                    {
                        if (!upload_win(px, py, p->clut, umin, vmin, uw, vh))
                        {
                            s_ft4RejUp++;
                        }
                        else
                        {
                            if (s_rspOfx != ofx || s_rspOfy != ofy)
                            {
                                s_rspOfx = ofx; s_rspOfy = ofy;
                                track_ovl_switch(s_d2gpuOvl);
                                rspq_write(s_d2gpuOvl, D2G_CMD_SET_OFFSET, 0, ((uint32_t)(int16_t)ofx << 16) | ((uint32_t)(int16_t)ofy & 0xFFFF));
                            }
                            if (s_rspTexOu != s_upOu || s_rspTexOv != s_upOv)
                            {
                                s_rspTexOu = s_upOu; s_rspTexOv = s_upOv;
                                track_ovl_switch(s_d2gpuOvl);
                                rspq_write(s_d2gpuOvl, D2G_CMD_SET_TEX_ORIGIN, 0, ((uint32_t)(uint16_t)s_upOu << 16) | ((uint32_t)(uint16_t)s_upOv & 0xFFFF));
                            }
                            if (s_tmemDirty)
                            {
                                __rdpq_autosync_use(AUTOSYNC_PIPE | AUTOSYNC_TILE(0) | AUTOSYNC_TMEM(0));
                                s_tmemDirty = false;
                            }

                            static uint32_t s_lastRgb = 0xFFFFFFFF, s_lastCol = 0;
                            uint32_t curRgb = ((uint32_t)p->r0 << 16) | ((uint32_t)p->g0 << 8) | p->b0;
                            uint32_t col;
                            if (__builtin_expect(curRgb == s_lastRgb, 1)) {
                                col = s_lastCol;
                            } else {
                                s_lastRgb = curRgb;
                                s_lastCol = col = ((uint32_t)s_shadeLut[p->r0] << 16) | ((uint32_t)s_shadeLut[p->g0] << 8) | s_shadeLut[p->b0];
                            }

                            uint32_t xy0 = *(const uint32_t*)&p->x0;
                            uint32_t xy1 = *(const uint32_t*)&p->x1;
                            uint32_t xy2 = *(const uint32_t*)&p->x2;
                            uint32_t xy3 = *(const uint32_t*)&p->x3;
                            uint32_t uv01 = (*(const uint16_t*)&p->u0 << 16) | *(const uint16_t*)&p->u1;
                            uint32_t uv23 = (*(const uint16_t*)&p->u2 << 16) | *(const uint16_t*)&p->u3;

                            track_ovl_switch(s_d2gpuOvl);
                            rspq_write(s_d2gpuOvl, D2G_CMD_POLY_FT4, col, xy0, xy1, xy2, xy3, uv01, uv23);
                            s_rspFT4Count++;
                            s_ft4Rsp++;
                            s_triCount += 2;
                            s_texTriCount += 2;
                            return 9;
                        }
                    }
                }
            }
        }
        s_ft4Fallback++;
        s_ft4Cpu++;
    }
#endif
    const short *xy[4] = { &p->x0, &p->x1, &p->x2, &p->x3 };
    const unsigned char *uv[4] = { &p->u0, &p->u1, &p->u2, &p->u3 };
    const uint32_t col = calc_vertex_color(p->r0, p->g0, p->b0, BM_NONE, true);
    PV q[4];
    if (__builtin_expect(s_ctxSx16 == 65536 && s_ctxSy16 == 65536, 1))
    {
        for (int i = 0; i < 4; i++)
        {
            q[i].X4 = (xy[i][0] + ofx) << 2;
            q[i].Y4 = (xy[i][1] + ofy) << 2;
            q[i].rgba = col; q[i].s32 = uv[i][0] << 5; q[i].t32 = uv[i][1] << 5;
        }
    }
    else
    {
        for (int i = 0; i < 4; i++)
        {
            q[i].X4 = ((xy[i][0] + ofx) * s_ctxSx16) >> 14;
            q[i].Y4 = ((xy[i][1] + ofy) * s_ctxSy16) >> 14;
            q[i].rgba = col; q[i].s32 = uv[i][0] << 5; q[i].t32 = uv[i][1] << 5;
        }
    }
    tri_core(&q[0], &q[1], &q[2], p->tpage, p->clut);
    tri_core(&q[1], &q[3], &q[2], p->tpage, p->clut);
    return 9;
}
int d2_native_rect(P_TAG *tag)
{
    const int code = tag->code, kind = code & 0xFD;
    const bool tex = kind == 0x64 || kind == 0x74 || kind == 0x7C;
    if (tex && overrideTexture != 0) return 0;
    int w, h, len;
    if (kind == 0x60) { TILE *p = (TILE *)tag; w = p->w; h = p->h; len = 3; }
    else if (kind == 0x64) { SPRT *p = (SPRT *)tag; w = p->w; h = p->h; len = 4; }
    else if (kind == 0x68) { w = h = 1; len = 2; }
    else if (kind == 0x70 || kind == 0x74) { w = h = 8; len = kind == 0x74 ? 3 : 2; }
    else if (kind == 0x78 || kind == 0x7C) { w = h = 16; len = kind == 0x7C ? 3 : 2; }
    else return 0;
#ifdef D2_ORIGIN
    {
        TILE *base = (TILE *)tag;
        short xs[4] = { base->x0, (short)(base->x0 + w), base->x0, (short)(base->x0 + w) };
        short ys[4] = { base->y0, base->y0, (short)(base->y0 + h), (short)(base->y0 + h) };
        d2_origin_check_large(tag, code, activeDrawEnv.tpage, 0, xs, ys, NULL, NULL, 4);
    }
#endif
    if (!activeDrawEnv.dfe || !s_scene || w <= 0 || h <= 0) return len;
    if (!ensure_slots()) return len;
    d2_flush_splits();
    const RECT16 &clip = activeDrawEnv.clip;
    if (clip.x != s_clipKey[0] || clip.y != s_clipKey[1] || clip.w != s_clipKey[2] || clip.h != s_clipKey[3] ||
        (activeDispEnv.disp.x | (activeDispEnv.disp.w << 16)) != s_clipKey[4] ||
        (activeDispEnv.disp.y | (activeDispEnv.disp.h << 16)) != s_clipKey[5]) GR_SetupClipMode(&clip, 1);
    const BlendMode bm = (code & 2) ? (BlendMode)(((activeDrawEnv.tpage >> 5) & 3) + 1) : BM_NONE;
    if (tex) s_texFormat = (TexFormat)((activeDrawEnv.tpage >> 7) & 3);
    begin_batch(tex && s_texFormat != TF_32_BIT_RGBA, bm);
    TILE *base = (TILE *)tag;
    const int ofx = activeDrawEnv.ofs[0] - activeDispEnv.disp.x, ofy = activeDrawEnv.ofs[1] - activeDispEnv.disp.y;
    int x[4] = { base->x0, base->x0 + w, base->x0, base->x0 + w };
    int y[4] = { base->y0, base->y0, base->y0 + h, base->y0 + h };
    int u0 = 0, v0 = 0, clut = 0;
    if (tex) { SPRT *s = (SPRT *)tag; u0 = s->u0; v0 = s->v0; clut = s->clut; }
    const uint32_t col = (code & 1) ? calc_vertex_color(128, 128, 128, bm, tex) : calc_vertex_color(base->r0, base->g0, base->b0, bm, tex);
    PV q[4];
    for (int i = 0; i < 4; i++)
    {
        q[i].X4 = ((x[i] + ofx) * s_ctxSx16) >> 14; q[i].Y4 = ((y[i] + ofy) * s_ctxSy16) >> 14;
        q[i].rgba = col; q[i].s32 = (u0 + ((i & 1) ? w : 0)) << 5; q[i].t32 = (v0 + ((i & 2) ? h : 0)) << 5;
    }
    s_noSizeLimit = true;
    tri_core(&q[0], &q[1], &q[2], activeDrawEnv.tpage, clut);
    tri_core(&q[1], &q[3], &q[2], activeDrawEnv.tpage, clut);
    s_noSizeLimit = false;
    return len;
}
static void d2_line_segment(short ax, short ay, short bx, short by, uint32_t ca, uint32_t cb, BlendMode bm)
{
    if (ax > bx || (ax == bx && ay > by)) { short t=ax; ax=bx; bx=t; t=ay; ay=by; by=t; uint32_t c=ca; ca=cb; cb=c; }
    const int ox=activeDrawEnv.ofs[0]-activeDispEnv.disp.x, oy=activeDrawEnv.ofs[1]-activeDispEnv.disp.y;
    int x[4]={ax,bx,bx,ax}, y[4]={ay,by,by,ay};
    if (bx-ax > (by>ay ? by-ay : ay-by)) { x[1]++; x[2]++; y[2]++; y[3]++; }
    else { y[1]++; y[2]++; x[2]++; x[3]++; }
    PV q[4];
    for(int i=0;i<4;i++){ q[i].X4=((x[i]+ox)*s_ctxSx16)>>14; q[i].Y4=((y[i]+oy)*s_ctxSy16)>>14; q[i].rgba=(i==0||i==3)?ca:cb; q[i].s32=q[i].t32=0; }
    begin_batch(false,bm);
    s_noSizeLimit = true;
    tri_core(&q[0],&q[1],&q[3],0,0);
    tri_core(&q[1],&q[2],&q[3],0,0);
    s_noSizeLimit = false;
}
int d2_native_line(P_TAG *tag)
{
    const int type=tag->code&0xF0, sub=tag->code&0x0C;
    if (type!=0x40 && type!=0x50) return 0;
    /* G3/G4 nao eram desenhadas pelo PsyCross; preserve o consumo do pacote sem voltar ao fallback. */
    if(type==0x50 && sub!=0) return sub==8 ? 7 : sub==12 ? 9 : 0;
    const int len=type==0x50?4:(sub==0?3:sub==8?5:6);
    if (!activeDrawEnv.dfe || !s_scene || !ensure_slots()) return len;
    d2_flush_splits();
    const RECT16 &c=activeDrawEnv.clip;
    if(c.x!=s_clipKey[0]||c.y!=s_clipKey[1]||c.w!=s_clipKey[2]||c.h!=s_clipKey[3]||(activeDispEnv.disp.x|(activeDispEnv.disp.w<<16))!=s_clipKey[4]||(activeDispEnv.disp.y|(activeDispEnv.disp.h<<16))!=s_clipKey[5]) GR_SetupClipMode(&c,1);
    const BlendMode bm=(tag->code&2)?(BlendMode)(((activeDrawEnv.tpage>>5)&3)+1):BM_NONE;
    if(type==0x50){
        LINE_G2 *p=(LINE_G2*)tag;
        uint8_t cr[2] = { p->r0, p->r1 }, cg[2] = { p->g0, p->g1 }, cb[2] = { p->b0, p->b1 };
        uint32_t rgba[2];
        calc_primitive_colors(rgba, 2, cr, cg, cb, bm, false);
        d2_line_segment(p->x0,p->y0,p->x1,p->y1,rgba[0],rgba[1],bm);
    }
    else if(sub==0){ LINE_F2 *p=(LINE_F2*)tag; uint32_t c=calc_vertex_color(p->r0,p->g0,p->b0,bm,false); d2_line_segment(p->x0,p->y0,p->x1,p->y1,c,c,bm); }
    else if(sub==8){ LINE_F3 *p=(LINE_F3*)tag; uint32_t c=calc_vertex_color(p->r0,p->g0,p->b0,bm,false); d2_line_segment(p->x0,p->y0,p->x1,p->y1,c,c,bm); d2_line_segment(p->x1,p->y1,p->x2,p->y2,c,c,bm); }
    else { LINE_F4 *p=(LINE_F4*)tag; uint32_t c=calc_vertex_color(p->r0,p->g0,p->b0,bm,false); d2_line_segment(p->x0,p->y0,p->x1,p->y1,c,c,bm); d2_line_segment(p->x1,p->y1,p->x2,p->y2,c,c,bm); d2_line_segment(p->x2,p->y2,p->x3,p->y3,c,c,bm); }
    return len;
}
int d2_native_drawenv(P_TAG *tag)
{
    const unsigned int *words=(const unsigned int *)&tag->pad0;
    int done=0;
    for(int i=0;i<tag->len;i++) {
        const unsigned int code=PSX_LEW(words[i]);
        switch((code>>24)&15) {
        case 1: activeDrawEnv.tpage=code&0x1ff; activeDrawEnv.dtd=(code>>9)&1; activeDrawEnv.dfe=(code>>10)&1; break;
        case 2: activeDrawEnv.tw.w=code&31; activeDrawEnv.tw.h=(code>>5)&31; activeDrawEnv.tw.x=(code>>10)&31; activeDrawEnv.tw.y=(code>>15)&31; break;
        case 3: activeDrawEnv.clip.x=code&1023; activeDrawEnv.clip.y=(code>>10)&1023; break;
        case 4: activeDrawEnv.clip.w=(code&1023)-activeDrawEnv.clip.x; activeDrawEnv.clip.h=((code>>10)&1023)-activeDrawEnv.clip.y; break;
        case 5: activeDrawEnv.ofs[0]=code&2047; activeDrawEnv.ofs[1]=(code>>11)&2047; break;
        case 6: break; /* mask bit ainda nao e suportado pelo renderer atual */
        case 0: return done;
        default: break;
        }
        done++;
    }
    return done;
}
int d2_native_psyx(P_TAG *tag)
{
    /* Pacotes B1/B2 sao extensoes do port para PC, nao comandos do GPU do PS1. */
    const int sub=tag->code&15;
    if(sub==1) { overrideTexture=0; return 2; }
    if(sub==2) return 2; /* marcador textual de debug */
    return 0;
}
int d2_native_vram_packet(P_TAG *tag)
{
    const int type=tag->code&0xF0;
    if(type==0 && (tag->code&15)==0) return 3;
    if(type==0xA0 || (type==0 && (tag->code&15)==1)) d2_flush_splits();
    if(type==0xA0) {
        DR_LOAD *p=(DR_LOAD*)tag;
        RECT16 r; *(unsigned int*)&r.x=*(unsigned int*)&p->code[1]; *(unsigned int*)&r.w=*(unsigned int*)&p->code[2];
        GR_CopyVRAM((unsigned short*)p->p,0,0,r.w,r.h,r.x,r.y);
        return getlen(tag);
    }
    if(type==0 && (tag->code&15)==1) {
        DR_MOVE *p=(DR_MOVE*)tag;
        RECT16 r; *(unsigned int*)&r.x=*(unsigned int*)&p->code[2]; *(unsigned int*)&r.w=*(unsigned int*)&p->code[4];
        GR_CopyVRAM(NULL,r.x,r.y,r.w,r.h,p->code[3]&0xffff,(p->code[3]>>16)&0xffff);
        return 5;
    }
    return 0;
}
extern "C" int d2_native_dispatch(P_TAG *tag)
{
#ifdef D2_ORIGIN
    s_curPrim = tag;
#endif
    if (__builtin_expect(tag->code == 0xB3, 1)) return d2_native_ft4((POLY_FT4*)tag);
    const int type = tag->code & 0xF0;
    if (__builtin_expect(type == 0x20 || type == 0x30, 1)) return d2_direct_poly(tag);
    if (type == 0xE0) return d2_native_drawenv(tag);
    if (type == 0x40 || type == 0x50) return d2_native_line(tag);
    if (type == 0x60 || type == 0x70) return d2_native_rect(tag);
    if (type == 0x00 || type == 0xA0) return d2_native_vram_packet(tag);
    if (type == 0xB0) return d2_native_psyx(tag);
    return 0;
}
extern "C" int d2_native_draw_prim(P_TAG *tag)
{
    const int n = d2_native_dispatch(tag);
    if (__builtin_expect(n <= 0, 0)) debugf("[native-gpu] primitiva isolada desconhecida code=%02x\n", tag ? tag->code : 0);
    return n;
}
extern "C" void d2_native_draw_ot(u_long *ot)
{
    if (!ot) return;
    P_TAG *base = (P_TAG*)ot;
    unsigned nodes = 0;
    for (;;) {
        if (__builtin_expect(++nodes > 65536, 0)) { debugf("[native-ot] ciclo ou OT invalida\n"); break; }
        const unsigned words = getlen(base);
        if (__builtin_expect(words != 0, 1)) {
            uintptr_t cur = (uintptr_t)base, end = cur + (words + P_LEN) * sizeof(u_int);
            while (cur < end) {
                P_TAG *tag = (P_TAG*)cur;
#ifdef D2_ORIGIN
                short xs[4] = {0}, ys[4] = {0};
                uint32_t col = 0;
                int nv = 0;
                {
                    const int type = tag->code & 0xF0;
                    if (tag->code == 0xB3) {
                        POLY_FT4 *p = (POLY_FT4*)tag;
                        xs[0] = p->x0; ys[0] = p->y0; xs[1] = p->x1; ys[1] = p->y1;
                        xs[2] = p->x2; ys[2] = p->y2; xs[3] = p->x3; ys[3] = p->y3;
                        col = ((uint32_t)p->r0 << 16) | ((uint32_t)p->g0 << 8) | p->b0;
                        nv = 4;
                    } else if (type == 0x20 || type == 0x30) {
                        const unsigned int *w = (const unsigned int *)tag + P_LEN;
                        const bool quad = (tag->code & 0x08) != 0, gour = (tag->code & 0x10) != 0, tex = (tag->code & 0x04) != 0;
                        const int stride = (gour ? 1 : 0) + 1 + (tex ? 1 : 0);
                        nv = quad ? 4 : 3;
                        for (int i = 0; i < nv; i++) {
                            const short *pxy = (const short *)(gour ? (w + i * stride + 1) : (w + 1 + i * stride));
                            xs[i] = pxy[0]; ys[i] = pxy[1];
                        }
                        col = *w & 0xFFFFFF;
                    } else if (type == 0x40 || type == 0x50) {
                        const int sub = tag->code & 0x0C;
                        if (sub == 0x0C) {
                            LINE_F4 *p = (LINE_F4*)tag;
                            xs[0] = p->x0; ys[0] = p->y0; xs[1] = p->x1; ys[1] = p->y1;
                            xs[2] = p->x2; ys[2] = p->y2; xs[3] = p->x3; ys[3] = p->y3;
                            col = ((uint32_t)p->r0 << 16) | ((uint32_t)p->g0 << 8) | p->b0;
                            nv = 4;
                        } else {
                            LINE_F2 *p = (LINE_F2*)tag;
                            xs[0] = p->x0; ys[0] = p->y0; xs[1] = p->x1; ys[1] = p->y1;
                            col = ((uint32_t)p->r0 << 16) | ((uint32_t)p->g0 << 8) | p->b0;
                            nv = 2;
                        }
                    } else if (type == 0x60 || type == 0x70) {
                        SPRT *p = (SPRT*)tag;
                        xs[0] = p->x0; ys[0] = p->y0; xs[1] = p->x0 + p->w; ys[1] = p->y0;
                        xs[2] = p->x0; ys[2] = p->y0 + p->h; xs[3] = p->x0 + p->w; ys[3] = p->y0 + p->h;
                        col = ((uint32_t)p->r0 << 16) | ((uint32_t)p->g0 << 8) | p->b0;
                        nv = 4;
                    } else if (type == 0xE0) {
                        col = *(const unsigned int *)&tag->pad0;
                        nv = 0;
                    }
                }
#endif
                int n = d2_native_dispatch(tag);
#ifdef D2_ORIGIN
                d2_origin_log_hud(tag, tag->code, (tag->code & 2) != 0, col, xs, ys, nv, s_lastSetModeTextured, s_lastSetModeFmt, s_lastSetModeBlend);
#endif
                uintptr_t next = cur + (n + P_LEN) * sizeof(u_int);
                if (__builtin_expect(n <= 0 || next <= cur || next > end, 0)) {
                    cur = end;
                    break;
                }
                cur = next;
            }
        }
        if (isendprim(base)) break;
        P_TAG *next = (P_TAG*)nextPrim(base);
        if (__builtin_expect(!next || next == base, 0)) break;
        base = next;
    }
#if D2_RSP_GPU
    if (s_d2gpuOvl) rspq_write(s_d2gpuOvl, D2G_CMD_DRAW_END);
#endif
#ifdef D2_ORIGIN
    d2_origin_frame_begin();
#endif
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
#ifdef D2_ORIGIN
    d2_origin_check_large(tag, code, tpage, clut, xs, ys, tex ? us : NULL, tex ? vs : NULL, nv);
#endif
    if (tex)
    {
        if (!quad && !gour) s_ft3Total++;
        else if (quad && !gour) s_directFt4Total++;
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
#ifdef D2_TRANSDBG
    d2_transdbg_inspect(tag, code, semi ? 1 : 0, (int)bm, tex ? tpage : activeDrawEnv.tpage, clut,
                        xs, ys, tex ? us : NULL, tex ? vs : NULL, nv, cr, cg, cb);
#endif

    const int ofx = activeDrawEnv.ofs[0] - activeDispEnv.disp.x, ofy = activeDrawEnv.ofs[1] - activeDispEnv.disp.y;
#if D2_RSP_GPU
    if (!tex && !gour && s_ctxSx16 == 65536 && s_ctxSy16 == 65536)
    {
        short minx = xs[0], maxx = xs[0], miny = ys[0], maxy = ys[0];
        for (int i = 1; i < nv; i++) {
            if (xs[i] < minx) minx = xs[i]; if (xs[i] > maxx) maxx = xs[i];
            if (ys[i] < miny) miny = ys[i]; if (ys[i] > maxy) maxy = ys[i];
        }
        int sx0 = (minx + ofx) << 2, sx1 = (maxx + ofx) << 2;
        int sy0 = (miny + ofy) << 2, sy1 = (maxy + ofy) << 2;
        if (sx1 < s_ctxCx0 || sx0 >= s_ctxCx1 || sy1 < s_ctxCy0 || sy0 >= s_ctxCy1) return len;
        if (sx0 >= -4096 && sx1 <= 4095 && sy0 >= -4096 && sy1 <= 4095)
        {
            if (s_rspOfx != ofx || s_rspOfy != ofy)
            {
                s_rspOfx = ofx;
                s_rspOfy = ofy;
                track_ovl_switch(s_d2gpuOvl);
                rspq_write(s_d2gpuOvl, D2G_CMD_SET_OFFSET, 0, ((uint32_t)(int16_t)ofx << 16) | ((uint32_t)(int16_t)ofy & 0xFFFF));
            }
            __rdpq_autosync_use(AUTOSYNC_PIPE);
            s_triCount += quad ? 2 : 1;
            if (!quad)
            {
                s_rspF3Count++;
                POLY_F3 *p = (POLY_F3 *)tag;
                uint32_t col = shadeOn ? ((uint32_t)s_shadeLut[p->r0] << 16) | ((uint32_t)s_shadeLut[p->g0] << 8) | s_shadeLut[p->b0]
                                       : ((uint32_t)s_shadeLut[128] << 16) | ((uint32_t)s_shadeLut[128] << 8) | s_shadeLut[128];
                uint32_t xy0 = ((uint32_t)(uint16_t)p->x0 << 16) | (uint16_t)p->y0;
                uint32_t xy1 = ((uint32_t)(uint16_t)p->x1 << 16) | (uint16_t)p->y1;
                uint32_t xy2 = ((uint32_t)(uint16_t)p->x2 << 16) | (uint16_t)p->y2;
                track_ovl_switch(s_d2gpuOvl);
                rspq_write(s_d2gpuOvl, D2G_CMD_POLY_F3, col, xy0, xy1, xy2);
                return len;
            }
            else
            {
                s_rspF4Count++;
                POLY_F4 *p = (POLY_F4 *)tag;
                uint32_t col = shadeOn ? ((uint32_t)s_shadeLut[p->r0] << 16) | ((uint32_t)s_shadeLut[p->g0] << 8) | s_shadeLut[p->b0]
                                       : ((uint32_t)s_shadeLut[128] << 16) | ((uint32_t)s_shadeLut[128] << 8) | s_shadeLut[128];
                uint32_t xy0 = ((uint32_t)(uint16_t)p->x0 << 16) | (uint16_t)p->y0;
                uint32_t xy1 = ((uint32_t)(uint16_t)p->x1 << 16) | (uint16_t)p->y1;
                uint32_t xy2 = ((uint32_t)(uint16_t)p->x2 << 16) | (uint16_t)p->y2;
                uint32_t xy3 = ((uint32_t)(uint16_t)p->x3 << 16) | (uint16_t)p->y3;
                track_ovl_switch(s_d2gpuOvl);
                rspq_write(s_d2gpuOvl, D2G_CMD_POLY_F4, col, xy0, xy1, xy2, xy3);
                return len;
            }
        }
    }
#if D2_RSP_TEX
    if (s_d2gpuOvl && tex && quad && !gour && !semi && s_texFormat == TF_4_BIT && s_ctxSx16 == 65536 && s_ctxSy16 == 65536)
    {
        short minx = xs[0], maxx = xs[0], miny = ys[0], maxy = ys[0];
        for (int i = 1; i < 4; i++) {
            if (xs[i] < minx) minx = xs[i]; if (xs[i] > maxx) maxx = xs[i];
            if (ys[i] < miny) miny = ys[i]; if (ys[i] > maxy) maxy = ys[i];
        }
        int sx0 = (minx + ofx) << 2, sx1 = (maxx + ofx) << 2;
        int sy0 = (miny + ofy) << 2, sy1 = (maxy + ofy) << 2;
        if (sx1 < s_ctxCx0 || sx0 >= s_ctxCx1 || sy1 < s_ctxCy0 || sy0 >= s_ctxCy1) return len;
        if (sx0 >= -4096 && sx1 <= 4095 && sy0 >= -4096 && sy1 <= 4095 && sx1 - sx0 <= 16000 && sy1 - sy0 <= 16000)
        {
            int umin = us[0], umax = us[0], vmin = vs[0], vmax = vs[0];
            for (int i = 1; i < 4; i++) {
                if (us[i] < umin) umin = us[i]; if (us[i] > umax) umax = us[i];
                if (vs[i] < vmin) vmin = vs[i]; if (vs[i] > vmax) vmax = vs[i];
            }
            int uw = (umax > umin) ? (umax - umin) : 1;
            int vh = (vmax > vmin) ? (vmax - vmin) : 1;
            if (uw <= 64 && vh <= 64)
            {
                const int px = (tpage & 15) * 64, py = ((tpage >> 4) & 1) * 256;
                if (px >= 512)
                {
                    if (upload_win(px, py, clut, umin, vmin, uw, vh))
                    {
                        if (s_rspOfx != ofx || s_rspOfy != ofy)
                        {
                            s_rspOfx = ofx; s_rspOfy = ofy;
                            track_ovl_switch(s_d2gpuOvl);
                            rspq_write(s_d2gpuOvl, D2G_CMD_SET_OFFSET, 0, ((uint32_t)(int16_t)ofx << 16) | ((uint32_t)(int16_t)ofy & 0xFFFF));
                        }
                        if (s_rspTexOu != s_upOu || s_rspTexOv != s_upOv)
                        {
                            s_rspTexOu = s_upOu; s_rspTexOv = s_upOv;
                            track_ovl_switch(s_d2gpuOvl);
                            rspq_write(s_d2gpuOvl, D2G_CMD_SET_TEX_ORIGIN, 0, ((uint32_t)(uint16_t)s_upOu << 16) | ((uint32_t)(uint16_t)s_upOv & 0xFFFF));
                        }
                        if (s_tmemDirty)
                        {
                            __rdpq_autosync_use(AUTOSYNC_PIPE | AUTOSYNC_TILE(0) | AUTOSYNC_TMEM(0));
                            s_tmemDirty = false;
                        }

                        uint32_t col = shadeOn ? (((uint32_t)s_shadeLut[cr[0]] << 16) | ((uint32_t)s_shadeLut[cg[0]] << 8) | s_shadeLut[cb[0]])
                                               : (((uint32_t)s_shadeLut[128] << 16) | ((uint32_t)s_shadeLut[128] << 8) | s_shadeLut[128]);
                        uint32_t xy0 = ((uint32_t)(uint16_t)xs[0] << 16) | (uint16_t)ys[0];
                        uint32_t xy1 = ((uint32_t)(uint16_t)xs[1] << 16) | (uint16_t)ys[1];
                        uint32_t xy2 = ((uint32_t)(uint16_t)xs[2] << 16) | (uint16_t)ys[2];
                        uint32_t xy3 = ((uint32_t)(uint16_t)xs[3] << 16) | (uint16_t)ys[3];
                        uint32_t uv01 = ((uint32_t)us[0] << 24) | ((uint32_t)vs[0] << 16) | ((uint32_t)us[1] << 8) | vs[1];
                        uint32_t uv23 = ((uint32_t)us[2] << 24) | ((uint32_t)vs[2] << 16) | ((uint32_t)us[3] << 8) | vs[3];

                        track_ovl_switch(s_d2gpuOvl);
                        rspq_write(s_d2gpuOvl, D2G_CMD_POLY_FT4, col, xy0, xy1, xy2, xy3, uv01, uv23);
                        s_rspFT4Count++;
                        s_ft4Rsp++;
                        s_triCount += 2;
                        s_texTriCount += 2;
                        return len;
                    }
                }
            }
        }
    }
#endif
#endif
    PV pv[4];
    uint32_t rgba[4];
    calc_primitive_colors(rgba, nv, shadeOn ? cr : NULL, shadeOn ? cg : NULL, shadeOn ? cb : NULL, bm, tex);
    if (__builtin_expect(s_ctxSx16 == 65536 && s_ctxSy16 == 65536, 1))
    {
        for (int i = 0; i < nv; i++)
        {
            pv[i].X4 = (xs[i] + ofx) << 2;
            pv[i].Y4 = (ys[i] + ofy) << 2;
            pv[i].rgba = rgba[i];
            pv[i].s32 = us[i] << 5; pv[i].t32 = vs[i] << 5;
        }
    }
    else
    {
        for (int i = 0; i < nv; i++)
        {
            pv[i].X4 = ((int)(xs[i] + ofx) * s_ctxSx16) >> 14;
            pv[i].Y4 = ((int)(ys[i] + ofy) * s_ctxSy16) >> 14;
            pv[i].rgba = rgba[i];
            pv[i].s32 = us[i] << 5; pv[i].t32 = vs[i] << 5;
        }
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
        if (quad) tri_core(&pv[1], &pv[3], &pv[2], tpage, clut);
    }
#ifdef D2_DPROF
    s_tTicks += cnt0() - t1;
#endif
    return len;
}
#endif

} // extern "C"
