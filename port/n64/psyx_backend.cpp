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
#include <string.h>

extern "C" {
// --- ponte libdragon (sys_n64.c)
typedef struct { unsigned short btn; signed char sx, sy; } d2_pad_raw_t;
void d2_sys_init(void);
int  d2_sys_vbl_count(void);
void d2_sys_set_vbl_cb(void (*f)(void));
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

// --- audio (SPU) : mudo por enquanto
int PsyX_SPUAL_InitSound() { return 1; }
void PsyX_SPUAL_ShutdownSound() {}
int PsyX_SPUAL_Alloc(int size) { static int top = 0x1000; int a = top; top += size; return a; }
int PsyX_SPUAL_InitAlloc(int, char *) { return 0; }
void PsyX_SPUAL_Free(u_int) {}
u_int PsyX_SPUAL_Write(u_char *, u_int size) { return size; }
u_int PsyX_SPUAL_Read(u_char *, u_int size) { return size; }
u_int PsyX_SPUAL_SetTransferStartAddr(u_int addr) { return addr; }
void PsyX_SPUAL_GetVoiceVolume(int, short *l, short *r) { *l = *r = 0; }
void PsyX_SPUAL_GetVoicePitch(int, u_short *p) { *p = 0; }
void PsyX_SPUAL_SetVoiceAttr(SpuVoiceAttr *) {}
void PsyX_SPUAL_SetKey(int, u_int) {}
int PsyX_SPUAL_GetKeyStatus(u_int) { return 0; }
void PsyX_SPUAL_GetAllKeysStatus(char *s) { memset(s, 0, 24); }
int PsyX_SPUAL_SetMute(int) { return 0; }
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

