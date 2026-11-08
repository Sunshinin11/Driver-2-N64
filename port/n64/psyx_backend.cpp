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
#include "d2_vram.h"
#include <libdragon.h>
#include <wav64.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

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

// --- VRAM de textura (ver d2_vram.h): metade esquerda no formato do PS1, metade direita ja no formato do RDP
#define VRAM_W 1024
#define VRAM_H 512
unsigned short d2_vramL[512 * 512] __attribute__((aligned(64)));
unsigned short d2_vramR[512 * 512] __attribute__((aligned(64)));

unsigned short d2_clut_map[512 * 64];
unsigned short d2_clut_pool[D2_CLUT_MAX][16];
unsigned short d2_clut_cell[D2_CLUT_MAX];
int d2_clut_n, d2_clut_bad;
extern "C" void d2_clut_pos(unsigned int h, int *x, int *y) { d2_clut_xy(h, x, y); }
unsigned short d2_clut_h(int x, int y)
{
    const unsigned int cell = (unsigned int)(((y & 511) << 6) | ((x & 1023) >> 4));
    if (d2_clut_map[cell]) return d2_clut_map[cell];
    if (d2_clut_n >= D2_CLUT_MAX) { d2_clut_bad++; return (unsigned short)D2_CLUT_MAX; }
    const int cx = (int)(cell & 63) * 16, cy = (int)(cell >> 6);
    for (int i = 0; i < 16; i++) d2_clut_pool[d2_clut_n][i] = d2_vram_rd(cx + i, cy);   // o que ja estava na VRAM antes de virar paleta
    d2_clut_cell[d2_clut_n] = (unsigned short)cell;
    d2_clut_map[cell] = (unsigned short)(++d2_clut_n);
    return d2_clut_map[cell];
}

// o RDP le a metade direita por DMA: depois de escrever, leva as linhas de cache para a RDRAM
static void d2_vram_r_flush(int x, int y, int w, int h)
{
    if (x + w <= 512) return;
    const int x0 = x < 512 ? 512 : x;
    for (int i = 0; i < h; i++)
        data_cache_hit_writeback(&d2_vramR[(((y + i) & 511) << 9) + (x0 - 512)], (size_t)(x + w - x0) * 2);
}

void GR_ClearVRAM(int x, int y, int w, int h, unsigned char r, unsigned char g, unsigned char b)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > VRAM_W) w = VRAM_W - x;
    if (y + h > VRAM_H) h = VRAM_H - y;
    if (w <= 0 || h <= 0) return;
    d2_vram_touch(x, y, w, h);
    if (x + w > 512) d2_vram_r_barrier();
    const unsigned short v = r | (g << 5) | (b << 11);
    for (int i = 0; i < h; i++)
        for (int j = 0; j < w; j++) d2_vram_wr(x + j, y + i, v);
    d2_vram_r_flush(x, y, w, h);
}
void GR_CopyVRAM(unsigned short *src, int x, int y, int w, int h, int dst_x, int dst_y)
{
    const int stride = w;
    const bool ext = src != 0;
    // recorta o destino em VRAM; ajusta a origem junto
    if (dst_x < 0) { x -= dst_x; w += dst_x; dst_x = 0; }
    if (dst_y < 0) { y -= dst_y; h += dst_y; dst_y = 0; }
    if (dst_x + w > VRAM_W) w = VRAM_W - dst_x;
    if (dst_y + h > VRAM_H) h = VRAM_H - dst_y;
    if (w <= 0 || h <= 0) return;
    d2_vram_touch(dst_x, dst_y, w, h);
    if (dst_x + w > 512) d2_vram_r_barrier();
    if (ext) {
        // dados vindos da memoria do jogo estao no formato do PS1 (bytes little-endian): troca os bytes de cada palavra de 16 bits
        const unsigned short *s = src + x + y * stride;
        for (int i = 0; i < h; i++, s += stride)
            for (int j = 0; j < w; j++) { unsigned short v = s[j]; d2_vram_wr(dst_x + j, dst_y + i, (unsigned short)((v >> 8) | (v << 8))); }
    } else {
        // VRAM -> VRAM (MoveImage): uma linha por vez, via buffer (as regioes podem se sobrepor)
        unsigned short tmp[VRAM_W];
        for (int i = 0; i < h; i++) {
            for (int j = 0; j < w; j++) tmp[j] = d2_vram_rd(x + j, y + i);
            for (int j = 0; j < w; j++) d2_vram_wr(dst_x + j, dst_y + i, tmp[j]);
        }
    }
    d2_vram_r_flush(dst_x, dst_y, w, h);
}
// Upload de CORES (paletas e imagens de 16 bits): o jogo manda palavras do PS1, a VRAM guarda direto o RGBA5551 do RDP.
// Retorna 0 so se o retangulo sai da VRAM.
int GR_LoadColor(const unsigned short *src, int dst_x, int dst_y, int w, int h)
{
    if (dst_x < 0 || dst_y < 0 || dst_x + w > VRAM_W || dst_y + h > VRAM_H) return 0;
    d2_vram_touch(dst_x, dst_y, w, h);
    for (int i = 0; i < h; i++, src += w)
    {
        const int y = dst_y + i;
        for (int j = 0; j < w; j++)
        {
            const int x = dst_x + j;
            if (j == 0 || (x & 15) == 0) d2_clut_h(x, y);
            d2_vram_wr(x, y, (unsigned short)((src[j] >> 8) | (src[j] << 8)));   // palavra do PS1 (bytes little-endian)
        }
    }
    return 1;
}
void GR_ReadVRAM(unsigned short *dst, int x, int y, int dst_w, int dst_h)
{
    if (x < 0 || y < 0 || x >= VRAM_W || y >= VRAM_H) return;
    int w = dst_w; if (x + w > VRAM_W) w = VRAM_W - x;
    for (int i = 0; i < dst_h && y + i < VRAM_H; i++, dst += dst_w)
        for (int j = 0; j < w; j++) { unsigned short v = d2_vram_rd(x + j, y + i); dst[j] = (unsigned short)((v >> 8) | (v << 8)); }
}


// --- audio (SPU): vozes nativas (wav64 VADPCM) sobre o mixer RSP do libdragon; sem SPU RAM nem ADPCM PS1
#define D2_SPU_RAM_SIZE (128 * 1024)
#define D2_SPU_VOICES 24
#define D2_SFX_TAG 0x40000000u
#define D2_MUS_TAG 0x20000000u
static u_int s_spuTransfer, s_spuTop = 0x1000;
static bool s_spuInited, s_spuMute;

struct D2SpuVoice {
    u_int addr, loopAddr;
    short volL, volR;
    u_short pitch;
};
static D2SpuVoice s_spuVoice[D2_SPU_VOICES];


struct D2NativeSfx {
    wav64_t *wav;
    u_int orig_sr;
};

static D2NativeSfx s_native_sfx[72][35];
static D2NativeSfx s_native_mus[8][64];
static int s_current_song = 0;

// endereco de voz -> wav64 nativo (SFX: lump/idx; musica: song/idx; os 16 bits baixos sao o offset de amostra do XM, ignorado)
static inline wav64_t *d2_native_wave(u_int addr, u_int *orig_sr)
{
    if (addr & D2_SFX_TAG) {
        u_int lump = (addr >> 8) & 0xFF, idx = addr & 0xFF;
        if (lump < 72 && idx < 35) { *orig_sr = s_native_sfx[lump][idx].orig_sr; return s_native_sfx[lump][idx].wav; }
    } else if (addr & D2_MUS_TAG) {
        u_int song = (addr >> 24) & 0xF, idx = (addr >> 16) & 0xFF;
        if (song < 8 && idx < 64) { *orig_sr = s_native_mus[song][idx].orig_sr; return s_native_mus[song][idx].wav; }
    }
    *orig_sr = 0;
    return NULL;
}

extern "C" void d2_vadpcm_set_current_song(int song)
{
    s_current_song = song;
}
static int s_current_lump = -1;

extern "C" void d2_vadpcm_set_current_lump(int lump)
{
    s_current_lump = lump;
}

extern "C" void d2_vadpcm_reset(void)
{
    if (s_spuInited) {
        for (int i = 0; i < 16; i++) {
            mixer_ch_stop(i);
        }
    }
    for (int l = 0; l < 72; l++) {
        for (int s = 0; s < 35; s++) {
            if (s_native_sfx[l][s].wav) {
                wav64_close(s_native_sfx[l][s].wav);
                s_native_sfx[l][s].wav = NULL;
                s_native_sfx[l][s].orig_sr = 0;
            }
        }
    }
    for (int m = 0; m < 8; m++) {
        for (int s = 0; s < 64; s++) {
            if (s_native_mus[m][s].wav) {
                wav64_close(s_native_mus[m][s].wav);
                s_native_mus[m][s].wav = NULL;
                s_native_mus[m][s].orig_sr = 0;
            }
        }
    }
    s_current_lump = -1;
}

extern "C" int d2_vadpcm_process_music(int num_samples, void *samps_ptr)
{
    struct SampleDataEntry { u_int address, length, loop, samplerate; };
    SampleDataEntry *samps = (SampleDataEntry *)samps_ptr;
    int song = s_current_song;
    if (song < 0 || song >= 8) return 0;
    // troca de faixa: libera as amostras da faixa anterior
    for (int m = 0; m < 8; m++) {
        if (m == song) continue;
        for (int i = 0; i < 64; i++) if (s_native_mus[m][i].wav) {
            wav64_close(s_native_mus[m][i].wav); s_native_mus[m][i].wav = NULL; s_native_mus[m][i].orig_sr = 0;
        }
    }
    wav64_loadparms_t parms;
    memset(&parms, 0, sizeof(parms));
    parms.streaming_mode = WAV64_STREAMING_NONE;
    int loaded = 0;
    for (int i = 0; i < num_samples && i < 64; i++) {
        if (!s_native_mus[song][i].wav) {
            char fn[64];
            snprintf(fn, sizeof(fn), "rom:/MUS64/M%02d_S%03d.wav64", song, i);
            wav64_t *w = wav64_load(fn, &parms);
            if (!w) { debugf("[VADPCM] WARN: failed to load %s\n", fn); continue; }
            s_native_mus[song][i].wav = w;
            s_native_mus[song][i].orig_sr = samps[i].samplerate ? samps[i].samplerate : 22050;
        }
        samps[i].address = D2_MUS_TAG | ((u_int)song << 24) | ((u_int)i << 16);
        loaded++;
    }
    debugf("[VADPCM] music=%d loaded=%d/%d\n", song, loaded, num_samples);
    return loaded > 0;
}

extern "C" int d2_vadpcm_process_lump(int dbank, int start_idx, int num_samples, void *samps_ptr)
{
    if (s_current_lump < 0 || s_current_lump >= 72 || dbank == 0) return 0;

    struct SampleDataEntry {
        u_int address, length, loop, samplerate;
    };
    SampleDataEntry *samps = (SampleDataEntry *)samps_ptr;

    wav64_loadparms_t parms;
    memset(&parms, 0, sizeof(parms));
    parms.streaming_mode = WAV64_STREAMING_NONE;

    int loaded_count = 0;
    for (int i = 0; i < num_samples && i < 35; i++) {
        char fn[64];
        snprintf(fn, sizeof(fn), "rom:/SFX64/L%02d_S%02d.wav64", s_current_lump, i);

        if (!s_native_sfx[s_current_lump][i].wav) {
            wav64_t *w = wav64_load(fn, &parms);
            if (!w) {
                debugf("[VADPCM] WARN: failed to load %s\n", fn);
                continue;
            }
            s_native_sfx[s_current_lump][i].wav = w;
            s_native_sfx[s_current_lump][i].orig_sr = samps[i].samplerate ? samps[i].samplerate : 22050;
        }

        samps[i].address = D2_SFX_TAG | ((u_int)s_current_lump << 8) | (u_int)i;
        loaded_count++;
    }

    debugf("[VADPCM] lump=%d bank=%d loaded=%d/%d\n", s_current_lump, dbank, loaded_count, num_samples);
    s_current_lump = -1;
    return (loaded_count > 0) ? 1 : 0;
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
    if (v->addr & (D2_SFX_TAG | D2_MUS_TAG)) {
        u_int orig_sr;
        wav64_t *wav = d2_native_wave(v->addr, &orig_sr);
        if (wav && orig_sr > 0) {
            float freq = 44100.0f * ((float)v->pitch / 4096.0f) * ((float)wav->wave.frequency / (float)orig_sr);
            if (freq < 100.0f) freq = 100.0f;
            if (freq > D2_SPU_MAX_FREQ) freq = D2_SPU_MAX_FREQ;
            mixer_ch_set_freq(ch, freq);
            return;
        }
    }
    mixer_ch_set_freq(ch, d2_spu_calc_freq(v->pitch));
}

static void d2_spu_shape(D2SpuVoice *v, int ch)
{
    mixer_ch_stop(ch);
    if (v->addr & (D2_SFX_TAG | D2_MUS_TAG)) {
        u_int osr;
        wav64_t *wav = d2_native_wave(v->addr, &osr);
        if (wav) {
            mixer_ch_play(ch, &wav->wave);
            d2_spu_apply_voice(v, ch);
            return;
        }
    }
}

int PsyX_SPUAL_InitSound()
{
    if (s_spuInited) return 1;
    audio_init(32000, 4); mixer_init(D2_SPU_VOICES);
    for (int i = 0; i < D2_SPU_VOICES; i++)
    {
        mixer_ch_set_limits(i, 16, D2_SPU_MAX_FREQ, 0);
        D2SpuVoice *v = &s_spuVoice[i]; memset(v, 0, sizeof *v);
        v->pitch = 4096;     }
    s_spuInited = true; return 1;
}
void PsyX_SPUAL_ShutdownSound() {
    if (s_spuInited) {
        d2_vadpcm_reset();
        mixer_close(); audio_close(); s_spuInited = false;
    }
}
int PsyX_SPUAL_Alloc(int size) { int a = (s_spuTop + 15) & ~15; if (a + size > D2_SPU_RAM_SIZE) return -1; s_spuTop = a + size; return a; }
int PsyX_SPUAL_InitAlloc(int, char *) { s_spuTop = 0x1000; return 0; }
void PsyX_SPUAL_Free(u_int) {}
u_int PsyX_SPUAL_Write(u_char *, u_int size) { return size; }
u_int PsyX_SPUAL_Read(u_char *, u_int) { return 0; }
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
    d2_sys_pump_vbl_cb();
    if (s_spuInited) mixer_try_play();
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

