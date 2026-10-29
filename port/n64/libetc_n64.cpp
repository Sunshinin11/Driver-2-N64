// Substitui PsyCross/src/psx/LIBETC.C (que usa SDL_timer): callbacks de vblank, VSync e modo de video.
#include "psx/libetc.h"
#include "PsyX/PsyX_public.h"
extern "C" {
void d2_sys_set_vbl_cb(void (*f)(void));
int  d2_sys_vbl_count(void);
void d2_sys_wait_vbl_until(int target);
extern int g_vmode;
void PsyX_WaitForTimestep(int count);

char scratchData[4096];
char *_scratchData = scratchData;
void (*vsync_callback)(void) = 0;

int StopCallback(void) { return 0; }
int ResetCallback(void) { int old = (int)(long)vsync_callback; vsync_callback = 0; d2_sys_set_vbl_cb(0); return old; }
int VSync(int mode)
{
    if (mode < 0) return d2_sys_vbl_count();
    if (mode == 0) PsyX_WaitForTimestep(1);
    else PsyX_WaitForTimestep(mode);
    return d2_sys_vbl_count();
}
int VSyncCallback(void (*f)(void)) { int old = (int)(long)vsync_callback; vsync_callback = f; d2_sys_set_vbl_cb(f); return old; }
int SetVideoMode(int mode) { int old = g_vmode; g_vmode = mode; return old; }
int GetVideoMode() { return g_vmode; }
void PadInit(int) {}
u_int PadRead(int) { return 0; }
void PadStop(void) {}
}
