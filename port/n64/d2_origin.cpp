#include "d2_origin.h"
#include <libdragon.h>
#include <string.h>
#include <stdio.h>

#ifdef D2_ORIGIN

#define ORIGIN_TABLE_SIZE 2048
#define ORIGIN_TABLE_MASK (ORIGIN_TABLE_SIZE - 1)

static D2OriginRecord s_originTable[ORIGIN_TABLE_SIZE];
static const char *s_curScope = "unknown";
static int s_curModelType = -1;
static int s_curCellPos[3] = {0, 0, 0};

extern int d2_rdp_frames;

void d2_origin_init(void)
{
    memset(s_originTable, 0, sizeof(s_originTable));
}

void d2_origin_frame_begin(void)
{
    memset(s_originTable, 0, sizeof(s_originTable));
    s_curScope = "unknown";
    s_curModelType = -1;
    s_curCellPos[0] = s_curCellPos[1] = s_curCellPos[2] = 0;
}

void d2_origin_set_cell(int model_type, int cx, int cy, int cz)
{
    s_curModelType = model_type;
    s_curCellPos[0] = cx;
    s_curCellPos[1] = cy;
    s_curCellPos[2] = cz;
}

void d2_origin_set_scope(const char *func_name)
{
    s_curScope = func_name ? func_name : "unknown";
}

const char *d2_origin_get_scope(void)
{
    return s_curScope;
}

void d2_origin_tag(const void *prim, const char *func, int model_type, const int cell_pos[3], int ptype, const short sxy[4][2], const short sz[4])
{
    if (!prim) return;
    unsigned int h = (((uintptr_t)prim >> 2) ^ ((uintptr_t)prim >> 8)) & ORIGIN_TABLE_MASK;
    for (int i = 0; i < 32; i++) {
        unsigned int idx = (h + i) & ORIGIN_TABLE_MASK;
        if (s_originTable[idx].prim == NULL || s_originTable[idx].prim == prim) {
            s_originTable[idx].prim = prim;
            s_originTable[idx].func_name = func ? func : s_curScope;
            s_originTable[idx].model_type = (short)(model_type >= 0 ? model_type : s_curModelType);
            s_originTable[idx].ptype = (short)ptype;
            if (cell_pos) {
                s_originTable[idx].cell_pos[0] = cell_pos[0];
                s_originTable[idx].cell_pos[1] = cell_pos[1];
                s_originTable[idx].cell_pos[2] = cell_pos[2];
            } else {
                s_originTable[idx].cell_pos[0] = s_curCellPos[0];
                s_originTable[idx].cell_pos[1] = s_curCellPos[1];
                s_originTable[idx].cell_pos[2] = s_curCellPos[2];
            }
            if (sxy) memcpy(s_originTable[idx].sxy, sxy, sizeof(s_originTable[idx].sxy));
            else memset(s_originTable[idx].sxy, 0, sizeof(s_originTable[idx].sxy));
            if (sz) memcpy(s_originTable[idx].sz, sz, sizeof(s_originTable[idx].sz));
            else memset(s_originTable[idx].sz, 0, sizeof(s_originTable[idx].sz));
            return;
        }
    }
}

void d2_origin_tag_auto(const void *prim)
{
    if (!prim) return;
    if (d2_origin_lookup(prim)) return; // already tagged
    d2_origin_tag(prim, s_curScope, s_curModelType, s_curCellPos, -1, NULL, NULL);
}

const D2OriginRecord *d2_origin_lookup(const void *prim)
{
    if (!prim) return NULL;
    unsigned int h = (((uintptr_t)prim >> 2) ^ ((uintptr_t)prim >> 8)) & ORIGIN_TABLE_MASK;
    for (int i = 0; i < 32; i++) {
        unsigned int idx = (h + i) & ORIGIN_TABLE_MASK;
        if (s_originTable[idx].prim == prim) return &s_originTable[idx];
        if (s_originTable[idx].prim == NULL) break;
    }
    return NULL;
}

void d2_origin_check_large(const void *prim, int code, int tpage, int clut, const short xs[4], const short ys[4], const unsigned char us[4], const unsigned char vs[4], int nv)
{
    return; // silenciado para não desacelerar o Ares durante a investigação de transparência
    short minx = xs[0], maxx = xs[0], miny = ys[0], maxy = ys[0];
    for (int i = 1; i < nv; i++) {
        if (xs[i] < minx) minx = xs[i]; if (xs[i] > maxx) maxx = xs[i];
        if (ys[i] < miny) miny = ys[i]; if (ys[i] > maxy) maxy = ys[i];
    }
    int w = maxx - minx, h = maxy - miny;
    if (w > 200 || h > 200) {
        const D2OriginRecord *rec = d2_origin_lookup(prim);
        const char *func = rec ? rec->func_name : (s_curScope ? s_curScope : "UNKNOWN_ORIGIN");
        int model = rec ? rec->model_type : s_curModelType;
        int ptype = rec ? rec->ptype : -1;
        int cx = rec ? rec->cell_pos[0] : s_curCellPos[0];
        int cy = rec ? rec->cell_pos[1] : s_curCellPos[1];
        int cz = rec ? rec->cell_pos[2] : s_curCellPos[2];
        debugf("[ORIGIN-LARGE] f=%d prim=%p func=%s model=%d cell=(%d,%d,%d) ptype=%d w=%d h=%d code=0x%02X tpage=0x%04X clut=0x%04X xy=[(%d,%d),(%d,%d),(%d,%d),(%d,%d)] uv=[(%d,%d),(%d,%d),(%d,%d),(%d,%d)]\n",
            d2_rdp_frames, prim, func, model, cx, cy, cz, ptype, w, h, code, (unsigned short)tpage, (unsigned short)clut,
            xs[0], ys[0], xs[1], ys[1], xs[2], ys[2], nv > 3 ? xs[3] : 0, nv > 3 ? ys[3] : 0,
            us ? us[0] : 0, vs ? vs[0] : 0, us ? us[1] : 0, vs ? vs[1] : 0, us ? us[2] : 0, vs ? vs[2] : 0, (us && nv > 3) ? us[3] : 0, (vs && nv > 3) ? vs[3] : 0);
        if (rec && (rec->sz[0] || rec->sz[1] || rec->sz[2] || rec->sz[3])) {
            debugf("    orig_sxy=[(%d,%d),(%d,%d),(%d,%d),(%d,%d)] orig_sz=[%d,%d,%d,%d]\n",
                rec->sxy[0][0], rec->sxy[0][1], rec->sxy[1][0], rec->sxy[1][1],
                rec->sxy[2][0], rec->sxy[2][1], rec->sxy[3][0], rec->sxy[3][1],
                rec->sz[0], rec->sz[1], rec->sz[2], rec->sz[3]);
        }
    }
}

void d2_origin_log_hud(const void *prim, int code, int semi, unsigned int col, const short xs[4], const short ys[4], int nv, int textured, int fmt, int blend)
{
    const D2OriginRecord *rec = d2_origin_lookup(prim);
    if (!rec) return;
    if (!strstr(rec->func_name, "overlay") && !strstr(rec->func_name, "HUD") && !strstr(rec->func_name, "Bar")) return;

    if (d2_rdp_frames < 200) return; // wait until gameplay

    static int s_loggedFrame = -1;
    static int s_loggedCount = 0;
    static int s_framesLogged = 0;
    if (s_loggedFrame != d2_rdp_frames) {
        if (s_framesLogged >= 2) return; // log up to 2 distinct frames
        s_loggedFrame = d2_rdp_frames;
        s_loggedCount = 0;
        s_framesLogged++;
        debugf("[HUD-FRAME-START] Frame %d dumping HUD primitives in OT order:\n", d2_rdp_frames);
    }
    s_loggedCount++;

    debugf("[HUD-PRIM] #%02d f=%d prim=%p code=0x%02X semi=%d col=0x%08lX xy=[(%d,%d),(%d,%d),(%d,%d),(%d,%d)] rdp_mode=[tex=%d fmt=%d blend=%d] func=%s\n",
        s_loggedCount, d2_rdp_frames, prim, code, semi, (unsigned long)col,
        xs[0], ys[0], xs[1], ys[1], xs[2], ys[2], nv > 3 ? xs[3] : 0, nv > 3 ? ys[3] : 0,
        textured, fmt, blend, rec->func_name);
}

#ifdef D2_TRANSDBG

extern unsigned short vram[];

enum TransEffectCat {
    CAT_TAILLIGHT = 0,    // ShowCarlight / ShowLight / ShowFlare / BrakeLight / NightLights
    CAT_POOL_BACK = 1,    // PlacePoolForCar sub_level 0 (brilho lanterna traseira no chão)
    CAT_HEADLIGHT = 2,    // sQuad / PlacePoolForCar sub_level 3 (farol / cone dianteiro)
    CAT_SHADOW = 3,       // PlaceShadowForCar / drawMesh
    CAT_SMOKE = 4,        // DisplaySmoke / debris.c
    CAT_SPARK = 5,        // DisplaySpark / debris.c
    CAT_TRAFFIC = 6,      // ShowGroundLight / AddTrafficLight
    CAT_HUD = 7,          // DrawPercentageBar / overlay.c
    CAT_COUNT = 8
};

static int s_transdbgCounts[CAT_COUNT] = {0};

void d2_transdbg_inspect(const void *prim, int code, int semi, int bm, int tpage, int clut,
                         const short xs[4], const short ys[4],
                         const unsigned char us[4], const unsigned char vs[4], int nv,
                         const unsigned char cr[4], const unsigned char cg[4], const unsigned char cb[4])
{
    if (d2_rdp_frames < 30) return; // ignora primeiros quadros de boot/logo

    const D2OriginRecord *rec = d2_origin_lookup(prim);
    const char *func = rec ? rec->func_name : s_curScope;
    if (!func) func = "unknown";

    int cat = -1;
    const char *effect_name = NULL;

    if (strstr(func, "overlay") || strstr(func, "HUD") || strstr(func, "Bar")) {
        cat = CAT_HUD;
        effect_name = "HUD Percentage Bar";
    } else if (strstr(func, "TrafficLight") || strstr(func, "GroundLight")) {
        cat = CAT_TRAFFIC;
        effect_name = "Traffic Light Glow";
    } else if (strstr(func, "PoolBack") ||
               (bm == 2 && (code == 0x2C || code == 0x2E) && us != NULL && (strstr(func, "Pool") || strstr(func, "debris")))) {
        cat = CAT_POOL_BACK;
        effect_name = "Taillight Glow On Road (PoolBack)";
    } else if (strstr(func, "PoolFront") || strstr(func, "HeadlightCone") || strstr(func, "sQuad") ||
               (bm == 2 && (code == 0x38 || code == 0x3A) && us == NULL && !strstr(func, "overlay") && !strstr(func, "Bar"))) {
        cat = CAT_HEADLIGHT;
        effect_name = "Headlight Cone / Pool (sQuad)";
    } else if (strstr(func, "Carlight") || strstr(func, "BrakeLight") || strstr(func, "NightLights") ||
               strstr(func, "ShowLight") || strstr(func, "ShowFlare")) {
        cat = CAT_TAILLIGHT;
        effect_name = "Taillight / Brake / Flare";
    } else if (strstr(func, "shadow") || (bm == 3 && us != NULL && !strstr(func, "Tile") && !strstr(func, "Building"))) {
        cat = CAT_SHADOW;
        effect_name = "Car Shadow";
    } else if (strstr(func, "Smoke")) {
        cat = CAT_SMOKE;
        effect_name = "Collision Smoke";
    } else if (strstr(func, "Spark") || ((code == 0x30 || code == 0x32) && strstr(func, "debris"))) {
        cat = CAT_SPARK;
        effect_name = "Collision Spark";
    }

    if (cat < 0) return;
    int in_game = (d2_rdp_frames >= 450) ? 1 : 0;
    static int s_counts[2][CAT_COUNT] = {{0}};
    if (s_counts[in_game][cat] >= 3) return;
    s_counts[in_game][cat]++;

    const char *code_str = "UNKNOWN";
    if (code == 0x2C || code == 0x2E) code_str = (code == 0x2E) ? "POLY_FT4 (semi)" : "POLY_FT4";
    else if (code == 0x24 || code == 0x26) code_str = (code == 0x26) ? "POLY_FT3 (semi)" : "POLY_FT3";
    else if (code == 0x38 || code == 0x3A) code_str = (code == 0x3A) ? "POLY_G4 (semi)" : "POLY_G4";
    else if (code == 0x30 || code == 0x32) code_str = (code == 0x32) ? "POLY_G3 (semi)" : "POLY_G3";
    else if (code == 0x20 || code == 0x22) code_str = (code == 0x22) ? "POLY_F3 (semi)" : "POLY_F3";
    else if (code == 0x28 || code == 0x2A) code_str = (code == 0x2A) ? "POLY_F4 (semi)" : "POLY_F4";

    int ps1_mode = (tpage >> 5) & 3;
    const char *mode_str = (ps1_mode == 0) ? "0: B/2 + F/2 (50% blend)" :
                           (ps1_mode == 1) ? "1: B + F (additive)" :
                           (ps1_mode == 2) ? "2: B - F (subtractive)" : "3: B + F/4 (25% additive)";

    debugf("\n[TRANSDBG] ======================================================================\n");
    debugf("[TRANSDBG-EFFECT] #%d/3 cat=%d [%s] func='%s' stage=%s f=%d prim=%p\n",
           s_counts[in_game][cat], cat, effect_name, func, in_game ? "INGAME" : "MENU", d2_rdp_frames, prim);
    debugf("  prim: code=0x%02X (%s) tex=%d semi=%d tpage=0x%04X clut=0x%04X ps1_mode=[%s]\n",
           code, code_str, us != NULL ? 1 : 0, semi, (unsigned short)tpage, (unsigned short)clut, mode_str);

    if (nv == 4) {
        debugf("  coords: xy=[(%d,%d),(%d,%d),(%d,%d),(%d,%d)]\n", xs[0], ys[0], xs[1], ys[1], xs[2], ys[2], xs[3], ys[3]);
        debugf("  colors: rgb=[(%d,%d,%d),(%d,%d,%d),(%d,%d,%d),(%d,%d,%d)]\n", cr[0], cg[0], cb[0], cr[1], cg[1], cb[1], cr[2], cg[2], cb[2], cr[3], cg[3], cb[3]);
    } else {
        debugf("  coords: xy=[(%d,%d),(%d,%d),(%d,%d)]\n", xs[0], ys[0], xs[1], ys[1], xs[2], ys[2]);
        debugf("  colors: rgb=[(%d,%d,%d),(%d,%d,%d),(%d,%d,%d)]\n", cr[0], cg[0], cb[0], cr[1], cg[1], cb[1], cr[2], cg[2], cb[2]);
    }

    int fmt = (tpage >> 7) & 3;
    const char *fmt_str = (fmt == 0) ? "TF_4_BIT (CI4)" : (fmt == 1) ? "TF_8_BIT (CI8)" : "TF_16_BIT (RGBA16/Direct)";
    int px = (tpage & 15) * 64;
    int py = ((tpage >> 4) & 1) * 256;
    int cx = (clut & 63) * 16;
    int cy = clut >> 6;

    int count_total = 0, count_stp1 = 0, count_stp0 = 0, count_zero = 0;

    if (us != NULL && vs != NULL) {
        int umin = us[0], umax = us[0], vmin = vs[0], vmax = vs[0];
        for (int i = 1; i < nv; i++) {
            if (us[i] < umin) umin = us[i]; if (us[i] > umax) umax = us[i];
            if (vs[i] < vmin) vmin = vs[i]; if (vs[i] > vmax) vmax = vs[i];
        }
        int bw = umax - umin + 1, bh = vmax - vmin + 1;
        debugf("  tex: fmt=%d (%s) tpage_xy=(%d,%d) clut_xy=(%d,%d) uv_bbox=[u:%d..%d, v:%d..%d] (dim: %dx%d)\n",
               fmt, fmt_str, px, py, cx, cy, umin, umax, vmin, vmax, bw, bh);

        struct { int u, v, idx; unsigned short raw; int stp, r, g, b; } samples[8];
        int num_samples = 0;

        for (int v = vmin; v <= vmax; v++) {
            const unsigned short *row = vram + (((py + v) & 511) << 10);
            for (int u = umin; u <= umax; u++) {
                count_total++;
                unsigned short raw = 0;
                int idx = -1;
                if (fmt == 0) {
                    unsigned int word = row[(px + (u >> 2)) & 1023];
                    idx = (word >> ((u & 3) * 4)) & 15;
                    raw = vram[((cy & 511) << 10) + ((cx + idx) & 1023)];
                } else if (fmt == 1) {
                    unsigned int word = row[(px + (u >> 1)) & 1023];
                    idx = (word >> ((u & 1) * 8)) & 255;
                    raw = vram[((cy & 511) << 10) + ((cx + idx) & 1023)];
                } else {
                    raw = row[(px + u) & 1023];
                }

                if (raw == 0x0000) {
                    count_zero++;
                } else if (raw & 0x8000) {
                    count_stp1++;
                } else {
                    count_stp0++;
                }

                if (raw != 0x0000 && num_samples < 8) {
                    int r5 = raw & 31, g5 = (raw >> 5) & 31, b5 = (raw >> 10) & 31;
                    samples[num_samples].u = u;
                    samples[num_samples].v = v;
                    samples[num_samples].idx = idx;
                    samples[num_samples].raw = raw;
                    samples[num_samples].stp = (raw >> 15) & 1;
                    samples[num_samples].r = (r5 << 3) | (r5 >> 2);
                    samples[num_samples].g = (g5 << 3) | (g5 >> 2);
                    samples[num_samples].b = (b5 << 3) | (b5 >> 2);
                    num_samples++;
                }
            }
        }

        debugf("  stp_counts: total=%d stp1_trans=%d stp0_opaque=%d zero_trans=%d\n",
               count_total, count_stp1, count_stp0, count_zero);

        if (fmt == 0) {
            debugf("  clut_palette (16 entries at cx=%d, cy=%d):\n", cx, cy);
            for (int i = 0; i < 16; i++) {
                unsigned short pal_v = vram[((cy & 511) << 10) + ((cx + i) & 1023)];
                int stp = (pal_v >> 15) & 1;
                int r5 = pal_v & 31, g5 = (pal_v >> 5) & 31, b5 = (pal_v >> 10) & 31;
                int r8 = (r5 << 3) | (r5 >> 2), g8 = (g5 << 3) | (g5 >> 2), b8 = (b5 << 3) | (b5 >> 2);
                debugf("    [%02d] raw=0x%04X (stp=%d, r=%2d, g=%2d, b=%2d) -> rgb888=(%3d,%3d,%3d)\n",
                       i, pal_v, stp, r5, g5, b5, r8, g8, b8);
            }
        }

        debugf("  sample_texels (count=%d):\n", num_samples);
        for (int i = 0; i < num_samples; i++) {
            debugf("    [#%d] (u=%3d, v=%3d) idx=%2d raw=0x%04X stp=%d rgb888=(%3d,%3d,%3d)\n",
                   i, samples[i].u, samples[i].v, samples[i].idx, samples[i].raw, samples[i].stp,
                   samples[i].r, samples[i].g, samples[i].b);
        }

        if (semi) {
            if (count_stp0 > 0) {
                debugf("  STP_RULE_CHECK: VIOLACAO DA REGRA DO PS1! %d texels com STP=0 sao misturados com alpha no N64; no PS1 deveriam ser 100%% OPACOS!\n", count_stp0);
            } else {
                debugf("  STP_RULE_CHECK: Todos os texels nao-zero tem STP=1 (respeitado).\n");
            }
        }
    }

    const char *bm_names[] = { "BM_NONE", "BM_AVERAGE", "BM_ADD", "BM_SUBTRACT", "BM_ADD_QUATER_SOURCE" };
    const char *bm_str = (bm >= 0 && bm <= 4) ? bm_names[bm] : "UNKNOWN";
    debugf("  n64_backend:\n");
    debugf("    blend_mode=%d (%s)\n", bm, bm_str);

    if (us != NULL) {
        if (bm == 2 || bm == 4) { // BM_ADD or BM_ADD_QUATER_SOURCE
            debugf("    path=RGBA32 aditivo proporcional ao brilho (commit fa14399), sem TLUT na TMEM\n");
            debugf("    combiner=RDPQ_COMBINER1((TEX0, ZERO, SHADE, ZERO), (ZERO, ZERO, ZERO, TEX0))\n");
            debugf("    blender=RDPQ_BLENDER((IN_RGB, IN_ALPHA, MEMORY_RGB, INV_MUX_ALPHA))\n");
            debugf("    alpha_mode=texel_alpha (A = max(R,G,B)%s)\n", (bm == 4) ? " / 4" : "");
            debugf("    alphacompare=1 (on)\n");
        } else {
            int pa = (bm == 1) ? 128 : ((bm == 3) ? 110 : 0);
            debugf("    path=%s paletizado (TLUT_RGBA16 na TMEM)\n", fmt == 0 ? "CI4" : (fmt == 1 ? "CI8" : "RGBA16"));
            if (pa) {
                debugf("    combiner=RDPQ_COMBINER1((TEX0, ZERO, SHADE, ZERO), (TEX0, ZERO, PRIM, ZERO))\n");
                debugf("    blender=RDPQ_BLENDER((IN_RGB, IN_ALPHA, MEMORY_RGB, INV_MUX_ALPHA))\n");
                debugf("    alpha_mode=texel_alpha * prim_alpha (TEX0.A * %d)\n", pa);
            } else {
                debugf("    combiner=RDPQ_COMBINER_TEX_SHADE\n");
                debugf("    blender=0 (opaco)\n");
                debugf("    alpha_mode=nenhum\n");
            }
            debugf("    alphacompare=1 (on)\n");
        }
    } else {
        int pa = (bm == 1 || bm == 2) ? 128 : ((bm == 3) ? 110 : ((bm == 4) ? 64 : 0));
        debugf("    path=Poligono sem textura (Gouraud/Flat)\n");
        if (pa) {
            debugf("    combiner=RDPQ_COMBINER1((ZERO, ZERO, ZERO, SHADE), (ZERO, ZERO, ZERO, PRIM))\n");
            debugf("    blender=RDPQ_BLENDER((IN_RGB, IN_ALPHA, MEMORY_RGB, INV_MUX_ALPHA))\n");
            debugf("    alpha_mode=prim_alpha (pa=%d)\n", pa);
        } else {
            debugf("    combiner=RDPQ_COMBINER_SHADE\n");
            debugf("    blender=0 (opaco)\n");
            debugf("    alpha_mode=nenhum\n");
        }
        debugf("    alphacompare=0 (off)\n");
    }
    debugf("[TRANSDBG] ======================================================================\n\n");
}

#endif // D2_TRANSDBG

#endif // D2_ORIGIN

