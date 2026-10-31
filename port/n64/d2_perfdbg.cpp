#include "d2_perfdbg.h"

#ifdef D2_PERFDBG
#include <libdragon.h>
#include <string.h>

extern "C" {

perf_frame_data_t g_perf_cur;
perf_frame_data_t g_perf_acc;
int g_perf_acc_frames = 0;
static uint32_t s_frame_start_t0 = 0;

void perf_frame_start(void)
{
    s_frame_start_t0 = perf_cnt0();
    memset(&g_perf_cur, 0, sizeof(g_perf_cur));
}

void perf_frame_end(void)
{
    uint32_t now = perf_cnt0();
    g_perf_cur.frame_total_ticks = now - s_frame_start_t0;

    // Acumula
    #define ACC(f) g_perf_acc.f += g_perf_cur.f
    ACC(frame_total_ticks);
    ACC(stepgame_ticks);
    ACC(drawgame_ticks);
    ACC(backend_ticks);
    ACC(vblank_wait_ticks);

    ACC(step_cars_physics_ticks);
    ACC(step_collision_ticks);
    ACC(step_ai_ticks);
    ACC(step_pedestrians_ticks);
    ACC(step_spool_ticks);

    ACC(draw_buildings_ticks);
    ACC(draw_ground_ticks);
    ACC(draw_sprites_ticks);
    ACC(draw_cars_ticks);
    ACC(draw_shadows_ticks);
    ACC(draw_effects_ticks);
    ACC(draw_pedestrians_ticks);

    ACC(bk_get_region_ci4_ticks);
    ACC(bk_get_region_ci8_ticks);
    ACC(bk_get_region_16b_ticks);
    ACC(bk_get_region_rgba32_add_ticks);
    ACC(bk_get_region_rgba32_sub_ticks);
    ACC(bk_shadow_sync_ticks);
    ACC(bk_tlut_upload_ticks);
    ACC(bk_tex_upload_ticks);
    ACC(bk_screen_clip_ticks);
    ACC(bk_tex_block_clip_ticks);
    ACC(bk_calc_color_ticks);
    ACC(bk_ot_walk_ticks);
    ACC(bk_wait_idle_ticks);
    ACC(bk_wait_idle_calls);

    ACC(cnt_triangles);
    ACC(cnt_miss_slots);
    ACC(cnt_texels_ci4);
    ACC(cnt_texels_ci8);
    ACC(cnt_texels_16b);
    ACC(cnt_texels_rgba32_add);
    ACC(cnt_texels_rgba32_sub);
    ACC(cnt_tlut_uploads);
    ACC(cnt_tex_uploads);
    ACC(cnt_clipped_polys);
    ACC(cnt_sub_triangles);

    ACC(audio_pump_ticks);
    ACC(audio_pump_calls);
    #undef ACC

    g_perf_acc_frames++;
    if (g_perf_acc_frames >= 30)
    {
        uint32_t n = g_perf_acc_frames;
        uint32_t tot = g_perf_acc.frame_total_ticks / n;
        uint32_t step = g_perf_acc.stepgame_ticks / n;
        uint32_t draw = g_perf_acc.drawgame_ticks / n;
        uint32_t bk = g_perf_acc.backend_ticks / n;
        uint32_t vbl = g_perf_acc.vblank_wait_ticks / n;
        int32_t top_unacc = (int32_t)tot - (int32_t)(step + draw + bk + vbl);

        // StepGame breakdown
        uint32_t s_phys = g_perf_acc.step_cars_physics_ticks / n;
        uint32_t s_coll = g_perf_acc.step_collision_ticks / n;
        uint32_t s_ai = g_perf_acc.step_ai_ticks / n;
        uint32_t s_ped = g_perf_acc.step_pedestrians_ticks / n;
        uint32_t s_spool = g_perf_acc.step_spool_ticks / n;
        int32_t step_unacc = (int32_t)step - (int32_t)(s_phys + s_coll + s_ai + s_ped + s_spool);

        // DrawGame breakdown
        uint32_t d_bldg = g_perf_acc.draw_buildings_ticks / n;
        uint32_t d_grnd = g_perf_acc.draw_ground_ticks / n;
        uint32_t d_spr = g_perf_acc.draw_sprites_ticks / n;
        uint32_t d_car = g_perf_acc.draw_cars_ticks / n;
        uint32_t d_shd = g_perf_acc.draw_shadows_ticks / n;
        uint32_t d_fx = g_perf_acc.draw_effects_ticks / n;
        uint32_t d_ped = g_perf_acc.draw_pedestrians_ticks / n;
        int32_t draw_unacc = (int32_t)draw - (int32_t)(d_bldg + d_grnd + d_spr + d_car + d_shd + d_fx + d_ped);

        // Backend breakdown
        uint32_t b_ci4 = g_perf_acc.bk_get_region_ci4_ticks / n;
        uint32_t b_ci8 = g_perf_acc.bk_get_region_ci8_ticks / n;
        uint32_t b_16b = g_perf_acc.bk_get_region_16b_ticks / n;
        uint32_t b_add = g_perf_acc.bk_get_region_rgba32_add_ticks / n;
        uint32_t b_sub = g_perf_acc.bk_get_region_rgba32_sub_ticks / n;
        uint32_t b_sync = g_perf_acc.bk_shadow_sync_ticks / n;
        uint32_t b_tlut = g_perf_acc.bk_tlut_upload_ticks / n;
        uint32_t b_tup = g_perf_acc.bk_tex_upload_ticks / n;
        uint32_t b_sclip = g_perf_acc.bk_screen_clip_ticks / n;
        uint32_t b_bclip = g_perf_acc.bk_tex_block_clip_ticks / n;
        uint32_t b_col = g_perf_acc.bk_calc_color_ticks / n;
        uint32_t b_ot = g_perf_acc.bk_ot_walk_ticks / n;
        uint32_t b_wait = g_perf_acc.bk_wait_idle_ticks / n;
        uint32_t b_calls = g_perf_acc.bk_wait_idle_calls / n;

        // Counts
        uint32_t c_tri = g_perf_acc.cnt_triangles / n;
        uint32_t c_miss = g_perf_acc.cnt_miss_slots / n;
        uint32_t c_tci4 = g_perf_acc.cnt_texels_ci4 / n;
        uint32_t c_tci8 = g_perf_acc.cnt_texels_ci8 / n;
        uint32_t c_t16 = g_perf_acc.cnt_texels_16b / n;
        uint32_t c_tadd = g_perf_acc.cnt_texels_rgba32_add / n;
        uint32_t c_tsub = g_perf_acc.cnt_texels_rgba32_sub / n;
        uint32_t c_tlut = g_perf_acc.cnt_tlut_uploads / n;
        uint32_t c_texup = g_perf_acc.cnt_tex_uploads / n;
        uint32_t c_clip = g_perf_acc.cnt_clipped_polys / n;
        uint32_t c_sub = g_perf_acc.cnt_sub_triangles / n;

        // Audio
        uint32_t a_ticks = g_perf_acc.audio_pump_ticks / n;
        uint32_t a_calls = g_perf_acc.audio_pump_calls / n;

        debugf("[PERF] FRAME: total=%lu step=%lu draw=%lu bk=%lu vbl_wait=%lu unacc=%ld\n",
               (unsigned long)tot, (unsigned long)step, (unsigned long)draw, (unsigned long)bk, (unsigned long)vbl, (long)top_unacc);
        debugf("[PERF] STEP: phys=%lu coll=%lu ai=%lu peds=%lu spool=%lu unacc=%ld\n",
               (unsigned long)s_phys, (unsigned long)s_coll, (unsigned long)s_ai, (unsigned long)s_ped, (unsigned long)s_spool, (long)step_unacc);
        debugf("[PERF] DRAW: bldg=%lu ground=%lu sprites=%lu cars=%lu shadow=%lu fx=%lu peds=%lu unacc=%ld\n",
               (unsigned long)d_bldg, (unsigned long)d_grnd, (unsigned long)d_spr, (unsigned long)d_car, (unsigned long)d_shd, (unsigned long)d_fx, (unsigned long)d_ped, (long)draw_unacc);
        debugf("[PERF] BACKEND: reg_ci4=%lu reg_ci8=%lu reg_16b=%lu reg_add=%lu reg_sub=%lu sh_sync=%lu tlut_up=%lu tex_up=%lu screen_clip=%lu blk_clip=%lu color=%lu ot_walk=%lu wait_rdp=%lu (calls=%lu)\n",
               (unsigned long)b_ci4, (unsigned long)b_ci8, (unsigned long)b_16b, (unsigned long)b_add, (unsigned long)b_sub, (unsigned long)b_sync,
               (unsigned long)b_tlut, (unsigned long)b_tup, (unsigned long)b_sclip, (unsigned long)b_bclip, (unsigned long)b_col, (unsigned long)b_ot, (unsigned long)b_wait, (unsigned long)b_calls);
        debugf("[PERF] COUNTS: tris=%lu miss=%lu tex_ci4=%lu tex_ci8=%lu tex_16b=%lu tex_add=%lu tex_sub=%lu tlut_up=%lu tex_up=%lu clips=%lu sub_tris=%lu\n",
               (unsigned long)c_tri, (unsigned long)c_miss, (unsigned long)c_tci4, (unsigned long)c_tci8, (unsigned long)c_t16, (unsigned long)c_tadd, (unsigned long)c_tsub,
               (unsigned long)c_tlut, (unsigned long)c_texup, (unsigned long)c_clip, (unsigned long)c_sub);
        debugf("[PERF] AUDIO: ticks=%lu calls=%lu\n",
               (unsigned long)a_ticks, (unsigned long)a_calls);

        memset(&g_perf_acc, 0, sizeof(g_perf_acc));
        g_perf_acc_frames = 0;
    }
}

} // extern "C"
#endif // D2_PERFDBG

