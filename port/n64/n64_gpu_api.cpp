// API GPU usada pelo motor no N64. Mantem os tipos da SDK do PS1, mas executa
// diretamente o backend N64 e o dispatcher nativo de primitivas.
#include "psx/libgte.h"
#include "psx/libgpu.h"
#include "PsyX/PsyX_render.h"
#include "PsyX/PsyX_public.h"

extern int g_GPUDisabledState;
extern DRAWENV activeDrawEnv;
extern void (*drawsync_callback)(void);
extern "C" void d2_native_draw_ot(u_long *p);
extern "C" int d2_native_draw_prim(P_TAG *p);
extern "C" int GR_LoadColor(const unsigned short *src, int dst_x, int dst_y, int w, int h);

int ClearImage(RECT16 *r, u_char red, u_char green, u_char blue)
{
    GR_ClearVRAM(r->x,r->y,r->w,r->h,red,green,blue);
    GR_Clear(r->x,r->y,r->w,r->h,red,green,blue);
    return 0;
}
int ClearImage2(RECT16 *r, u_char red, u_char green, u_char blue)
{
    return ClearImage(r,red,green,blue);
}
int DrawSync(int)
{
    if(drawsync_callback) drawsync_callback();
    return 0;
}
int LoadImage(RECT16 *r, u_long *p)
{
    GR_CopyVRAM((unsigned short*)p,0,0,r->w,r->h,r->x,r->y);
    return 0;
}
extern "C" int d2_LoadColor(void *rect, void *p)
{
    RECT16 *r = (RECT16 *)rect;
    if (!GR_LoadColor((const unsigned short *)p, r->x, r->y, r->w, r->h)) LoadImage(r, (u_long *)p);
    return 0;
}
int LoadImage2(RECT16 *r, u_long *p)
{
    LoadImage(r,p); return 0;
}
int MoveImage(RECT16 *r, int x, int y)
{
    GR_CopyVRAM(NULL,r->x,r->y,r->w,r->h,x,y);
    return 0;
}
void DrawOTag(u_long *p)
{
    if(g_GPUDisabledState) return;
    PsyX_BeginScene();
    d2_native_draw_ot(p);
}
void DrawOTagEnv(u_long *p, DRAWENV *env)
{
    PutDrawEnv(env); DrawOTag(p);
}
void DrawPrim(void *p)
{
    if(g_GPUDisabledState) return;
    PsyX_BeginScene();
    d2_native_draw_prim((P_TAG*)p);
}
