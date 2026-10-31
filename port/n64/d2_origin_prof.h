#ifndef D2_ORIGIN_PROF_H
#define D2_ORIGIN_PROF_H

#ifdef D2_ORIGIN_PROF
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum D2OProfCat {
    OPROF_OTHER     = 0,
    OPROF_MAP_WALK  = 1,
    OPROF_PERM_BLD  = 2,
    OPROF_SPOOL_BLD = 3,
    OPROF_GROUND    = 4,
    OPROF_SPRITES   = 5,
    OPROF_CARS      = 6,
    OPROF_SHADOWS   = 7,
    OPROF_PEDS      = 8,
    OPROF_EFFECTS   = 9,
    OPROF_SKY       = 10,
    OPROF_HUD       = 11,
    OPROF_T3D_BLD   = 12,
    OPROF_T3D_GRD   = 13,
    OPROF_COUNT     = 14
};

typedef struct D2OProfData {
    uint32_t step_ticks;
    uint32_t ot_overhead_ticks;
    uint32_t asm_ticks[OPROF_COUNT];
    uint32_t bk_ticks[OPROF_COUNT];
    uint32_t objs[OPROF_COUNT];
    uint32_t verts[OPROF_COUNT];
    uint32_t prims[OPROF_COUNT];
    uint32_t tris[OPROF_COUNT];
    uint32_t sclips[OPROF_COUNT];
    uint32_t uvclips[OPROF_COUNT];
    uint32_t subtris[OPROF_COUNT];
    uint32_t tex_up[OPROF_COUNT];
    uint32_t tlut_up[OPROF_COUNT];
    uint32_t tex_bytes[OPROF_COUNT];

    // Sub-counters for static world analysis
    uint32_t perm_bld_roadfurn;
    uint32_t perm_bld_props;
    uint32_t perm_bld_alpha;
    uint32_t perm_bld_other;
    uint32_t spool_bld_static;
    uint32_t spool_bld_other;
    uint32_t ground_sub4;
    uint32_t ground_sub2;
    uint32_t ground_1x1;
} D2OProfData;

extern D2OProfData g_d2_oprof;
extern uint8_t g_d2_oprof_cat;
extern uint8_t g_d2_oprof_bk_cat;
extern uint32_t g_d2_oprof_last_t;

static inline uint32_t d2_oprof_cnt0(void) {
    uint32_t c;
    __asm__ volatile("mfc0 %0,$9" : "=r"(c));
    return c;
}

static inline int d2_oprof_switch(int new_cat) {
    uint32_t now = d2_oprof_cnt0();
    int prev = g_d2_oprof_cat;
    g_d2_oprof.asm_ticks[prev] += now - g_d2_oprof_last_t;
    g_d2_oprof_last_t = now;
    g_d2_oprof_cat = (uint8_t)new_cat;
    return prev;
}

static inline void d2_oprof_draw_begin(void) {
    g_d2_oprof_cat = OPROF_OTHER;
    g_d2_oprof_last_t = d2_oprof_cnt0();
}

static inline void d2_oprof_draw_end(void) {
    uint32_t now = d2_oprof_cnt0();
    g_d2_oprof.asm_ticks[g_d2_oprof_cat] += now - g_d2_oprof_last_t;
    g_d2_oprof_last_t = now;
    g_d2_oprof_cat = OPROF_OTHER;
}

void d2_oprof_window_report(int cc, int nframes);

#ifdef __cplusplus
}

struct D2OProfGuard {
    int prev;
    inline D2OProfGuard(int cat) : prev(d2_oprof_switch(cat)) {}
    inline ~D2OProfGuard() { d2_oprof_switch(prev); }
};
#define D2_OPROF_SCOPE(var, cat)      D2OProfGuard var(cat)
#endif

#define D2_OPROF_DRAW_BEGIN()         d2_oprof_draw_begin()
#define D2_OPROF_DRAW_END()           d2_oprof_draw_end()
#define D2_OPROF_ENTER(var, cat)      int var = d2_oprof_switch(cat)
#define D2_OPROF_LEAVE(var)           ((void)d2_oprof_switch(var))
#define D2_OPROF_OBJ(cat)             (g_d2_oprof.objs[(cat)]++)

#else // !D2_ORIGIN_PROF

#define D2_OPROF_SCOPE(var, cat)      ((void)0)
#define D2_OPROF_DRAW_BEGIN()         ((void)0)
#define D2_OPROF_DRAW_END()           ((void)0)
#define D2_OPROF_ENTER(var, cat)      ((void)0)
#define D2_OPROF_LEAVE(var)           ((void)0)
#define D2_OPROF_OBJ(cat)             ((void)0)

#endif // D2_ORIGIN_PROF

#endif // D2_ORIGIN_PROF_H
