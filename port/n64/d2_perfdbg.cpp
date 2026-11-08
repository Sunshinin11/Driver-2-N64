#include "d2_perfdbg.h"

#ifdef D2_PERFDBG
#include <libdragon.h>
#include <string.h>

extern "C" {

perf_frame_data_t g_perf_cur;
perf_frame_data_t g_perf_acc;
int g_perf_acc_frames = 0;
int g_in_pedestrians = 0;
static uint32_t s_frame_start_t0 = 0;

// Cull counters (commit 3d5a70f)
uint32_t g_cull_cnt_arrow = 0;
uint32_t g_cull_cnt_tile1x1 = 0;
uint32_t g_cull_cnt_tile1x1lit = 0;
uint32_t g_cull_cnt_bld = 0;
uint32_t g_cull_cnt_bld_subdiv = 0;
uint32_t g_cull_cnt_drawmesh = 0;
uint32_t g_cull_cnt_drawmeshlit = 0;

// Redundant upload tracking
struct UploadRecord {
    uint32_t k0; // px, py, u0, v0
    uint32_t k1; // w, h, step, fmt, rgba
    uint32_t k2; // clut
};
#define MAX_FRAME_UPLOADS 2048
static UploadRecord s_frame_uploads[MAX_FRAME_UPLOADS];
static int s_frame_upload_count = 0;

void track_region_upload(int px, int py, int u0, int v0, int w, int h, int step, int clut, int fmt, int rgba)
{
    uint32_t k0 = (uint32_t)(px & 0xFFF) | ((uint32_t)(py & 0xFFF) << 12) | ((uint32_t)(u0 & 0xFF) << 24);
    uint32_t k1 = (uint32_t)(v0 & 0xFF) | ((uint32_t)(w & 0x1FF) << 8) | ((uint32_t)(h & 0x1FF) << 17) |
                  ((uint32_t)(step & 0xF) << 26) | ((uint32_t)(fmt & 3) << 30);
    uint32_t k2 = (uint32_t)(clut & 0xFFFF) | ((uint32_t)(rgba ? 1 : 0) << 16);

    for (int i = 0; i < s_frame_upload_count; i++) {
        if (s_frame_uploads[i].k0 == k0 && s_frame_uploads[i].k1 == k1 && s_frame_uploads[i].k2 == k2) {
            g_perf_cur.cnt_tex_up_redundant++;
            return;
        }
    }
    if (s_frame_upload_count < MAX_FRAME_UPLOADS) {
        s_frame_uploads[s_frame_upload_count].k0 = k0;
        s_frame_uploads[s_frame_upload_count].k1 = k1;
        s_frame_uploads[s_frame_upload_count].k2 = k2;
        s_frame_upload_count++;
    }
    g_perf_cur.cnt_tex_up_unique++;
}

void perf_frame_start(void)
{
    s_frame_start_t0 = perf_cnt0();
    memset(&g_perf_cur, 0, sizeof(g_perf_cur));
    s_frame_upload_count = 0;
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
    ACC(bk_tex_bclip_upload_ticks);
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

    // Diag 2
    ACC(cnt_tri_single_block);
    ACC(cnt_tri_block_clip);
    ACC(cnt_tri_subdiv);
    ACC(cnt_blocks_generated);
    if (g_perf_cur.max_blocks_per_tri > g_perf_acc.max_blocks_per_tri)
        g_perf_acc.max_blocks_per_tri = g_perf_cur.max_blocks_per_tri;
    ACC(cnt_tex_up_redundant);
    ACC(cnt_tex_up_unique);

    ACC(uv_bld_count); ACC(uv_bld_w_sum); ACC(uv_bld_h_sum);
    ACC(uv_ground_count); ACC(uv_ground_w_sum); ACC(uv_ground_h_sum);
    ACC(uv_cars_count); ACC(uv_cars_w_sum); ACC(uv_cars_h_sum);
    ACC(uv_peds_count); ACC(uv_peds_w_sum); ACC(uv_peds_h_sum);
    ACC(uv_other_count); ACC(uv_other_w_sum); ACC(uv_other_h_sum);

    // Diag 3
    ACC(cnt_pedestrians_drawn);
    ACC(ped_anim_ticks);
    ACC(ped_transform_ticks);
    ACC(ped_assembly_ticks);


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
        uint32_t b_bclip_up = g_perf_acc.bk_tex_bclip_upload_ticks / n;
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

        // Diag 2 counts
        uint32_t c_sblk = g_perf_acc.cnt_tri_single_block / n;
        uint32_t c_bclip = g_perf_acc.cnt_tri_block_clip / n;
        uint32_t c_subdiv = g_perf_acc.cnt_tri_subdiv / n;
        uint32_t c_blks = g_perf_acc.cnt_blocks_generated / n;
        uint32_t c_max_blks = g_perf_acc.max_blocks_per_tri;
        uint32_t c_tup_redun = g_perf_acc.cnt_tex_up_redundant / n;
        uint32_t c_tup_uniq = g_perf_acc.cnt_tex_up_unique / n;

        // Diag 3 counts & ticks
        uint32_t c_peds_tot = g_perf_acc.cnt_pedestrians_drawn;
        uint32_t c_peds = (c_peds_tot + n / 2) / n;
        uint32_t p_anim = g_perf_acc.ped_anim_ticks / n;
        uint32_t p_trans = g_perf_acc.ped_transform_ticks / n;
        uint32_t p_assem = g_perf_acc.ped_assembly_ticks / n;
        int32_t ped_unacc = (int32_t)d_ped - (int32_t)(p_anim + p_trans + p_assem);
        uint32_t avg_ped = c_peds_tot ? (g_perf_acc.draw_pedestrians_ticks / c_peds_tot) : 0;

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

        debugf("[CULL-3D5A70F] arrow=%lu tile1x1=%lu tile1x1lit=%lu bld=%lu bld_subdiv=%lu drawmesh=%lu drawmeshlit=%lu\n",
               (unsigned long)g_cull_cnt_arrow, (unsigned long)g_cull_cnt_tile1x1, (unsigned long)g_cull_cnt_tile1x1lit,
               (unsigned long)g_cull_cnt_bld, (unsigned long)g_cull_cnt_bld_subdiv,
               (unsigned long)g_cull_cnt_drawmesh, (unsigned long)g_cull_cnt_drawmeshlit);

        debugf("[PERF-DIAG2] bclip_excl=%lu bclip_up=%lu ot_walk=%lu single_blk=%lu blk_clip=%lu subdiv=%lu blks_gen=%lu max_blks=%lu texup_uniq=%lu texup_redun=%lu\n",
               (unsigned long)((b_bclip > b_bclip_up) ? (b_bclip - b_bclip_up) : 0), (unsigned long)b_bclip_up, (unsigned long)b_ot,
               (unsigned long)c_sblk, (unsigned long)c_bclip, (unsigned long)c_subdiv, (unsigned long)c_blks, (unsigned long)c_max_blks,
               (unsigned long)c_tup_uniq, (unsigned long)c_tup_redun);

        uint32_t uv_bld_c = g_perf_acc.uv_bld_count / n;
        uint32_t uv_grn_c = g_perf_acc.uv_ground_count / n;
        uint32_t uv_car_c = g_perf_acc.uv_cars_count / n;
        uint32_t uv_ped_c = g_perf_acc.uv_peds_count / n;
        uint32_t uv_oth_c = g_perf_acc.uv_other_count / n;

        debugf("[PERF-DIAG2-UV] bld(cnt=%lu avg=%lux%lu) ground(cnt=%lu avg=%lux%lu) cars(cnt=%lu avg=%lux%lu) peds(cnt=%lu avg=%lux%lu) other(cnt=%lu avg=%lux%lu)\n",
               (unsigned long)uv_bld_c, (unsigned long)(uv_bld_c ? (g_perf_acc.uv_bld_w_sum / (n * uv_bld_c)) : 0), (unsigned long)(uv_bld_c ? (g_perf_acc.uv_bld_h_sum / (n * uv_bld_c)) : 0),
               (unsigned long)uv_grn_c, (unsigned long)(uv_grn_c ? (g_perf_acc.uv_ground_w_sum / (n * uv_grn_c)) : 0), (unsigned long)(uv_grn_c ? (g_perf_acc.uv_ground_h_sum / (n * uv_grn_c)) : 0),
               (unsigned long)uv_car_c, (unsigned long)(uv_car_c ? (g_perf_acc.uv_cars_w_sum / (n * uv_car_c)) : 0), (unsigned long)(uv_car_c ? (g_perf_acc.uv_cars_h_sum / (n * uv_car_c)) : 0),
               (unsigned long)uv_ped_c, (unsigned long)(uv_ped_c ? (g_perf_acc.uv_peds_w_sum / (n * uv_ped_c)) : 0), (unsigned long)(uv_ped_c ? (g_perf_acc.uv_peds_h_sum / (n * uv_ped_c)) : 0),
               (unsigned long)uv_oth_c, (unsigned long)(uv_oth_c ? (g_perf_acc.uv_other_w_sum / (n * uv_oth_c)) : 0), (unsigned long)(uv_oth_c ? (g_perf_acc.uv_other_h_sum / (n * uv_oth_c)) : 0));

        debugf("[PERF-DIAG3] peds_cnt=%lu avg_ped_ticks=%lu anim=%lu trans=%lu assem=%lu unacc=%ld\n",
               (unsigned long)c_peds, (unsigned long)avg_ped,
               (unsigned long)p_anim, (unsigned long)p_trans, (unsigned long)p_assem, (long)ped_unacc);

        memset(&g_perf_acc, 0, sizeof(g_perf_acc));
        g_perf_acc_frames = 0;
    }
}

} // extern "C"
#endif // D2_PERFDBG
