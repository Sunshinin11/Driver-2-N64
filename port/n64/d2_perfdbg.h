#ifndef D2_PERFDBG_H
#define D2_PERFDBG_H

#ifdef D2_PERFDBG
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline uint32_t perf_cnt0(void) {
    uint32_t c;
    __asm__ volatile("mfc0 %0,$9" : "=r"(c));
    return c;
}

typedef struct {
    // a) Top-level
    uint32_t frame_total_ticks;
    uint32_t stepgame_ticks;
    uint32_t drawgame_ticks;
    uint32_t backend_ticks;
    uint32_t vblank_wait_ticks;

    // b) Dentro de StepGame
    uint32_t step_cars_physics_ticks;
    uint32_t step_collision_ticks;
    uint32_t step_ai_ticks;
    uint32_t step_pedestrians_ticks;
    uint32_t step_spool_ticks;

    // c) Dentro de DrawGame
    uint32_t draw_buildings_ticks;
    uint32_t draw_ground_ticks;
    uint32_t draw_sprites_ticks;
    uint32_t draw_cars_ticks;
    uint32_t draw_shadows_ticks;
    uint32_t draw_effects_ticks;
    uint32_t draw_pedestrians_ticks;

    // d) Dentro do Backend
    uint32_t bk_get_region_ci4_ticks;
    uint32_t bk_get_region_ci8_ticks;
    uint32_t bk_get_region_16b_ticks;
    uint32_t bk_get_region_rgba32_add_ticks;
    uint32_t bk_get_region_rgba32_sub_ticks;
    uint32_t bk_shadow_sync_ticks;
    uint32_t bk_tlut_upload_ticks;
    uint32_t bk_tex_upload_ticks;
    uint32_t bk_screen_clip_ticks;
    uint32_t bk_tex_block_clip_ticks;
    uint32_t bk_calc_color_ticks;
    uint32_t bk_ot_walk_ticks;
    uint32_t bk_wait_idle_ticks;
    uint32_t bk_wait_idle_calls;

    // e) Contagens por quadro
    uint32_t cnt_triangles;
    uint32_t cnt_miss_slots;
    uint32_t cnt_texels_ci4;
    uint32_t cnt_texels_ci8;
    uint32_t cnt_texels_16b;
    uint32_t cnt_texels_rgba32_add;
    uint32_t cnt_texels_rgba32_sub;
    uint32_t cnt_tlut_uploads;
    uint32_t cnt_tex_uploads;
    uint32_t cnt_clipped_polys;
    uint32_t cnt_sub_triangles;

    // f) Audio
    uint32_t audio_pump_ticks;
    uint32_t audio_pump_calls;

} perf_frame_data_t;

extern perf_frame_data_t g_perf_cur;
extern perf_frame_data_t g_perf_acc;
extern int g_perf_acc_frames;

void perf_frame_end(void);
void perf_frame_start(void);

#define PERF_START(t) uint32_t _pt0_##t = perf_cnt0()
#define PERF_END_ADD(t, field) do { g_perf_cur.field += (perf_cnt0() - _pt0_##t); } while(0)

#ifdef __cplusplus
}
#endif

#else // !D2_PERFDBG

#define PERF_START(t) ((void)0)
#define PERF_END_ADD(t, field) ((void)0)

#endif // D2_PERFDBG
#endif // D2_PERFDBG_H

