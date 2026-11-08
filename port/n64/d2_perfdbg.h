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
    uint32_t bk_tex_bclip_upload_ticks;
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

    // Diagnostico 2: tri_core paths & blocks
    uint32_t cnt_tri_single_block;
    uint32_t cnt_tri_block_clip;
    uint32_t cnt_tri_subdiv;
    uint32_t cnt_blocks_generated;
    uint32_t max_blocks_per_tri;
    uint32_t cnt_tex_up_redundant;
    uint32_t cnt_tex_up_unique;

    // Diagnostico 2: UV distribution
    uint32_t uv_bld_count, uv_bld_w_sum, uv_bld_h_sum;
    uint32_t uv_ground_count, uv_ground_w_sum, uv_ground_h_sum;
    uint32_t uv_cars_count, uv_cars_w_sum, uv_cars_h_sum;
    uint32_t uv_peds_count, uv_peds_w_sum, uv_peds_h_sum;
    uint32_t uv_other_count, uv_other_w_sum, uv_other_h_sum;

    // Diagnostico 3: Pedestrians
    uint32_t cnt_pedestrians_drawn;
    uint32_t ped_anim_ticks;
    uint32_t ped_transform_ticks;
    uint32_t ped_assembly_ticks;


    // f) Audio
    uint32_t audio_pump_ticks;
    uint32_t audio_pump_calls;

} perf_frame_data_t;

extern perf_frame_data_t g_perf_cur;
extern perf_frame_data_t g_perf_acc;
extern int g_perf_acc_frames;
extern int g_in_pedestrians;

// Cull counters (commit 3d5a70f)
extern uint32_t g_cull_cnt_arrow;
extern uint32_t g_cull_cnt_tile1x1;
extern uint32_t g_cull_cnt_tile1x1lit;
extern uint32_t g_cull_cnt_bld;
extern uint32_t g_cull_cnt_bld_subdiv;
extern uint32_t g_cull_cnt_drawmesh;
extern uint32_t g_cull_cnt_drawmeshlit;

void perf_frame_end(void);
void perf_frame_start(void);
void track_region_upload(int px, int py, int u0, int v0, int w, int h, int step, int clut, int fmt, int rgba);

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
