// API GPU usada pelo motor no N64. Mantem os tipos da SDK do PS1, mas executa
// diretamente o backend N64 e o dispatcher nativo de primitivas.
#include "psx/libgte.h"
#include "psx/libgpu.h"
#include "PsyX/PsyX_render.h"
#include "PsyX/PsyX_public.h"
#include "gpu/PsyX_GPU.h"

extern int g_GPUDisabledState;
extern int g_splitIndex;
extern DRAWENV activeDrawEnv;
extern void (*drawsync_callback)(void);
extern "C" void d2_native_draw_ot(u_long *p);
extern "C" int d2_native_draw_prim(P_TAG *p);

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
    GR_UpdateVRAM();
    GR_ReadFramebufferDataToVRAM();
    if(g_splitIndex>0) DrawAllSplits();
    if(drawsync_callback) drawsync_callback();
    return 0;
}
int LoadImage(RECT16 *r, u_long *p)
{
    GR_CopyVRAM((unsigned short*)p,0,0,r->w,r->h,r->x,r->y);
    return 0;
}
int LoadImage2(RECT16 *r, u_long *p)
{
    LoadImage(r,p); GR_UpdateVRAM(); GR_ReadFramebufferDataToVRAM(); return 0;
}
int MoveImage(RECT16 *r, int x, int y)
{
    GR_CopyVRAM(NULL,r->x,r->y,r->w,r->h,x,y);
    return 0;
}
void DrawOTag(u_long *p)
{
    if(g_GPUDisabledState) { ClearSplits(); return; }
    if(PsyX_BeginScene()) ClearSplits();
    d2_native_draw_ot(p);
    DrawAllSplits();
}
void DrawOTagEnv(u_long *p, DRAWENV *env)
{
    PutDrawEnv(env); DrawOTag(p);
}
void DrawPrim(void *p)
{
    if(g_GPUDisabledState) { ClearSplits(); return; }
    if(PsyX_BeginScene()) ClearSplits();
    d2_native_draw_prim((P_TAG*)p);
}
