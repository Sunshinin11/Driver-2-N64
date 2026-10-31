#include "d2_origin_prof.h"

#ifdef D2_ORIGIN_PROF
#include <libdragon.h>
#include <string.h>

D2OProfData g_d2_oprof;
uint8_t g_d2_oprof_cat = OPROF_OTHER;
uint8_t g_d2_oprof_bk_cat = OPROF_OTHER;
uint32_t g_d2_oprof_last_t = 0;

static const char * const s_cat_names[OPROF_COUNT] = {
    "OTHER",
    "MAP_WALK",
    "PERM_BLD",
    "SPOOL_BLD",
    "GROUND",
    "SPRITES",
    "CARS",
    "SHADOWS",
    "PEDS",
    "EFFECTS",
    "SKY",
    "HUD",
    "T3D_BLD",
    "T3D_GRD"
};

extern "C" void d2_oprof_window_report(int cc, int nframes)
{
    if (nframes <= 0) {
        memset(&g_d2_oprof, 0, sizeof(g_d2_oprof));
        return;
    }
    const unsigned nf = (unsigned)nframes;
    uint32_t tot_asm = 0, tot_bk = 0;
    for (int c = 0; c < OPROF_COUNT; c++) {
        tot_asm += g_d2_oprof.asm_ticks[c];
        tot_bk  += g_d2_oprof.bk_ticks[c];
    }
    debugf("[OPROF_SUM] cc=%d nf=%u step=%u asm=%u bk=%u ot_ovh=%u | perm(rf=%u pr=%u al=%u ot=%u) spool(st=%u ot=%u) grd(s4=%u s2=%u x1=%u)\n",
           cc, nf,
           g_d2_oprof.step_ticks / nf,
           tot_asm / nf,
           tot_bk / nf,
           g_d2_oprof.ot_overhead_ticks / nf,
           g_d2_oprof.perm_bld_roadfurn / nf,
           g_d2_oprof.perm_bld_props / nf,
           g_d2_oprof.perm_bld_alpha / nf,
           g_d2_oprof.perm_bld_other / nf,
           g_d2_oprof.spool_bld_static / nf,
           g_d2_oprof.spool_bld_other / nf,
           g_d2_oprof.ground_sub4 / nf,
           g_d2_oprof.ground_sub2 / nf,
           g_d2_oprof.ground_1x1 / nf);

    for (int c = 0; c < OPROF_COUNT; c++) {
        uint32_t at = g_d2_oprof.asm_ticks[c];
        uint32_t bt = g_d2_oprof.bk_ticks[c];
        uint32_t pr = g_d2_oprof.prims[c];
        uint32_t tr = g_d2_oprof.tris[c];
        uint32_t ob = g_d2_oprof.objs[c];
        if (at == 0 && bt == 0 && pr == 0 && tr == 0 && ob == 0) continue;
        debugf("[OPROF] cc=%d cat=%-9s tot=%u asm=%u bk=%u objs=%u verts=%u prims=%u tris=%u sclip=%u uvclip=%u sub=%u texUp=%u tlutUp=%u bytes=%u\n",
               cc, s_cat_names[c],
               (at + bt) / nf,
               at / nf,
               bt / nf,
               ob / nf,
               g_d2_oprof.verts[c] / nf,
               pr / nf,
               tr / nf,
               g_d2_oprof.sclips[c] / nf,
               g_d2_oprof.uvclips[c] / nf,
               g_d2_oprof.subtris[c] / nf,
               g_d2_oprof.tex_up[c] / nf,
               g_d2_oprof.tlut_up[c] / nf,
               g_d2_oprof.tex_bytes[c] / nf);
    }
    memset(&g_d2_oprof, 0, sizeof(g_d2_oprof));
}

#endif // D2_ORIGIN_PROF

