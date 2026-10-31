// Backend N64 da camada PsyX (substitui o OpenGL/SDL do PC). Etapa E1b: VRAM do PS1 em RDRAM, tempo, controle e
// o "contrato" de render (GR_*) ainda vazio - o desenho com o RDP entra na etapa E4.
#include "psx/types.h"
#include "psx/libgte.h"
#include "psx/libgpu.h"
#include "psx/libspu.h"
#include "PsyX/PsyX_public.h"
#include "PsyX/PsyX_globals.h"
#include "PsyX/PsyX_render.h"
#include "../redriver2/src_rebuild/PsyCross/src/audio/PsyX_SPUAL.h"
#include <libdragon.h>
#include <string.h>
#include <stdlib.h>

extern "C" {
// --- ponte libdragon (sys_n64.c)
typedef struct { unsigned short btn; signed char sx, sy; } d2_pad_raw_t;
void d2_sys_init(void);
int  d2_sys_vbl_count(void);
void d2_sys_set_vbl_cb(void (*f)(void));
void d2_sys_pump_vbl_cb(void);
void d2_sys_wait_vbl_until(int target);
int  d2_pad_poll(int port, d2_pad_raw_t *o);

// --- estado de sistema (era PsyX_main.cpp)
unsigned int g_swapTime;
int g_vmode = 0;                 // 0 = NTSC
int g_activeKeyboardControllers;
int g_windowWidth = 320, g_windowHeight = 240;
int g_dbg_wireframeMode, g_dbg_texturelessMode;

void d2_vram_touch(int x, int y, int w, int h);
int PsyX_Sys_GetVBlankCount() { return d2_sys_vbl_count(); }
int PsyX_Sys_SetVMode(int mode) { int old = g_vmode; g_vmode = mode; return old; }

extern "C" void d2_sys_present(const char *who);
void PsyX_WaitForTimestep(int count)
{
    d2_sys_present("vsync");
    static int last;
    int now = d2_sys_vbl_count();
    if (last == 0 || now - last > 8) last = now;      // primeira chamada ou atraso grande: nao tenta "recuperar"
    d2_sys_wait_vbl_until(last + count);
    last = d2_sys_vbl_count() > last + count ? d2_sys_vbl_count() : last + count;
}
void PsyX_EnableSwapInterval(int enable) { (void)enable; }
void PsyX_SetSwapInterval(int interval) { (void)interval; }
void PsyX_GetScreenSize(int *w, int *h) { *w = 320; *h = 240; }
void PsyX_GetPSXWidescreenMappedViewport(struct _RECT16 *r) { r->x = 0; r->y = 0; r->w = 320; r->h = 240; }
void PsyX_SetCursorPosition(int x, int y) { (void)x; (void)y; }
void PsyX_SetCursorRelative(int enable) { (void)enable; }

// --- VRAM do PS1: 1024x512 de 16 bits (1 MB) em RDRAM
#define VRAM_W 1024
#define VRAM_H 512
unsigned short vram[VRAM_W * VRAM_H];

void GR_ClearVRAM(int x, int y, int w, int h, unsigned char r, unsigned char g, unsigned char b)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > VRAM_W) w = VRAM_W - x;
    if (y + h > VRAM_H) h = VRAM_H - y;
    if (w <= 0 || h <= 0) return;
    d2_vram_touch(x, y, w, h);
    unsigned short *dst = vram + x + y * VRAM_W;
    for (int i = 0; i < h; i++, dst += VRAM_W)
        for (int j = 0; j < w; j++) dst[j] = r | (g << 5) | (b << 11);
}
void GR_CopyVRAM(unsigned short *src, int x, int y, int w, int h, int dst_x, int dst_y)
{
    int stride = w;
    bool ext = src != 0;
    if (!src) { src = vram; stride = VRAM_W; }
    // recorta o destino em VRAM; ajusta a origem junto
    if (dst_x < 0) { x -= dst_x; w += dst_x; dst_x = 0; }
    if (dst_y < 0) { y -= dst_y; h += dst_y; dst_y = 0; }
    if (dst_x + w > VRAM_W) w = VRAM_W - dst_x;
    if (dst_y + h > VRAM_H) h = VRAM_H - dst_y;
    if (w <= 0 || h <= 0) return;
    d2_vram_touch(dst_x, dst_y, w, h);
    src += x + y * stride;
    unsigned short *dst = vram + dst_x + dst_y * VRAM_W;
    if (ext) {
        // dados vindos da memoria do jogo estao no formato do PS1 (bytes little-endian): troca os bytes de cada palavra de 16 bits
        for (int i = 0; i < h; i++, dst += VRAM_W, src += stride)
            for (int j = 0; j < w; j++) { unsigned short v = src[j]; dst[j] = (unsigned short)((v >> 8) | (v << 8)); }
    } else
        for (int i = 0; i < h; i++, dst += VRAM_W, src += stride) memmove(dst, src, w * sizeof(short));
}
void GR_ReadVRAM(unsigned short *dst, int x, int y, int dst_w, int dst_h)
{
    if (x < 0 || y < 0 || x >= VRAM_W || y >= VRAM_H) return;
    int w = dst_w; if (x + w > VRAM_W) w = VRAM_W - x;
    unsigned short *src = vram + x + VRAM_W * y;
    for (int i = 0; i < dst_h && y + i < VRAM_H; i++, dst += dst_w, src += VRAM_W)
        for (int j = 0; j < w; j++) { unsigned short v = src[j]; dst[j] = (unsigned short)((v >> 8) | (v << 8)); }
}
void d2_vram_touch(int x, int y, int w, int h);

// --- audio (SPU) : memoria e vozes ADPCM do PS1 sobre o mixer RSP do libdragon
#define D2_SPU_RAM_SIZE (512 * 1024)
#define D2_SPU_VOICES 24
static u_char s_spuRam[D2_SPU_RAM_SIZE] __attribute__((aligned(16)));
static u_int s_spuTransfer, s_spuTop = 0x1000;
static bool s_spuInited, s_spuMute;

struct D2SpuVoice {
    waveform_t wave;
    u_int addr, loopAddr;
    short volL, volR;
    u_short pitch;
};
struct D2SpuDecode {
    u_int start, block, loopBlock;
    int next, s1, s2, nib;
    short pcm[28];
};
static D2SpuVoice s_spuVoice[D2_SPU_VOICES];

static inline void d2_spu_decode_28(const u_char *p, short * __restrict out, int &s1, int &s2)
{
    static const int f0[5] = { 0, 60, 115, 98, 122 };
    static const int f1[5] = { 0, 0, -52, -55, -60 };
    int shift = p[0] & 15, filter = p[0] >> 4;
    if (shift > 12) shift = 12;
    if (filter > 4) filter = 0;
    const int k0 = f0[filter], k1 = f1[filter];
    const int sh = 12 - shift;
    int prev1 = s1, prev2 = s2;
    for (int b = 2; b < 16; b++)
    {
        int byte = p[b];
        int n0 = byte & 15; if (n0 & 8) n0 -= 16;
        int v0 = (n0 << sh) + ((prev1 * k0 + prev2 * k1 + 32) >> 6);
        if (v0 > 32767) v0 = 32767; else if (v0 < -32768) v0 = -32768;
        *out++ = (short)v0;

        int n1 = byte >> 4; if (n1 & 8) n1 -= 16;
        int v1 = (n1 << sh) + ((v0 * k0 + prev1 * k1 + 32) >> 6);
        if (v1 > 32767) v1 = 32767; else if (v1 < -32768) v1 = -32768;
        *out++ = (short)v1;

        prev2 = v0; prev1 = v1;
    }
    s1 = prev1; s2 = prev2;
}

static void d2_spu_decode_block(D2SpuDecode *d)
{
    if (d->block + 16 > D2_SPU_RAM_SIZE) { memset(d->pcm, 0, sizeof d->pcm); return; }
    d2_spu_decode_28(s_spuRam + d->block, d->pcm, d->s1, d->s2);
}

static void d2_spu_seek(D2SpuDecode *d, int pos)
{
    if (pos < 0) pos = 0;
    u_int targetBlk = (u_int)(pos / 28);
    if (pos == 0) {
        d->s1 = d->s2 = 0;
    } else if (targetBlk != d->loopBlock) {
        // Aquecimento O(1) do filtro IIR no bloco imediatamente anterior
        d->s1 = d->s2 = 0;
        if (targetBlk > 0) {
            u_int warmAddr = d->start + (targetBlk - 1) * 16;
            if (warmAddr + 16 <= D2_SPU_RAM_SIZE)
                d2_spu_decode_28(s_spuRam + warmAddr, d->pcm, d->s1, d->s2);
        }
    }
    // Se targetBlk == d->loopBlock, preserva s1/s2 do fim da iteracao anterior (comportamento real do SPU do PS1)
    d->block = d->start + targetBlk * 16;
    d->next = (int)(targetBlk * 28);
    d2_spu_decode_block(d);
    d->nib = pos - d->next;
}

static void d2_spu_read(void *ctx, samplebuffer_t *sbuf, int wpos, int wlen, bool seeking)
{
    D2SpuDecode *d = (D2SpuDecode *)ctx;
    if (seeking || wpos != d->next + d->nib) d2_spu_seek(d, wpos);
    short *out = (short *)samplebuffer_append(sbuf, wlen);
    int rem = wlen;
    while (rem > 0)
    {
        if (d->nib >= 28 && rem >= 28 && d->block + 32 <= D2_SPU_RAM_SIZE)
        {
            d->block += 16; d->next += 28;
            d2_spu_decode_28(s_spuRam + d->block, out, d->s1, d->s2);
            out += 28; rem -= 28; d->nib = 28;
            continue;
        }
        if (d->nib >= 28) { d->block += 16; d->next += 28; d->nib = 0; d2_spu_decode_block(d); }
        int avail = 28 - d->nib;
        int take = rem < avail ? rem : avail;
        memcpy(out, d->pcm + d->nib, (size_t)take * sizeof(short));
        out += take; d->nib += take; rem -= take;
    }
}

static constexpr float D2_SPU_MASTER_VOL = 0.35f;
static constexpr float D2_SPU_MAX_FREQ   = 44100.0f;

static float d2_spu_calc_freq(u_short pitch)
{
    float freq = 44100.0f * (float)pitch / 4096.0f;
    if (freq < 100.0f) freq = 100.0f;
    if (freq > D2_SPU_MAX_FREQ) freq = D2_SPU_MAX_FREQ;
    return freq;
}

static void d2_spu_apply_voice(const D2SpuVoice *v, int ch)
{
    if (!s_spuInited || !mixer_ch_playing(ch)) return;
    float vl = s_spuMute ? 0.0f : ((float)v->volL / 16384.0f) * D2_SPU_MASTER_VOL;
    float vr = s_spuMute ? 0.0f : ((float)v->volR / 16384.0f) * D2_SPU_MASTER_VOL;
    if (vl < -1.0f) vl = -1.0f; else if (vl > 1.0f) vl = 1.0f;
    if (vr < -1.0f) vr = -1.0f; else if (vr > 1.0f) vr = 1.0f;
    mixer_ch_set_vol(ch, vl, vr);
    mixer_ch_set_freq(ch, d2_spu_calc_freq(v->pitch));
}

static void d2_spu_shape(D2SpuVoice *v, int ch)
{
    mixer_ch_stop(ch);
    int blocks = 0, loopBlock = -1; bool repeat = false;
    for (u_int a = v->addr; a + 16 <= D2_SPU_RAM_SIZE && blocks < 8192; a += 16, blocks++)
    {
        int flags = s_spuRam[a + 1];
        if (flags & 4) loopBlock = blocks;
        if (flags & 1) { repeat = (flags & 2) != 0; blocks++; break; }
    }
    if (blocks <= 0) blocks = 1;
    if (loopBlock < 0 && v->loopAddr >= v->addr) {
        int lb = (int)((v->loopAddr - v->addr) >> 4);
        if (lb < blocks) loopBlock = lb;
    }
    int loopStart = (loopBlock >= 0 && loopBlock < blocks) ? loopBlock : 0;
    D2SpuDecode *d = (D2SpuDecode *)v->wave.ctx;
    memset(d, 0, sizeof *d);
    d->start = v->addr;
    d->loopBlock = (u_int)loopStart;
    d2_spu_seek(d, 0);
    v->wave.__uuid = 0;
    v->wave.frequency = d2_spu_calc_freq(v->pitch);
    v->wave.len = blocks * 28;
    v->wave.loop_len = repeat ? (blocks - loopStart) * 28 : 0;
    mixer_ch_play(ch, &v->wave);
    d2_spu_apply_voice(v, ch);
}

int PsyX_SPUAL_InitSound()
{
    if (s_spuInited) return 1;
    audio_init(32000, 4); mixer_init(D2_SPU_VOICES);
    for (int i = 0; i < D2_SPU_VOICES; i++)
    {
        mixer_ch_set_limits(i, 16, D2_SPU_MAX_FREQ, 0);
        D2SpuVoice *v = &s_spuVoice[i]; memset(v, 0, sizeof *v);
        v->pitch = 4096; v->wave.name = "psx-adpcm"; v->wave.bits = 16; v->wave.channels = 1;
        v->wave.frequency = 32000.0f; v->wave.read = d2_spu_read; v->wave.append_units = 28;
        v->wave.ctx = malloc(sizeof(D2SpuDecode));
    }
    s_spuInited = true; return 1;
}
void PsyX_SPUAL_ShutdownSound() { if (s_spuInited) { mixer_close(); audio_close(); s_spuInited = false; } }
int PsyX_SPUAL_Alloc(int size) { int a = (s_spuTop + 15) & ~15; if (a + size > D2_SPU_RAM_SIZE) return -1; s_spuTop = a + size; return a; }
int PsyX_SPUAL_InitAlloc(int, char *) { s_spuTop = 0x1000; return 0; }
void PsyX_SPUAL_Free(u_int) {}
u_int PsyX_SPUAL_Write(u_char *src, u_int size) { if (s_spuTransfer >= D2_SPU_RAM_SIZE) return 0; if (size > D2_SPU_RAM_SIZE - s_spuTransfer) size = D2_SPU_RAM_SIZE - s_spuTransfer; memcpy(s_spuRam + s_spuTransfer, src, size); s_spuTransfer += size; return size; }
u_int PsyX_SPUAL_Read(u_char *dst, u_int size) { if (s_spuTransfer >= D2_SPU_RAM_SIZE) return 0; if (size > D2_SPU_RAM_SIZE - s_spuTransfer) size = D2_SPU_RAM_SIZE - s_spuTransfer; memcpy(dst, s_spuRam + s_spuTransfer, size); s_spuTransfer += size; return size; }
u_int PsyX_SPUAL_SetTransferStartAddr(u_int addr) { s_spuTransfer = addr; return addr; }
void PsyX_SPUAL_GetVoiceVolume(int n, short *l, short *r) { if (n < 0 || n >= D2_SPU_VOICES) { *l = *r = 0; } else { *l = s_spuVoice[n].volL; *r = s_spuVoice[n].volR; } }
void PsyX_SPUAL_GetVoicePitch(int n, u_short *p) { *p = n >= 0 && n < D2_SPU_VOICES ? s_spuVoice[n].pitch : 0; }
void PsyX_SPUAL_SetVoiceAttr(SpuVoiceAttr *a)
{
    for (int n = 0; n < D2_SPU_VOICES; n++) if (a->voice & (1u << n))
    {
        D2SpuVoice *v = &s_spuVoice[n];
        if (a->mask & SPU_VOICE_VOLL) v->volL = a->volume.left;
        if (a->mask & SPU_VOICE_VOLR) v->volR = a->volume.right;
        if (a->mask & SPU_VOICE_PITCH) v->pitch = a->pitch;
        if (a->mask & SPU_VOICE_WDSA) v->addr = a->addr;
        if (a->mask & SPU_VOICE_LSAX) v->loopAddr = a->loop_addr;
        if (s_spuInited) d2_spu_apply_voice(v, n);
    }
}
void PsyX_SPUAL_SetKey(int on, u_int bits) { if (!s_spuInited) return; for (int n = 0; n < D2_SPU_VOICES; n++) if (bits & (1u << n)) { if (on) d2_spu_shape(&s_spuVoice[n], n); else mixer_ch_stop(n); } }
int PsyX_SPUAL_GetKeyStatus(u_int bits) { int r = 0; if (s_spuInited) for (int n = 0; n < D2_SPU_VOICES; n++) if ((bits & (1u << n)) && mixer_ch_playing(n)) r |= 1u << n; return r; }
void PsyX_SPUAL_GetAllKeysStatus(char *s) { for (int n = 0; n < D2_SPU_VOICES; n++) s[n] = s_spuInited && mixer_ch_playing(n); }
int PsyX_SPUAL_SetMute(int on) { int old = s_spuMute; s_spuMute = on != 0; if (s_spuInited) { for (int n = 0; n < D2_SPU_VOICES; n++) d2_spu_apply_voice(&s_spuVoice[n], n); } return old; }
void d2_audio_pump() {
#ifdef D2_PERFDBG
    uint32_t t0 = perf_cnt0();
    d2_sys_pump_vbl_cb();
    if (s_spuInited) mixer_try_play();
    g_perf_cur.audio_pump_ticks += perf_cnt0() - t0;
    g_perf_cur.audio_pump_calls++;
#else
    d2_sys_pump_vbl_cb();
    if (s_spuInited) mixer_try_play();
#endif
}
int PsyX_SPUAL_SetReverb(int) { return 0; }
int PsyX_SPUAL_GetReverbState() { return 0; }
u_int PsyX_SPUAL_SetReverbVoice(int, u_int) { return 0; }
u_int PsyX_SPUAL_GetReverbVoice() { return 0; }

// --- controle: converte o N64 para o formato de pad do PS1 (digital 0x41; bits ativos em 0)
static u_char *padbuf[2];
void PsyX_Pad_InitPad(int slot, u_char *padData)
{
    if (slot < 0 || slot > 1) return;
    padbuf[slot] = padData;
    if (padData) { padData[0] = 0xFF; padData[1] = 0; padData[2] = padData[3] = 0xFF; }
}
void PsyX_Pad_Vibrate(int, int, unsigned char *, int) {}
int PsyX_Pad_GetStatus(int, int slot) { d2_pad_raw_t p; return slot == 0 ? 1 : d2_pad_poll(slot, &p); }
void PsyX_Pad_InternalPadUpdates()
{
    for (int s = 0; s < 2; s++)
    {
        if (!padbuf[s]) continue;
        d2_pad_raw_t p;
        if (!d2_pad_poll(s, &p)) { padbuf[s][0] = 0xFF; continue; }
        unsigned short n = p.btn;
        unsigned short ps = 0xFFFF;                       // PS1: 0 = apertado
        #define PRESS(cond, bit) if (cond) ps &= ~(bit)
        const int L = p.sx < -40, R = p.sx > 40, U = p.sy > 40, D = p.sy < -40;
        PRESS(n & 8, 0x0008);                             // START
        PRESS((n & 16) || U, 0x0010); PRESS((n & 128) || R, 0x0020); PRESS((n & 32) || D, 0x0040); PRESS((n & 64) || L, 0x0080);
        PRESS(n & 4, 0x0100);                             // Z -> L2
        PRESS(n & 512, 0x0800);                           // R -> R1
        PRESS(n & 256, 0x0400);                           // L -> L1
        PRESS(n & 1024, 0x1000);                          // C-cima -> Triangulo
        PRESS(n & 2048, 0x2000);                          // C-baixo -> Circulo
        PRESS(n & 1, 0x4000);                             // A -> X (acelera)
        PRESS(n & 2, 0x8000);                             // B -> Quadrado (freio/re)
        PRESS(n & 4096, 0x0200);                          // C-esq -> R2
        PRESS(n & 8192, 0x0001);                          // C-dir -> Select
        padbuf[s][0] = 0x00; padbuf[s][1] = 0x41;
        padbuf[s][2] = (u_char)(ps & 0xFF);               // byte 2: SELECT/START/direcoes
        padbuf[s][3] = (u_char)(ps >> 8);                 // byte 3: L2 R2 L1 R1 e botoes de acao
        #undef PRESS
    }
}
void PsyX_UpdateInput(void) { PsyX_Pad_InternalPadUpdates(); }
} // extern "C"

