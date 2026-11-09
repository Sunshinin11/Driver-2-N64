// Renderizador N64 (RDP) da camada PsyX: recebe os triangulos ja transformados (coordenadas de tela do PS1) e desenha com rdpq.
// As texturas do PS1 vivem na VRAM (1024x512 de 16 bits). Para cada triangulo texturizado, a regiao usada da pagina de textura e
// decodificada (CI4/CI8/RGBA16 no formato do RDP) para um cache pequeno e carregada na TMEM; a paleta (CLUT) vai junto.
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
#include "d2_t3d_world.h"

extern "C" {
#include "d2_vram.h"   // VRAM nativa (metade esquerda com bytes trocados, metade direita ja no formato do RDP)
extern DRAWENV activeDrawEnv;
extern DISPENV activeDispEnv;
void d2_sys_note_frame(void);      // sys_n64.c
void d2_log_push(const char *);
int d2_sys_vbl_count(void);
}

#define D2DBG(...) ((void)0)
#define SCREEN_W 320
#define SCREEN_H 240

// ---------------------------------------------------------------- estado de cena
static surface_t *s_fb;
static bool s_scene;
int d2_rdp_frames;
extern "C" int d2_rdp_scene_active(void) { return s_scene; }
extern "C" int d2_rdp_frame_count(void) { return d2_rdp_frames; }
extern "C" surface_t* d2_rdp_get_current_fb(void) { return s_fb; }

TextureID g_whiteTexture = 1, g_vramTexture = 2;
static TexFormat s_texFormat;
static bool s_textured, s_offscreen;
static BlendMode s_blend;
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
#include <rsp.h>
#include <rspq.h>
DEFINE_RSP_UCODE(rsp_d2gpu);
static uint32_t s_d2gpuOvl = 0;
static int s_rspOfx = 0x7FFFFFFF;
static int s_rspOfy = 0x7FFFFFFF;
static int s_rspAlpha = 255;
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
    D2G_CMD_POLY_GT3       = 8,
    D2G_CMD_LOAD_WIN       = 9,
    D2G_CMD_SET_ALPHA      = 10,
};

static void rdp_init_once(void)
{
    if (s_rdpInit) return;
    s_rdpInit = true;
    rdpq_init();
    d2_t3d_init();
    s_d2gpuOvl = rspq_overlay_register(&rsp_d2gpu);
    debugf("[rsp_ovl] registered rsp_d2gpu ucode ovl_id=0x%08lx\n", (unsigned long)s_d2gpuOvl);
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
static int s_palIA = 0;              // paleta da TMEM em formato IA16 (so o alfa = brilho do texel): quads de 4 bits no modo SUBTRACT
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
static inline unsigned short pal_word(int x, int y)
{
    if (!s_palIA) return d2_vram_rdn(x, y);
    return (unsigned short)(psx_to_rgba32(d2_vram_rd(x, y)) & 255);   // IA16: intensidade 0, alfa = max(r,g,b) como no RGBA32
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
static unsigned int s_shLastUse;
static int s_upOu, s_upOv;                         // origem (em texels) da regiao que ficou na TMEM: coordenadas de textura = (u - ou, v - ov)
static TexSlot s_shTag;
static int s_winBase = -1;          // base (0/512 bytes) da imagem de textura ativa para a janela; -1 = outra imagem
static unsigned int s_cBankHitX, s_dCntP, s_dTicks, s_tTicks, s_nTri, s_nClip, s_clipTicks, s_upTicks, s_nSub, s_palT, s_winHitT, s_winMissT, s_regT, s_vwFrame, s_nHitVw;
static inline unsigned int cnt0(void) { unsigned int c; __asm__ volatile("mfc0 %0,$9" : "=r"(c)); return c; }
static unsigned int s_cLdSh, s_cLdReg, s_cLdWin, s_cTlut, s_cWinHit, s_cCalls;   // contadores por quadro (uploads de textura / paleta)                            // so o endereco e usado (marca "TMEM carregada a partir da sombra")
static struct { unsigned int k0, k1, sig; } s_shLast;

static inline void d2_perf_tlut_upload(const void *pal, int idx, int n) {
    rdpq_tex_upload_tlut(pal, idx, n);
}
#define rdpq_tex_upload_tlut d2_perf_tlut_upload

static inline void d2_perf_load_tile(rdpq_tile_t tile, int s0, int t0, int s1, int t1) {
    rdpq_load_tile(tile, s0, t0, s1, t1);
}
#define rdpq_load_tile d2_perf_load_tile

static inline size_t d2_perf_upload_sub(rdpq_tile_t tile, const surface_t *surf, const rdpq_texparms_t *parms, int s0, int t0, int s1, int t1) {
    return rdpq_tex_upload_sub(tile, surf, parms, s0, t0, s1, t1);
}
#define rdpq_tex_upload_sub d2_perf_upload_sub

// o RDP le a metade direita da VRAM (paginas de textura) por DMA: antes de reescreve-la, espera ele terminar
extern "C" void d2_vram_r_barrier(void) { if (s_shLastUse > s_doneCounter) rdp_wait_idle(); }

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
        int cx, cy; d2_clut_xy((unsigned)clut & 0xFFFF, &cx, &cy);
        sig ^= cell_signature(cx, cy, fmt == TF_4_BIT ? 16 : 256, 1);
    }

    TexSlot *s = 0;
    for (int i = 0; i < 4; i++)
    {
        TexSlot *c = &s_slots[(base + i) % TEX_SLOTS];
        if (c->valid && c->key0 == k0 && c->key1 == k1 && c->key2 == k2 && c->sig == sig) {
            c->use = ++s_useCounter; s_dbgHit++;
            return c;
        }
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
        int cx, cy; d2_clut_xy((unsigned)clut & 0xFFFF, &cx, &cy);
        for (int y = 0; y < sh; y++)
        {
            const int vy = py + v0 + y * step;
            uint32_t *d = d32 + y * wpad;
            if (fmt == TF_4_BIT)
            {
                for (int x = 0; x < sw; x++)
                {
                    int u = u0 + x * step;
                    unsigned int word = d2_vram_rd(px + (u >> 2), vy);
                    int idx = (word >> ((u & 3) * 4)) & 15;
                    unsigned short psx_col = d2_vram_rd(cx + idx, cy);
                    d[x] = psx_to_rgba32(psx_col);
                }
            }
            else if (fmt == TF_8_BIT)
            {
                for (int x = 0; x < sw; x++)
                {
                    int u = u0 + x * step;
                    unsigned int word = d2_vram_rd(px + (u >> 1), vy);
                    int idx = (word >> ((u & 1) * 8)) & 255;
                    unsigned short psx_col = d2_vram_rd(cx + idx, cy);
                    d[x] = psx_to_rgba32(psx_col);
                }
            }
            else
            {
                for (int x = 0; x < sw; x++)
                {
                    int u = u0 + x * step;
                    unsigned short psx_col = d2_vram_rd(px + u, vy);
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
            const int vy = py + v0 + y * step;
            if (fmt == TF_4_BIT)
            {
                unsigned char *d = dst + y * (wpad / 2);
                int x = 0;
                if (step == 1 && (u0 & 3) == 0)
                {
                    // 4 texels (uma palavra da VRAM) por vez: troca os nibbles de cada byte (o RDP quer o texel da esquerda no nibble alto)
                    const int words = sw >> 2;
                    const int x0 = (px + (u0 >> 2)) & 1023;
                    unsigned short *d16 = (unsigned short *)d;
                    if (x0 >= 512 && x0 + words <= 1024)
                        memcpy(d16, &d2_vramR[(vy & 511) * 512 + (x0 - 512)], (size_t)words * 2);   // metade direita: ja no formato do RDP
                    else
                        for (int k = 0; k < words; k++) d16[k] = d2_vconv(d2_vram_rd(x0 + k, vy));
                    x = words << 2;
                }
                for (; x < sw; x++)
                {
                    int u = u0 + x * step;
                    unsigned int word = d2_vram_rd(px + (u >> 2), vy);
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
                    const int words = sw >> 1;
                    unsigned short *d16 = (unsigned short *)d;
                    const int x0 = (px + (u0 >> 1)) & 1023;
                    if (x0 + words <= 512)
                        memcpy(d16, &d2_vramL[(vy & 511) * 512 + x0], (size_t)words * 2);   // metade esquerda: ja na ordem de memoria do N64
                    else
                        for (int k = 0; k < words; k++) { unsigned int wv = d2_vram_rd(x0 + k, vy); d16[k] = (unsigned short)((wv << 8) | (wv >> 8)); }
                    x = words << 1;
                }
                for (; x < sw; x++)
                {
                    int u = u0 + x * step;
                    unsigned int word = d2_vram_rd(px + (u >> 1), vy);
                    d[x] = (unsigned char)(word >> ((u & 1) * 8));
                }
                for (int pb = sw; pb < wpad; pb++) d[pb] = 0;
            }
            else
            {
                unsigned short *d = (unsigned short *)(dst + y * wpad * 2);
                for (int x = 0; x < sw; x++) d[x] = d2_vram_rdn(px + u0 + x * step, vy);
                for (int x = sw; x < wpad; x++) d[x] = 0;
            }
        }
    }
    data_cache_hit_writeback(dst, (bytes + 15) & ~15);
    s->key0 = k0; s->key1 = k1; s->key2 = k2; s->sig = sig; s->valid = 1; s->gen = ++s_genCounter; s->use = ++s_useCounter;
    return s;
}

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

// ---------------------------------------------------------------- modos do RDP
static void set_mode(bool textured, int fmt, BlendMode blend)
{
    int rgba_mode = 0;
    if (blend == BM_ADD) rgba_mode = 1;
    else if (blend == BM_ADD_QUATER_SOURCE) rgba_mode = 2;
    else if (blend == BM_SUBTRACT) rgba_mode = 3;
    // SUBTRACT com textura de 4 bits: o combinador so usa o alfa do texel (= max(r,g,b)), entao CI4 + TLUT IA16 reproduz o RGBA32 e vai pela janela/RSP
    const bool ciSub = textured && fmt == TF_4_BIT && blend == BM_SUBTRACT;
    const int modeBlend = rgba_mode;
    if (ciSub) rgba_mode = 0;
    s_palIA = ciSub ? 1 : 0;

    if (rgba_mode != s_curRgbaMode)
    {
        s_lastUp.clut = -1;
        s_lastUp.palEnt = 0;
        banks_invalidate();
        s_curRgbaMode = rgba_mode;
    }
    s_curBlend = blend;
    int key = textured ? (1 + fmt) * 8 + (int)blend : (int)blend;
    if (key == s_modeKey) return;
    s_modeKey = key;
    s_lastUp.slot = 0;     // o estado de textura se perde ao trocar de modo (palette/tlut)
    s_lastUp.clut = -1;
    s_lastUp.palEnt = 0;
    s_leanTiles = false;
    s_tmemWin = false;
    s_winBase = -1;
    s_curBank = -1;
    s_pbClut = -1;
    rdpq_set_mode_standard();
    rdpq_mode_antialias(AA_NONE);
    rdpq_mode_dithering(DITHER_NONE_NONE);
    rdpq_mode_filter(FILTER_POINT);

    const bool isRgba32 = (modeBlend != 0);
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
            rdpq_mode_tlut(ciSub ? TLUT_IA16 : TLUT_NONE);
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

extern "C" void d2_rdp_invalidate_backend_cache(void)
{
    rdpq_sync_pipe();
    rdpq_sync_load();
    rdpq_sync_tile();

    // Restaura o scissor ativo do backend do Driver 2
    rdpq_set_scissor(s_scx0, s_scy0, s_scx1, s_scy1);

    s_modeKey = -1;
    s_curRgbaMode = -1;
    s_curBlend = (BlendMode)-1;

    memset(&s_lastUp, 0, sizeof(s_lastUp));
    s_lastUp.clut = -1;
    s_lastUp.palVer = (unsigned int)-1;

    s_shLast.k0 = 0;
    s_shLast.k1 = 0;
    s_shLast.sig = 0;

    s_leanTiles = false;
    s_tmemWin = false;
    s_winBase = -1;
    s_curBank = -1;
    s_pbClut = -1;
    banks_invalidate();
}


static void upload_pal_bank(int clut)
{
    const int pkey = clut | (s_palIA ? 0x10000 : 0);
    if (pkey == s_pbClut && s_pbVw == s_vramWrites && s_curBank > 0 && s_bank[s_curBank].clut == pkey) { s_bank[s_curBank].lru = ++s_bankLru; return; }
    s_pbClut = pkey; s_pbVw = s_vramWrites;
    PalEnt *e = &s_palCache[(pkey ^ (pkey >> 8)) & (PAL_CACHE - 1)];
    int cx, cy; d2_clut_xy((unsigned)clut & 0xFFFF, &cx, &cy);
    bool ok = e->valid && e->clut == pkey;
    if (ok && e->vw != s_vramWrites)
    {
        const unsigned int sig = cell_signature(cx, cy, 16, 1);
        if (sig != e->sig) ok = false; else e->vw = s_vramWrites;
    }
    if (!ok)
    {
        if (e->valid && e->use > s_doneCounter) rdp_wait_idle();      // o RDP ainda pode estar lendo esta paleta
        for (int i = 0; i < 16; i++) e->pal[i] = pal_word(cx + i, cy);
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
        if (s_d2gpuOvl)
        {
            track_ovl_switch(s_d2gpuOvl);
            rspq_write(s_d2gpuOvl, D2G_CMD_SET_TILE_PAL, 0, (uint32_t)b);
            s_curBank = b;
            s_tmemDirty = true;
        }
        else
        {
            track_ovl_switch(0);
            rdpq_tileparms_t tp = {}; tp.palette = b;
            rdpq_set_tile(TILE0, FMT_CI4, 0, 32, &tp);
            s_curBank = b;
            s_tmemDirty = true;
        }
    }
}

// ---------------------------------------------------------------- D2_TEXWIN: janela fixa de 64x64 texels (CI4, 2 KB) na TMEM
// Em vez de carregar o retangulo exato de cada triangulo, carrega uma janela de 64x64 da VRAM sombra e mantem enquanto os
// triangulos seguintes couberem nela (mesma pagina, VRAM nao alterada). A carga e um unico LOAD_TILE (tiles fixos).
static struct { int px, py, wu0, wv0; unsigned int sig, vw; } s_win;
static TexSlot s_winTag;
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
        hit = false;          // a VRAM mudou desde a carga (raro durante o jogo): recarrega
    }
    upload_pal_bank(clut);
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
        upload_pal_bank(clut);       // TILE0 foi refeito com o banco 0: reaplica o banco da paleta atual
        s_tmemDirty = true;
    }
    // coordenadas de carga em bytes da sombra (2 texels CI4 por byte): coluna = ((px-512)*4 + wu0)/2
    // o RDP limita coordenadas de carga a < 1024: a imagem comeca no byte 0 ou 512 da linha (troca so quando muda)
    int bx = ((px & 511) * 4 + wu0) >> 1; const int by = py + wv0;
    const int base = bx >= 512 ? 512 : 0;
    { const int bkey = base + (px < 512 ? 1024 : 0);
    if (bkey != s_winBase) { track_ovl_switch(0); rdpq_set_texture_image_raw(0, PhysicalAddr((const char *)(px < 512 ? d2_vramLc : d2_vramR) + base), FMT_I8, SH_W * 2, 512); s_winBase = bkey; } }
    bx -= base;
    if (s_d2gpuOvl)
    {
        // LOAD_TILE do RDP escrito pelo proprio overlay (sem trocar de overlay do RSP)
        track_ovl_switch(s_d2gpuOvl);
        rspq_write(s_d2gpuOvl, D2G_CMD_LOAD_WIN, 0,
            (0x34u << 24) | (((uint32_t)(bx * 4) & 0xFFF) << 12) | ((uint32_t)(by * 4) & 0xFFF),
            (6u << 24) | (((uint32_t)((bx + 32) * 4 - 4) & 0xFFF) << 12) | ((uint32_t)((by + 64) * 4 - 4) & 0xFFF));
    }
    else
    {
        track_ovl_switch(0);
        rdpq_load_tile(TILE6, bx, by, bx + 32, by + 64);
    }
    s_cLdWin++;
    s_tmemWin = true;
    s_tmemDirty = true;
    s_upOu = wu0; s_upOv = wv0; s_lastUp.slot = &s_winTag;
    return true;
}

// carrega a regiao (u0,v0,w,h, passo step) da pagina de textura na TMEM (paleta inclusa). Retorna false se nao coube.
static bool upload_region(int page, int clut, int u0, int v0, int w, int h, int step)
{
    if (w <= 0 || h <= 0) return false;
    const int px = (page & 15) * 64, py = ((page >> 4) & 1) * 256;
    if (!s_curRgbaMode && s_texFormat == TF_4_BIT && step == 1 && w <= 64 && h <= 64)
    {
        return upload_win(px, py, clut, u0, v0, w, h);
    }
    s_upOu = u0; s_upOv = v0;
    int sw, sh, wpad;
    bool useShadow = false;
    int ua = 0, e0 = 0, e1 = 0;
    if (!s_curRgbaMode && s_texFormat == TF_4_BIT && step == 1)
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
        shSig = cell_signature(wx, py + v0, ww, h);
        shK0 = (unsigned)px | ((unsigned)py << 10) | ((unsigned)u0 << 20);
        shK1 = (unsigned)v0 | ((unsigned)w << 8) | ((unsigned)h << 20);
        s_shLastUse = ++s_useCounter;
        s_upOu = ua + ((u0 - ua) & ~1);   // origem da carga direta (mesma em acerto e em carga nova)
    }

    if (!s_curRgbaMode)
    {
        const bool paletted = s_texFormat != TF_16_BIT;
        const int pkey = clut | (s_palIA ? 0x10000 : 0);
        if (false) {}
        else if (paletted && (s_lastUp.clut != pkey || s_lastUp.fmt != s_texFormat || s_lastUp.palVer != s_vramWrites || s_lastUp.slot == 0))
        {
            int cx, cy; d2_clut_xy((unsigned)clut & 0xFFFF, &cx, &cy);
            const int n = s_texFormat == TF_4_BIT ? 16 : 256;
            s_palIdx = (s_palIdx + 1) % PAL_RING;
            if (s_palUse[s_palIdx] > s_doneCounter) rdp_wait_idle();
            unsigned short *pal = s_palRing[s_palIdx];
            for (int i = 0; i < n; i++) pal[i] = pal_word(cx + i, cy);
            data_cache_hit_writeback(pal, n * 2);
            rdpq_tex_upload_tlut(pal, 0, n); s_cTlut++; s_winBase = -1; if (n > 16) banks_invalidate(); else s_bank[0].clut = -1;
            if (n > 16) banks_invalidate();
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
            // carga direta da sombra (como a janela): bytes [e0/2, e1/2) das linhas v0..v0+h, origem da textura em ua+e0
            const int e0 = (u0 - ua) & ~1, e1 = ((u0 - ua) + w + 1) & ~1, nb = (e1 - e0) >> 1, pitch = (nb + 7) & ~7;
            int bx = (px & 511) * 2 + ((ua + e0) >> 1); const int by = py + v0, base = bx >= 512 ? 512 : 0;
            { const int bkey = base + (px < 512 ? 1024 : 0);
            if (bkey != s_winBase) { rdpq_set_texture_image_raw(0, PhysicalAddr((const char *)(px < 512 ? d2_vramLc : d2_vramR) + base), FMT_I8, SH_W * 2, 512); s_winBase = bkey; } }
            bx -= base;
            rdpq_set_tile(TILE6, FMT_I8, 0, pitch, NULL);
            rdpq_load_tile(TILE6, bx, by, bx + nb, by + h);
            rdpq_set_tile(TILE0, FMT_CI4, 0, pitch, NULL);
            rdpq_set_tile_size(TILE0, 0, 0, e1 - e0, h);
            s_upOu = ua + e0;
            s_cLdSh++; s_leanTiles = false; s_tmemWin = false;
            s_curBank = -1; s_pbClut = -1;
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
            s_curBank = -1; s_pbClut = -1;
        }
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
            s_curBank = -1; s_pbClut = -1;
        }
        s_lastUp.slot = slot; s_lastUp.gen = slot->gen;
    }
    return true;
}

// Share the original game's live VRAM pages with native 3D. No frozen sprite
// association: region streaming and CLUT changes use the same invalidation path.
extern "C" int d2_rdp_t3d_texture(int page, int clut, int u, int v, int w, int h, int* ou, int* ov)
{
    s_texFormat = TF_4_BIT;
    s_curRgbaMode = 0;
    rdpq_mode_tlut(TLUT_RGBA16);
    if (!upload_region(page, clut, u, v, w, h, 1)) return 0;
    *ou = s_upOu;
    *ov = s_upOv;
    return 1;
}

// paleta de 16 ou 256 cores (recarrega se mudou CLUT, formato, VRAM ou modo)
static bool upload_pal(int clut)
{
    const int pkey = clut | (s_palIA ? 0x10000 : 0);
    if (s_lastUp.clut == pkey && s_lastUp.fmt == s_texFormat && s_lastUp.palVer == s_vramWrites && s_lastUp.slot != 0) return false;
    int cx, cy; d2_clut_xy((unsigned)clut & 0xFFFF, &cx, &cy);
    const int n = s_texFormat == TF_4_BIT ? 16 : 256;
    s_palIdx = (s_palIdx + 1) % PAL_RING;
    if (s_palUse[s_palIdx] > s_doneCounter) rdp_wait_idle();
    unsigned short *pal = s_palRing[s_palIdx];
    for (int i = 0; i < n; i++) pal[i] = pal_word(cx + i, cy);
    data_cache_hit_writeback(pal, n * 2);
    rdpq_tex_upload_tlut(pal, 0, n); s_cTlut++; s_winBase = -1; if (n > 16) banks_invalidate(); else s_bank[0].clut = -1;
    if (n > 16) banks_invalidate();
    s_palUse[s_palIdx] = ++s_useCounter;
    s_lastUp.clut = pkey; s_lastUp.fmt = s_texFormat; s_lastUp.palVer = s_vramWrites; s_lastUp.palEnt = 0;
    return true;
}

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


// ---------------------------------------------------------------- contrato GR_*
extern "C" {

extern "C" void d2_audio_pump(void);
char PsyX_BeginScene(void)
{
    if (s_scene) return 0;
    d2_audio_pump();
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
    s_leanTiles = false;
    s_tmemWin = false;
    s_winBase = -1;
    banks_invalidate();
    D2DBG("[rdp] BeginScene disp=%d,%d %dx%d clip=%d,%d %dx%d vbl=%d\n", activeDispEnv.disp.x, activeDispEnv.disp.y, activeDispEnv.disp.w, activeDispEnv.disp.h, activeDrawEnv.clip.x, activeDrawEnv.clip.y, activeDrawEnv.clip.w, activeDrawEnv.clip.h, d2_sys_vbl_count());
    return 1;
}

void PsyX_EndScene(void)
{
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
    {   // contador de FPS (media de 8 quadros) em sete segmentos com retangulos de preenchimento (texto da rdpq custava ~6% do quadro)
        static unsigned t0f, nf, fps10; static bool fpsOn = true, comboPrev;
        {   // L + R + Start liga/desliga o contador (ligado por padrao; desligado nao custa nada)
            const joypad_buttons_t bt = joypad_get_buttons_held(JOYPAD_PORT_1);
            const bool combo = bt.l && bt.r && bt.start;
            if (combo && !comboPrev) fpsOn = !fpsOn;
            comboPrev = combo;
        }
        if (nf == 0) t0f = t0f ? t0f : get_ticks();
        if (++nf == 8) { const unsigned t = get_ticks(); fps10 = (unsigned)((8ull * 46875000ull * 10ull) / (unsigned)(t - t0f)); t0f = t; nf = 0; }
        static const unsigned char seg[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };   // bits a..g
        static const signed char sr[7][4] = { {1,0,7,2}, {6,1,8,7}, {6,7,8,13}, {1,12,7,14}, {0,7,2,13}, {0,1,2,7}, {1,6,7,8} };
        const unsigned v = fps10 > 999 ? 999 : fps10;
        const int x0 = 24, y0 = 24;
        if (fpsOn) {
        track_ovl_switch(0);
        rdpq_set_mode_fill(RGBA32(0, 0, 0, 255));
        rdpq_fill_rectangle(x0 - 2, y0 - 2, x0 + 40, y0 + 16);
        rdpq_set_fill_color(RGBA32(255, 255, 255, 255));
        int dx = x0;
        const int digs[3] = { (int)(v / 100), (int)((v / 10) % 10), (int)(v % 10) };
        for (int d = 0; d < 3; d++)
        {
            if (d == 2) { rdpq_fill_rectangle(dx, y0 + 12, dx + 2, y0 + 14); dx += 4; }
            if (d == 0 && digs[0] == 0) continue;
            for (int k = 0; k < 7; k++)
                if (seg[digs[d]] & (1 << k)) rdpq_fill_rectangle(dx + sr[k][0], y0 + sr[k][1], dx + sr[k][2], y0 + sr[k][3]);
            dx += 11;
        }
        }
    }
    track_ovl_switch(0);
    rdpq_detach_show();
    d2_audio_pump();
    {   // medicao alinhada ao jogo: janelas de 32 passos de CameraCnt (media, p95, pior quadro, atrasos e posicao)
        extern int CameraCnt;
        extern VECTOR camera_position;
        extern int LoadedArea;
        static int lastCC = -1, prevFrameCC = -1, prevVbl = 0, winVbl0 = 0;
        static unsigned int t0 = 0, prevT = 0, winTrisSum = 0, winTexSum = 0;
        static unsigned int winDt[64];
        static int winN = 0, winLate2 = 0, winLate3 = 0;
        unsigned int now; __asm__ volatile("mfc0 %0,$9" : "=r"(now));
        const int curVbl = d2_sys_vbl_count();
        if (CameraCnt < lastCC || lastCC < 0 || CameraCnt == 0) {
            lastCC = CameraCnt; prevFrameCC = CameraCnt;
            t0 = prevT = now; winVbl0 = prevVbl = curVbl;
            winN = winLate2 = winLate3 = 0; winTrisSum = winTexSum = 0;
        } else if (CameraCnt > prevFrameCC) {
            unsigned int dt = now - prevT;
            int dvbl = curVbl - prevVbl;
            prevT = now; prevVbl = curVbl; prevFrameCC = CameraCnt;
            if (winN < 64) winDt[winN++] = dt;
            if (dvbl > 2) winLate2++;
            if (dvbl > 3) winLate3++;
            winTrisSum += (unsigned)s_benchTris;
            winTexSum += (unsigned)s_benchTex;
            if ((CameraCnt >> 5) != (lastCC >> 5)) {
                unsigned int sorted[64];
                for (int i = 0; i < winN; i++) {
                    unsigned int v = winDt[i];
                    int j = i - 1;
                    while (j >= 0 && sorted[j] > v) { sorted[j + 1] = sorted[j]; j--; }
                    sorted[j + 1] = v;
                }
                unsigned int p95 = winN > 0 ? sorted[(winN * 95) / 100 < winN ? (winN * 95) / 100 : winN - 1] : 0;
                unsigned int wmax = winN > 0 ? sorted[winN - 1] : 0;
                int wvbl = curVbl - winVbl0;
                unsigned int fps100 = wvbl > 0 ? (unsigned)((CameraCnt - lastCC) * 6000 + (wvbl >> 1)) / (unsigned)wvbl : 0;
                unsigned int dCC = (unsigned)(CameraCnt - lastCC);
                debugf("[bench] cc=%d ticks=%u p95=%u max=%u vbl=%d fps100=%u late2=%d late3=%d tris=%d tex=%d avg_tris=%u pos=%d,%d area=%d\n",
                       CameraCnt & ~31, (now - t0) / dCC, p95, wmax, wvbl, fps100, winLate2, winLate3,
                       s_benchTris, s_benchTex, winN > 0 ? winTrisSum / (unsigned)winN : 0,
                       camera_position.vx, camera_position.vz, LoadedArea);
                lastCC = CameraCnt; t0 = now; winVbl0 = curVbl;
                winN = winLate2 = winLate3 = 0; winTrisSum = winTexSum = 0;
            }
        }
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
void GR_StoreFrameBuffer(int, int, int, int) {}
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
    const PV *v[3] = { A, B, C };
    int X4[3] = { A->X4, B->X4, C->X4 }, Y4[3] = { A->Y4, B->Y4, C->Y4 };
    uint32_t rgba[3] = { A->rgba, B->rgba, C->rgba };
    int mnx = X4[0], mxx = X4[0], mny = Y4[0], mxy = Y4[0];
    if (X4[1] < mnx) mnx = X4[1]; if (X4[1] > mxx) mxx = X4[1]; if (Y4[1] < mny) mny = Y4[1]; if (Y4[1] > mxy) mxy = Y4[1];
    if (X4[2] < mnx) mnx = X4[2]; if (X4[2] > mxx) mxx = X4[2]; if (Y4[2] < mny) mny = Y4[2]; if (Y4[2] > mxy) mxy = Y4[2];
    // fora do scissor?
    if (mxx < s_ctxCx0 || mnx >= s_ctxCx1 || mxy < s_ctxCy0 || mny >= s_ctxCy1)
    {
        return;
    }

    // Descarte de triangulos de area zero (degenerados), como a GPU do PS1
    long long dx01 = (long long)X4[1] - X4[0], dy01 = (long long)Y4[1] - Y4[0];
    long long dx02 = (long long)X4[2] - X4[0], dy02 = (long long)Y4[2] - Y4[0];
    if (dx01 * dy02 - dx02 * dy01 == 0LL)
    {
        return;
    }


    // Regra da GPU do PS1: descarta o triangulo quando a largura for > 1023 ou a altura > 511 em coordenadas PSX
    if (!s_noSizeLimit)
    {
        const int dX4 = mxx - mnx;
        const int dY4 = mxy - mny;
        if ((long long)dX4 * 16384LL > 1023LL * (long long)s_ctxSx16 ||
            (long long)dY4 * 16384LL > 511LL * (long long)s_ctxSy16)
        {
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
            return;
        }
        np = clip_poly_screen(ptmp1, np, ptmp2, 0, (float)(SCREEN_W + 64), false);
        if (np < 3) {
            return;
        }
        np = clip_poly_screen(ptmp2, np, ptmp1, 1, -64.0f, true);
        if (np < 3) {
            return;
        }
        np = clip_poly_screen(ptmp1, np, ptmp2, 1, (float)(SCREEN_H + 64), false);
        if (np < 3) {
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
        if (!upload_region(page, clut, umin, vmin, w, h, step))
        {
            w = uw; h = vh;      // a margem de +1 estoura a TMEM em blocos de 64x64 (4 bits): tenta sem ela
            if (!upload_region(page, clut, umin, vmin, w, h, step))
            {
                return;
            }
        }
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
                continue;
            }
            n = clip_poly(b, n, a, 0, (float)(tu + bw), false);  if (n < 3) {
                continue;
            }
            n = clip_poly(a, n, b, 1, (float)tv, true);          if (n < 3) {
                continue;
            }
            n = clip_poly(b, n, a, 1, (float)(tv + bh), false);  if (n < 3) {
                continue;
            }
            int w = bw, h = bh;
            if (tu + w > 256) w = 256 - tu;
            if (tv + h > 256) h = 256 - tv;
            if (w <= 0 || h <= 0) continue;
            bool up_ok = upload_region(page, clut, tu, tv, w, h, step);
            if (!up_ok) {
                continue;
            }
            for (int i = 1; i + 1 < n; i++) {
                emit_tri(true, a[0], a[i], a[i + 1], (float)(step == 1 ? s_upOu : tu), (float)(step == 1 ? s_upOv : tv), 1.0f / step, 1.0f, 1.0f);
                s_nSub++;
            }
        }
}

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

static inline uint32_t calc_vertex_color_raw(unsigned r, unsigned g, unsigned b, BlendMode blend, bool textured)
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

static inline uint32_t calc_vertex_color(unsigned r, unsigned g, unsigned b, BlendMode blend, bool textured)
{
    return calc_vertex_color_raw(r, g, b, blend, textured);
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

// ---------------------------------------------------------------- caminho direto: poligono do PS1 (OT) -> RDP, sem GrVertex/lotes
// Chamado por ProcessFlatPoly/ProcessGouraudPoly (PsyX_GPU.cpp). Devolve o tamanho do primitivo (em palavras) ou 0 para o caminho antigo.
static unsigned int s_dCnt;
int d2_direct_poly_i(P_TAG *tag);
int d2_direct_poly(P_TAG *tag);
// quad texturizado de 4 bits (pagina em x >= 512): o RSP monta os dois triangulos, com 7 palavras de comando em vez de ~30 escritas pela CPU
static bool rsp_ft4(const short *xs, const short *ys, const unsigned char *us, const unsigned char *vs, int tpage, int clut, uint32_t col, int ofx, int ofy, int alpha = 255)
{
    if (!s_d2gpuOvl || s_texFormat != TF_4_BIT || s_ctxSx16 != 65536 || s_ctxSy16 != 65536) return false;
    short minx = xs[0], maxx = xs[0], miny = ys[0], maxy = ys[0];
    int umin = us[0], umax = us[0], vmin = vs[0], vmax = vs[0];
    for (int i = 1; i < 4; i++)
    {
        if (xs[i] < minx) minx = xs[i]; if (xs[i] > maxx) maxx = xs[i];
        if (ys[i] < miny) miny = ys[i]; if (ys[i] > maxy) maxy = ys[i];
        if (us[i] < umin) umin = us[i]; if (us[i] > umax) umax = us[i];
        if (vs[i] < vmin) vmin = vs[i]; if (vs[i] > vmax) vmax = vs[i];
    }
    const int sx0 = (minx + ofx) << 2, sx1 = (maxx + ofx) << 2, sy0 = (miny + ofy) << 2, sy1 = (maxy + ofy) << 2;
    if (sx1 < s_ctxCx0 || sx0 >= s_ctxCx1 || sy1 < s_ctxCy0 || sy0 >= s_ctxCy1) return true;     // fora do recorte: nada a desenhar
    if (sx0 < -4096 || sx1 > 4095 || sy0 < -4096 || sy1 > 4095 || sx1 - sx0 > 16000 || sy1 - sy0 > 16000) return false;
    const int uw = (umax > umin) ? (umax - umin) : 1, vh = (vmax > vmin) ? (vmax - vmin) : 1;
    if (uw > 64 || vh > 64 || !upload_win((tpage & 15) * 64, ((tpage >> 4) & 1) * 256, clut, umin, vmin, uw, vh)) return false;
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
    const uint32_t xy0 = ((uint32_t)(uint16_t)xs[0] << 16) | (uint16_t)ys[0], xy1 = ((uint32_t)(uint16_t)xs[1] << 16) | (uint16_t)ys[1];
    const uint32_t xy2 = ((uint32_t)(uint16_t)xs[2] << 16) | (uint16_t)ys[2], xy3 = ((uint32_t)(uint16_t)xs[3] << 16) | (uint16_t)ys[3];
    const uint32_t uv01 = ((uint32_t)us[0] << 24) | ((uint32_t)vs[0] << 16) | ((uint32_t)us[1] << 8) | vs[1];
    const uint32_t uv23 = ((uint32_t)us[2] << 24) | ((uint32_t)vs[2] << 16) | ((uint32_t)us[3] << 8) | vs[3];
    if (alpha != s_rspAlpha)
    {
        s_rspAlpha = alpha;
        track_ovl_switch(s_d2gpuOvl);
        rspq_write(s_d2gpuOvl, D2G_CMD_SET_ALPHA, 0, (uint32_t)alpha);
    }
    track_ovl_switch(s_d2gpuOvl);
    rspq_write(s_d2gpuOvl, D2G_CMD_POLY_FT4, col, xy0, xy1, xy2, xy3, uv01, uv23);
    s_rspFT4Count++;
    s_triCount += 2;
    s_texTriCount += 2;
    return true;
}
// triangulo texturizado de 4 bits com cor por vertice (gouraud), montado pelo RSP
static bool rsp_gt3(const short *xs, const short *ys, const unsigned char *us, const unsigned char *vs, int tpage, int clut, const uint32_t *col, int ofx, int ofy)
{
    if (!s_d2gpuOvl || s_texFormat != TF_4_BIT || s_ctxSx16 != 65536 || s_ctxSy16 != 65536) return false;
    short minx = xs[0], maxx = xs[0], miny = ys[0], maxy = ys[0];
    int umin = us[0], umax = us[0], vmin = vs[0], vmax = vs[0];
    for (int i = 1; i < 3; i++)
    {
        if (xs[i] < minx) minx = xs[i]; if (xs[i] > maxx) maxx = xs[i];
        if (ys[i] < miny) miny = ys[i]; if (ys[i] > maxy) maxy = ys[i];
        if (us[i] < umin) umin = us[i]; if (us[i] > umax) umax = us[i];
        if (vs[i] < vmin) vmin = vs[i]; if (vs[i] > vmax) vmax = vs[i];
    }
    const int sx0 = (minx + ofx) << 2, sx1 = (maxx + ofx) << 2, sy0 = (miny + ofy) << 2, sy1 = (maxy + ofy) << 2;
    if (sx1 < s_ctxCx0 || sx0 >= s_ctxCx1 || sy1 < s_ctxCy0 || sy0 >= s_ctxCy1) return true;
    if (sx0 < -4096 || sx1 > 4095 || sy0 < -4096 || sy1 > 4095 || sx1 - sx0 > 16000 || sy1 - sy0 > 16000) return false;
    const int uw = (umax > umin) ? (umax - umin) : 1, vh = (vmax > vmin) ? (vmax - vmin) : 1;
    if (uw > 64 || vh > 64 || !upload_win((tpage & 15) * 64, ((tpage >> 4) & 1) * 256, clut, umin, vmin, uw, vh)) return false;
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
    const uint32_t xy0 = ((uint32_t)(uint16_t)xs[0] << 16) | (uint16_t)ys[0], xy1 = ((uint32_t)(uint16_t)xs[1] << 16) | (uint16_t)ys[1];
    const uint32_t xy2 = ((uint32_t)(uint16_t)xs[2] << 16) | (uint16_t)ys[2];
    const uint32_t uv01 = ((uint32_t)us[0] << 24) | ((uint32_t)vs[0] << 16) | ((uint32_t)us[1] << 8) | vs[1];
    const uint32_t uv2 = ((uint32_t)us[2] << 24) | ((uint32_t)vs[2] << 16);
    track_ovl_switch(s_d2gpuOvl);
    rspq_write(s_d2gpuOvl, D2G_CMD_POLY_GT3, col[0], xy0, xy1, xy2, uv01, uv2, col[1], col[2]);
    s_rspFT4Count++;
    s_triCount++;
    s_texTriCount++;
    return true;
}
int d2_native_ft4(POLY_FT4 *p)
{
    if (!activeDrawEnv.dfe || !s_scene) return 9;
    if (!ensure_slots()) return 9;

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
    {
        const short xs[4] = { p->x0, p->x1, p->x2, p->x3 }, ys[4] = { p->y0, p->y1, p->y2, p->y3 };
        const unsigned char us[4] = { p->u0, p->u1, p->u2, p->u3 }, vs[4] = { p->v0, p->v1, p->v2, p->v3 };
        const uint32_t col = ((uint32_t)s_shadeLut[p->r0] << 16) | ((uint32_t)s_shadeLut[p->g0] << 8) | s_shadeLut[p->b0];
        if (rsp_ft4(xs, ys, us, vs, p->tpage, p->clut, col, ofx, ofy)) return 9;
    }
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
    
    int w, h, len;
    if (kind == 0x60) { TILE *p = (TILE *)tag; w = p->w; h = p->h; len = 3; }
    else if (kind == 0x64) { SPRT *p = (SPRT *)tag; w = p->w; h = p->h; len = 4; }
    else if (kind == 0x68) { w = h = 1; len = 2; }
    else if (kind == 0x70 || kind == 0x74) { w = h = 8; len = kind == 0x74 ? 3 : 2; }
    else if (kind == 0x78 || kind == 0x7C) { w = h = 16; len = kind == 0x7C ? 3 : 2; }
    else return 0;
    if (!activeDrawEnv.dfe || !s_scene || w <= 0 || h <= 0) return len;
    if (!ensure_slots()) return len;

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
    if(sub==1) return 2;
    if(sub==2) return 2; /* marcador textual de debug */
    return 0;
}
int d2_native_vram_packet(P_TAG *tag)
{
    const int type=tag->code&0xF0;
    if(type==0 && (tag->code&15)==0) return 3;
    
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
    if (__builtin_expect(tag->code == 0xB3 || tag->code == 0x2C, 1)) return d2_native_ft4((POLY_FT4*)tag);
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
// ---- OT nativo (Etapa 3): vetor de baldes por slot, sem lista ligada entre slots e sem terminador ----
struct D2OtReg { uintptr_t base; int n; };
static D2OtReg s_otReg[8];
static int s_otRegN = 0, s_otRegRR = 0;
extern "C" void d2_ot_register(void *ot, int n)
{
    const uintptr_t b = (uintptr_t)ot;
    for (int i = 0; i < s_otRegN; i++) if (s_otReg[i].base == b) { s_otReg[i].n = n; return; }
    if (s_otRegN < 8) { s_otReg[s_otRegN].base = b; s_otReg[s_otRegN].n = n; s_otRegN++; return; }
    s_otReg[s_otRegRR].base = b; s_otReg[s_otRegRR].n = n; s_otRegRR = (s_otRegRR + 1) & 7;
}
static const D2OtReg *d2_ot_find(uintptr_t p)
{
    for (int i = 0; i < s_otRegN; i++)
        if (p >= s_otReg[i].base && p < s_otReg[i].base + (uintptr_t)s_otReg[i].n * sizeof(OT_TAG)) return &s_otReg[i];
    return 0;
}
// desenha um no da lista de comandos (um pacote, ou varios em sequencia dentro do mesmo tag)
static inline void d2_ot_node(P_TAG *base)
{
        const unsigned words = getlen(base);
        if (__builtin_expect(words != 0, 1)) {
            uintptr_t cur = (uintptr_t)base, end = cur + (words + P_LEN) * sizeof(u_int);
            while (cur < end) {
                P_TAG *tag = (P_TAG*)cur;
                int n = d2_native_dispatch(tag);
                uintptr_t next = cur + (n + P_LEN) * sizeof(u_int);
                if (__builtin_expect(n <= 0 || next <= cur || next > end, 0)) {
                    cur = end;
                    break;
                }
                cur = next;
            }
        }
}

extern "C" void d2_audio_mix(void);
extern "C" void d2_native_draw_ot(u_long *ot)
{
    if (!ot) return;
    bool t3d_drawn = false;
    bool t3d_ordered = d2_t3d_begin_ordered_world() != 0;
    uintptr_t uot_top = (uintptr_t)ot;
    // Extended PsyCross OT entries contain both pointer and length (8 bytes
    // on N64). u_long is only 4 bytes and misidentifies every depth bucket.
    static_assert(sizeof(OT_TAG) == P_LEN * sizeof(u_int), "Unexpected OT tag layout");
    uintptr_t uot_base = uot_top - (0x1080 - 1) * sizeof(OT_TAG);
    const D2OtReg *rg = d2_ot_find((uintptr_t)ot);
    if (rg) {
        const int top = (int)(((uintptr_t)ot - rg->base) / sizeof(OT_TAG));
        const OT_TAG *slots = (const OT_TAG*)rg->base;
        int nextOrd = t3d_ordered ? d2_t3d_order_peek_bucket() : -1;
        for (int sl = top; sl >= 0; --sl) {
            if (!(sl & 63)) d2_audio_mix();      // o quadro leva ~90 ms: sem isso o buffer de audio esvazia no meio do desenho
            {
                const int bucket = 0x107F - (top - sl);
                if (t3d_ordered) {
                    if (nextOrd >= bucket && bucket >= 0 && bucket <= 0x107F) { d2_t3d_draw_world_until(bucket); nextOrd = d2_t3d_order_peek_bucket(); }
                } else if (!t3d_drawn && bucket >= 0 && bucket < 0x1070) {
                    t3d_drawn = true;
                    d2_t3d_draw_queued_buildings();
                }
            }
            for (P_TAG *pk = (P_TAG*)slots[sl].addr; pk; pk = (P_TAG*)nextPrim(pk)) d2_ot_node(pk);
        }
    } else {
    // cadeia avulsa (ex.: texto de debug do FntFlush): lista de pacotes terminada em 0
    P_TAG *base = (P_TAG*)ot;
    unsigned nodes = 0;
    for (;;) {
        if (t3d_ordered) {
            uintptr_t ubase=(uintptr_t)base;
            if(ubase>=uot_base && ubase<=uot_top && (ubase-uot_base)%sizeof(OT_TAG)==0)
                d2_t3d_draw_world_until((int)((ubase-uot_base)/sizeof(OT_TAG)));
        } else if (!t3d_drawn) {
            uintptr_t ubase = (uintptr_t)base;
            if (ubase >= uot_base && ubase <= uot_top) {
                int bucket = (int)((ubase - uot_base) / sizeof(OT_TAG));
                if (bucket < 0x1070) {
                    t3d_drawn = true;
                    d2_t3d_draw_queued_buildings();
                }
            }
        }
        if (__builtin_expect(++nodes > 65536, 0)) { debugf("[native-ot] ciclo ou OT invalida\n"); break; }
        d2_ot_node(base);
        if (isendprim(base)) break;
        P_TAG *next = (P_TAG*)nextPrim(base);
        if (__builtin_expect(!next || next == base, 0)) break;
        base = next;
    }
    }
    if(t3d_ordered) {
        d2_t3d_end_ordered_world();
    } else if (!t3d_drawn) {
        d2_t3d_draw_queued_buildings();
    }
    if (s_d2gpuOvl) rspq_write(s_d2gpuOvl, D2G_CMD_DRAW_END);
}
int d2_direct_poly(P_TAG *tag)
{
    const int code = tag->code;
    const bool tex = (code & 0x04) != 0, quad = (code & 0x08) != 0, gour = (code & 0x10) != 0;
    const bool semi = (code & 0x02) != 0, shadeOn = (code & 0x01) == 0;
    const int len = gour ? (tex ? (quad ? 12 : 9) : (quad ? 8 : 6)) : (tex ? (quad ? 9 : 7) : (quad ? 5 : 4));
    

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
        if (!quad && !gour) s_ft3Total++;
        else if (quad && !gour) s_directFt4Total++;
        activeDrawEnv.tpage = tpage;
        // POLY_FT3 nulo: so troca a tpage (truque oficial da SCE)
        if (!quad && !gour && xs[0] == -1 && ys[0] == -1 && xs[1] == -1 && ys[1] == -1 && xs[2] == -1 && ys[2] == -1) return len;
    }
    if (!activeDrawEnv.dfe || !s_scene) return len;             // fora da tela: o caminho antigo tambem descarta
    if (!ensure_slots()) return len;


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
    if (tex && quad && !gour && bm == BM_NONE && texOk)
    {
        const uint32_t col = shadeOn ? ((uint32_t)s_shadeLut[cr[0]] << 16) | ((uint32_t)s_shadeLut[cg[0]] << 8) | s_shadeLut[cb[0]]
                                     : ((uint32_t)s_shadeLut[128] << 16) | ((uint32_t)s_shadeLut[128] << 8) | s_shadeLut[128];
        if (rsp_ft4(xs, ys, us, vs, tpage, clut, col, ofx, ofy)) return len;
    }
    if (tex && quad && !gour && bm == BM_SUBTRACT && texOk)
    {
        uint32_t rg0;
        calc_primitive_colors(&rg0, 1, shadeOn ? cr : NULL, shadeOn ? cg : NULL, shadeOn ? cb : NULL, bm, true);
        if (rsp_ft4(xs, ys, us, vs, tpage, clut, rg0 >> 8, ofx, ofy, (int)(rg0 & 255))) return len;
    }
    if (tex && !quad && gour && bm == BM_NONE && texOk)
    {
        uint32_t col[3];
        for (int i = 0; i < 3; i++)
            col[i] = shadeOn ? ((uint32_t)s_shadeLut[cr[i]] << 16) | ((uint32_t)s_shadeLut[cg[i]] << 8) | s_shadeLut[cb[i]]
                             : ((uint32_t)s_shadeLut[128] << 16) | ((uint32_t)s_shadeLut[128] << 8) | s_shadeLut[128];
        if (rsp_gt3(xs, ys, us, vs, tpage, clut, col, ofx, ofy)) return len;
    }
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
    if (0) {}
    else
    {
        tri_core(&pv[0], &pv[1], &pv[2], tpage, clut);
        if (quad) tri_core(&pv[1], &pv[3], &pv[2], tpage, clut);
    }
    return len;
}

} // extern "C"
