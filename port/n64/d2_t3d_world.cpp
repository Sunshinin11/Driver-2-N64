#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>
#include <stdlib.h>
#include <math.h>

#include "d2_t3d_world.h"

#ifdef D2_T3D_WORLD

#ifndef D2_T3D_LIVE_TEXTURES
#define D2_T3D_LIVE_TEXTURES 1
#endif

#include "driver2.h"
#include "C/models.h"
#include "engine/mdl.h"
#include "C/draw.h"
#include "C/main.h"
#include "C/texture.h"
#include "d2_xf.h"
#include "d2_vram.h"
#ifdef ONE
#undef ONE
#endif

extern "C" {
    extern TEXINF* tpage_ids[128];
    extern int tpage_amount;
}
extern XYPAIR *permlist;

// Static arenas in .bss (never from heap)
#define D2_T3D_MAX_VERTS        9216
#define D2_T3D_MAX_QUEUED_TILES 192
#define D2_T3D_MAX_QUEUED_BLDS  96
#define D2_T3D_MAX_MATS         (D2_T3D_MAX_QUEUED_TILES + D2_T3D_MAX_QUEUED_BLDS)
#define T3D_VERT_WINDOW         32

// Vertices for all converted world models (9216 verts = 4608 packed structs = 144 KB)
static T3DVertPacked s_t3d_vtx_pool[D2_T3D_MAX_VERTS / 2] __attribute__((aligned(16)));
static int s_t3d_vtx_used = 0;

// Model blocks (buildings + ground/road tiles)
static rspq_block_t* s_t3d_model_blocks[MAX_MODEL_SLOTS];
static bool s_t3d_live_model[MAX_MODEL_SLOTS];
static unsigned char s_t3d_tile_ofs[MAX_MODEL_SLOTS]; // 229 = water/grass, 133 = road/pavement, 0 = building

#if defined(D2_T3D_STATS) || defined(D2_ORIGIN_PROF)
static unsigned short s_t3d_model_triangles[MAX_MODEL_SLOTS];
#endif
#ifdef D2_T3D_STATS
static unsigned s_stat_candidates, s_stat_native, s_stat_queued, s_stat_triangles;
static unsigned s_stat_ticks, s_stat_frames;
static uint32_t t3d_stat_clock(void) {
    uint32_t ticks;
    __asm__ volatile("mfc0 %0,$9" : "=r"(ticks));
    return ticks;
}
static void t3d_stat_frame(uint32_t ticks) {
    s_stat_ticks += ticks;
    if (++s_stat_frames < 30) return;
    extern int CameraCnt;
    debugf("[T3D_WORK] cc=%d frames=%u candidates=%u native=%u legacy=%u queued=%u triangles=%u submit_ticks=%u\n",
           CameraCnt, s_stat_frames, s_stat_candidates, s_stat_native,
           s_stat_candidates - s_stat_native, s_stat_queued, s_stat_triangles, s_stat_ticks);
    s_stat_candidates = s_stat_native = s_stat_queued = s_stat_triangles = 0;
    s_stat_ticks = s_stat_frames = 0;
}
#endif

// Matrix pool for queued instances (double buffered: 2 frames x 448 instances x 64B = 56 KB)
static T3DMat4FP s_t3d_mat_pool[2][D2_T3D_MAX_MATS] __attribute__((aligned(16)));

// Viewport matrix buffers (2 frames x 2 matrices x 64B = 256 bytes)
static T3DMat4FP s_t3d_vp_matFP[2][2] __attribute__((aligned(16)));

static T3DViewport s_t3d_viewport;
static bool s_t3d_inited = false;
static int s_t3d_models_converted = 0;
static int s_t3d_tiles_converted = 0;
static int s_t3d_blds_converted = 0;

// --------------------------------------------------------------------------
// RESOLVEDOR AUTOMÁTICO DE MATERIAIS NATIVOS DO REDRIVER2 (CI4)
// --------------------------------------------------------------------------
struct D2MaterialDesc {
    bool valid;
    int tset;
    int tid;
    uint8_t x;
    uint8_t y;
    uint8_t width;
    uint8_t height;
    unsigned short tpage;
    unsigned short clut;
    sprite_t* sp; // Sprite CI4 carregado (se disponível e suportado na TMEM)
};

struct D2SpriteCacheEntry {
    int tset;
    int tid;
    sprite_t* sp;
};
#define D2_MAX_SPRITE_CACHE 16
static D2SpriteCacheEntry s_sprite_cache[D2_MAX_SPRITE_CACHE];
static int s_sprite_cache_count = 0;

static sprite_t* d2_get_material_sprite(int tset, int tid, int width, int height)
{
    // Limite de TMEM para CI4 em passe único: 4096 texels (2048 bytes + TLUT de 16 cores)
    if (width <= 0 || height <= 0 || (width * height) > 4096 || width > 64 || height > 64) {
        return NULL;
    }

    for (int i = 0; i < s_sprite_cache_count; i++) {
        if (s_sprite_cache[i].tset == tset && s_sprite_cache[i].tid == tid) {
            return s_sprite_cache[i].sp;
        }
    }

    if (s_sprite_cache_count >= D2_MAX_SPRITE_CACHE) return NULL;

    // Chave de material por (tset, tid) - eliminação de dependência de nome do modelo
    char path[64];
    snprintf(path, sizeof(path), "rom:/TEX/tex_s%d_t%d.sprite", tset, tid);

    int fd = dfs_open(path + 5); // skip "rom:/"
    if (fd < 0) {
        s_sprite_cache[s_sprite_cache_count++] = { tset, tid, NULL };
        return NULL;
    }
    dfs_close(fd);

    sprite_t* sp = sprite_load(path);
    if (sp) {
        debugf("[T3D_TEX] Material (s=%d, t=%d) carregado %s (%dx%d)\n", tset, tid, path, sp->width, sp->height);
    }
    s_sprite_cache[s_sprite_cache_count++] = { tset, tid, sp };
    return sp;
}

static bool d2_resolve_material(int tset, int tid, D2MaterialDesc* out_desc)
{
    if (!out_desc) return false;
    memset(out_desc, 0, sizeof(D2MaterialDesc));

    // 1 & 2. Verificação dos limites: página existente, tset válido e tid menor que tpage_texamts[tset]
    if (tset < 0 || tset >= 128 || tset >= tpage_amount) return false;
    if (tpage_ids[tset] == NULL) return false;
    if (tid < 0 || tid >= tpage_texamts[tset]) return false;

    // 3 & 4. Consultar tpage_ids[tset][tid] e obter x, y, width, height
    const TEXINF& info = tpage_ids[tset][tid];
    out_desc->valid = true;
    out_desc->tset = tset;
    out_desc->tid = tid;
    out_desc->x = info.x;
    out_desc->y = info.y;
    out_desc->width = info.width;
    out_desc->height = info.height;

    // 5. Obter identificação da página e da CLUT pelas tabelas originais
    out_desc->tpage = texture_pages[tset];
    out_desc->clut = (tid < 32) ? texture_cluts[tset][tid] : 0;

    // 6. Consultar se há sprite disponível e compatível com TMEM
    out_desc->sp = d2_get_material_sprite(tset, tid, info.width, info.height);

    return true;
}

static void d2_t3d_reset_materials(void)
{
    // Libera associações físicas de materiais ao trocar de região/nível
    for (int i = 0; i < s_sprite_cache_count; i++) {
        if (s_sprite_cache[i].sp) {
            sprite_free(s_sprite_cache[i].sp);
            s_sprite_cache[i].sp = NULL;
        }
    }
    s_sprite_cache_count = 0;
}

typedef struct {
    int model_idx;
    int yang;
    int z;
    int tx, ty, tz;
} D2QueuedInstance;

static D2QueuedInstance s_t3d_tile_queue[D2_T3D_MAX_QUEUED_TILES];
static int s_t3d_tile_queue_count = 0;

static D2QueuedInstance s_t3d_bld_queue[D2_T3D_MAX_QUEUED_BLDS];
static int s_t3d_bld_queue_count = 0;

static color_t s_tile_tint = { 255, 255, 255, 255 };

extern "C" void d2_rdp_invalidate_backend_cache(void);
extern "C" int d2_rdp_frame_count(void);
extern "C" surface_t* d2_rdp_get_current_fb(void);

static bool is_perm_tpage(int tset)
{
    if (!permlist || nperms <= 0 || tset < 0 || tset >= 128) return false;
    for (int i = 0; i < nperms; i++) {
        if (permlist[i].x == tset) return true;
    }
    return false;
}

// Samples the average opaque 15-bit PS1 texel color inside (uv0..uv3) from VRAM + CLUT.
static uint32_t sample_poly_color(int tset, int tid,
                                  UV_INFO uv0, UV_INFO uv1, UV_INFO uv2, UV_INFO uv3,
                                  bool is_tri, uint32_t fallback_rgba)
{
    if (!is_perm_tpage(tset)) {
        return fallback_rgba;
    }

    unsigned short tpage_val = texture_pages[tset];
    unsigned short clut_val  = (tid >= 0 && tid < 32) ? texture_cluts[tset][tid] : texture_cluts[tset][0];

    const int px = (tpage_val & 15) * 64;
    const int py = ((tpage_val >> 4) & 1) * 256;
    const int cx = (clut_val & 63) * 16;
    const int cy = (clut_val >> 6) & 511;

    uint32_t sum_r = 0, sum_g = 0, sum_b = 0, cnt = 0;

    if (is_tri) {
        static const float bcoords[6][2] = {
            { 0.25f, 0.25f }, { 0.55f, 0.20f }, { 0.20f, 0.55f },
            { 0.33f, 0.33f }, { 0.15f, 0.15f }, { 0.42f, 0.42f }
        };
        for (int k = 0; k < 6; k++) {
            float a = bcoords[k][0], b = bcoords[k][1];
            float c = 1.0f - a - b;
            int u = (int)(c * uv0.u + a * uv1.u + b * uv2.u + 0.5f);
            int v = (int)(c * uv0.v + a * uv1.v + b * uv2.v + 0.5f);
            if (u < 0) u = 0; else if (u > 255) u = 255;
            if (v < 0) v = 0; else if (v > 255) v = 255;

            unsigned short word = d2_vram_rd(px + (u >> 2), py + v);
            int idx = (word >> ((u & 3) * 4)) & 15;
            unsigned short c16 = d2_vram_rd(cx + idx, cy);
            if (c16 != 0) {
                int r5 = c16 & 31, g5 = (c16 >> 5) & 31, b5 = (c16 >> 10) & 31;
                sum_r += (r5 << 3) | (r5 >> 2);
                sum_g += (g5 << 3) | (g5 >> 2);
                sum_b += (b5 << 3) | (b5 >> 2);
                cnt++;
            }
        }
    } else {
        for (int gy = 0; gy < 4; gy++) {
            float fy = (gy * 2 + 1) * 0.125f;
            float ify = 1.0f - fy;
            for (int gx = 0; gx < 4; gx++) {
                float fx = (gx * 2 + 1) * 0.125f;
                float ifx = 1.0f - fx;
                int u = (int)((ifx * ify) * uv0.u + (fx * ify) * uv1.u + (fx * fy) * uv2.u + (ifx * fy) * uv3.u + 0.5f);
                int v = (int)((ifx * ify) * uv0.v + (fx * ify) * uv1.v + (fx * fy) * uv2.v + (ifx * fy) * uv3.v + 0.5f);
                if (u < 0) u = 0; else if (u > 255) u = 255;
                if (v < 0) v = 0; else if (v > 255) v = 255;

                unsigned short word = d2_vram_rd(px + (u >> 2), py + v);
                int idx = (word >> ((u & 3) * 4)) & 15;
                unsigned short c16 = d2_vram_rd(cx + idx, cy);
                if (c16 != 0) {
                    int r5 = c16 & 31, g5 = (c16 >> 5) & 31, b5 = (c16 >> 10) & 31;
                    sum_r += (r5 << 3) | (r5 >> 2);
                    sum_g += (g5 << 3) | (g5 >> 2);
                    sum_b += (b5 << 3) | (b5 >> 2);
                    cnt++;
                }
            }
        }
    }

    if (cnt == 0) return fallback_rgba;
    uint32_t r = sum_r / cnt;
    uint32_t g = sum_g / cnt;
    uint32_t b = sum_b / cnt;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return (r << 24) | (g << 16) | (b << 8) | 0xFFu;
}

static uint32_t apply_wall_contrast(uint32_t rgba, const SVECTOR& a, const SVECTOR& b, const SVECTOR& c)
{
    int32_t e1x = (int32_t)b.vx - a.vx, e1y = (int32_t)b.vy - a.vy, e1z = (int32_t)b.vz - a.vz;
    int32_t e2x = (int32_t)c.vx - a.vx, e2y = (int32_t)c.vy - a.vy, e2z = (int32_t)c.vz - a.vz;
    int64_t nx = (int64_t)e1y * e2z - (int64_t)e1z * e2y;
    int64_t ny = (int64_t)e1z * e2x - (int64_t)e1x * e2z;
    int64_t nz = (int64_t)e1x * e2y - (int64_t)e1y * e2x;
    int64_t anx = nx < 0 ? -nx : nx;
    int64_t any = ny < 0 ? -ny : ny;
    int64_t anz = nz < 0 ? -nz : nz;

    int mul = 256; // 1.00
    if (any > anx && any > anz) {
        mul = 256; // roof / horizontal top
    } else if (anx >= anz) {
        mul = (nx >= 0) ? 242 : 206; // East vs West wall (~0.95 vs ~0.80)
    } else {
        mul = (nz >= 0) ? 226 : 192; // North vs South wall (~0.88 vs ~0.75)
    }

    uint32_t r = (((rgba >> 24) & 0xFFu) * (uint32_t)mul) >> 8;
    uint32_t g = (((rgba >> 16) & 0xFFu) * (uint32_t)mul) >> 8;
    uint32_t bl = (((rgba >> 8) & 0xFFu) * (uint32_t)mul) >> 8;
    return (r << 24) | (g << 16) | (bl << 8) | 0xFFu;
}

static bool extract_poly_info(const unsigned char* p, int ptype,
                              int* v0, int* v1, int* v2, int* v3, bool* is_tri,
                              int* tset, int* tid,
                              UV_INFO* uv0, UV_INFO* uv1, UV_INFO* uv2, UV_INFO* uv3)
{
    *tset = p[1];
    *tid  = p[2];
    if (ptype == 11 || ptype == 21 || ptype == 19 || ptype == 4 || ptype == 5) {
        const PL_POLYFT4* q = (const PL_POLYFT4*)p;
        *v0 = q->v0; *v1 = q->v1; *v2 = q->v2; *v3 = q->v3;
        *uv0 = q->uv0; *uv1 = q->uv1; *uv2 = q->uv2; *uv3 = q->uv3;
        *is_tri = false;
        return true;
    }
    if (ptype == 23) {
        const POLYGT4* q = (const POLYGT4*)p;
        *v0 = q->v0; *v1 = q->v1; *v2 = q->v2; *v3 = q->v3;
        *uv0 = q->uv0; *uv1 = q->uv1; *uv2 = q->uv2; *uv3 = q->uv3;
        *is_tri = false;
        return true;
    }
    if (ptype == 10 || ptype == 20) {
        const POLYFT3* t = (const POLYFT3*)p;
        *v0 = t->v0; *v1 = t->v1; *v2 = t->v2; *v3 = t->v2;
        *uv0 = t->uv0; *uv1 = t->uv1; *uv2 = t->uv2; *uv3 = t->uv2;
        *is_tri = true;
        return true;
    }
    if (ptype == 22) {
        const POLYGT3* t = (const POLYGT3*)p;
        *v0 = t->v0; *v1 = t->v1; *v2 = t->v2; *v3 = t->v2;
        *uv0 = t->uv0; *uv1 = t->uv1; *uv2 = t->uv2; *uv3 = t->uv2;
        *is_tri = true;
        return true;
    }
    return false;
}

#include "d2_t3d_live_textures.inc"

void d2_t3d_init(void)
{
    if (s_t3d_inited) return;
    s_t3d_inited = true;

    t3d_init((T3DInitParams){});

    s_t3d_viewport = t3d_viewport_create();
    s_t3d_viewport._bufferCount = 2;
    s_t3d_viewport._bufferIdx = 0;
    s_t3d_viewport._matFP = (T3DMat4FP*)UncachedAddr(s_t3d_vp_matFP);

    debugf("[T3D] Initialized Tiny3D world renderer. Static vtx pool: %d bytes (%d KB), mat pool: %d bytes (%d KB)\n",
           (int)sizeof(s_t3d_vtx_pool), (int)sizeof(s_t3d_vtx_pool) / 1024,
           (int)sizeof(s_t3d_mat_pool), (int)sizeof(s_t3d_mat_pool) / 1024);
}

struct ConvertedPoly {
    SVECTOR pos[4];
    int16_t st[4][2];
    uint32_t col[4];
    bool is_tri;
    D2MaterialDesc mat;
    int pass_type; // 0 = untextured/shaded, 1 = textured CI4
};

void d2_t3d_on_models_loaded(void)
{
    if (!s_t3d_inited) {
        d2_t3d_init();
    }
    // Ensure RSP is idle before freeing any previous level's blocks
    rspq_wait();
    d2_t3d_reset_materials();
    memset(s_t3d_live_model, 0, sizeof(s_t3d_live_model));

    for (int i = 0; i < MAX_MODEL_SLOTS; i++) {
        if (s_t3d_model_blocks[i]) {
            rspq_block_free(s_t3d_model_blocks[i]);
            s_t3d_model_blocks[i] = NULL;
        }
        s_t3d_tile_ofs[i] = 0;
#if defined(D2_T3D_STATS) || defined(D2_ORIGIN_PROF)
        s_t3d_model_triangles[i] = 0;
#endif
    }

    s_t3d_vtx_used = 0;
    s_t3d_models_converted = 0;
    s_t3d_tiles_converted = 0;
    s_t3d_blds_converted = 0;
    s_t3d_tile_queue_count = 0;
    s_t3d_bld_queue_count = 0;

    heap_stats_t hs_before, hs_after;
    sys_get_heap_stats(&hs_before);
    debugf("[T3D_MEM] === INICIO CONVERSAO MODELOS === Heap livre: %d bytes (usado: %d)\n",
           hs_before.free, hs_before.used);

    int count = num_models_in_pack;
    if (count > MAX_MODEL_SLOTS) count = MAX_MODEL_SLOTS;

    // Identify all ground/water/grass/path tile models (including Low2High/Low2Lower LOD targets)
    static bool is_tile_slot[MAX_MODEL_SLOTS];
    memset(is_tile_slot, 0, sizeof(is_tile_slot));
    for (int mid = 0; mid < count; mid++) {
        MODEL* m = modelpointers[mid];
        if (!m || m == &dummyModel) continue;
        if (m->shape_flags & SHAPE_FLAG_SPRITE) continue;
        if ((m->shape_flags & (SHAPE_FLAG_WATER | SHAPE_FLAG_TILE)) ||
            (m->flags2 & (MODEL_FLAG_PATH | MODEL_FLAG_GRASS))) {
            is_tile_slot[mid] = true;
            if (Low2HighDetailTable && Low2HighDetailTable[mid] < count)
                is_tile_slot[Low2HighDetailTable[mid]] = true;
            if (Low2LowerDetailTable && Low2LowerDetailTable[mid] < count)
                is_tile_slot[Low2LowerDetailTable[mid]] = true;
        }
    }

    struct FaceInfo {
        int v[4];
        bool is_tri;
        int sort_ofs;
        int tset;
        int tid;
        uint32_t col;
        UV_INFO uv[4];
    };

    static FaceInfo faces[160];
    static ConvertedPoly s_poly_buffer[160];

    // Pass 0: Convert ground tile models FIRST (small vertex footprint, #1 frame cost)
    // Pass 1: Convert static building & road-furniture models SECOND
    for (int pass = 0; pass < 2; pass++) {
        for (int mid = 0; mid < count; mid++) {
            const bool is_tile = is_tile_slot[mid];
            if ((pass == 0 && !is_tile) || (pass == 1 && is_tile)) continue;

            MODEL* model = modelpointers[mid];
            if (!model || model == &dummyModel || model->num_polys == 0) continue;

            MODEL* vtx_model = (model->instance_number != -1 &&
                                model->instance_number >= 0 &&
                                model->instance_number < count &&
                                modelpointers[model->instance_number] != &dummyModel)
                               ? modelpointers[model->instance_number]
                               : model;
            int nverts = vtx_model->num_vertices;
            if (nverts <= 0) continue;

            if (!is_tile) {
                // Disqualify sprites, alpha/transparent fences, animated objects, and small breakable/street props
                if (model->shape_flags & (SHAPE_FLAG_SPRITE | SHAPE_FLAG_TRANS | SHAPE_FLAG_ALPHA)) {
                    continue;
                }
                if (model->flags2 & (MODEL_FLAG_ANIMOBJ | MODEL_FLAG_CHAIR | MODEL_FLAG_BARRIER |
                                     MODEL_FLAG_SMASHABLE | MODEL_FLAG_LAMP | MODEL_FLAG_TREE)) {
                    continue;
                }
            }

            SVECTOR* src_verts = GET_MODEL_DATA(SVECTOR, model, vertices);
            unsigned char* pblock = (unsigned char*)GET_MODEL_DATA(unsigned char, model, poly_block);

#if D2_T3D_LIVE_TEXTURES
            // Keep original UVs/material IDs and render with current region pages.
            // The existing vertex arena becomes a bounded double-buffered stream.
            bool supported_model = model->num_polys > 0;
            const unsigned char* check = pblock;
            for (int face = 0; face < model->num_polys && supported_model; face++) {
                int type = check[0] & 31, ids[4], tset, tid;
                bool triangle;
                UV_INFO uv[4];
                int size = PolySizes[type];
                supported_model = size > 0 && extract_poly_info(check, type,
                    &ids[0], &ids[1], &ids[2], &ids[3], &triangle, &tset, &tid,
                    &uv[0], &uv[1], &uv[2], &uv[3]);
                if (supported_model) {
                    supported_model = tset < 128 && tid < 32;
                    for (int k = 0; k < (triangle ? 3 : 4); k++)
                        if (ids[k] >= nverts) supported_model = false;
                }
                check += size;
            }
            // Unsupported primitives retain the original textured renderer.
            if (!supported_model) continue;
            s_t3d_live_model[mid] = true;
            s_t3d_tile_ofs[mid] = is_tile ? ((model->shape_flags & SHAPE_FLAG_WATER) || (model->flags2 & MODEL_FLAG_GRASS) ? 229 : 133) : 0;
            s_t3d_models_converted++;
            if (is_tile) s_t3d_tiles_converted++; else s_t3d_blds_converted++;
            continue;
#endif

            int num_faces = 0;

            const bool is_water_or_grass = ((model->shape_flags & SHAPE_FLAG_WATER) || (model->flags2 & MODEL_FLAG_GRASS)) != 0;
            const int base_ofse = is_water_or_grass ? 229 : 133;
            const int ofse_lut[8] = { base_ofse, base_ofse, 0, 75, 133, base_ofse, base_ofse, base_ofse };
            uint32_t tileTypes = is_tile ? (d2_rd32(model + 1) >> 2) : 0;
            int ttype = 0;

            uint32_t default_col = 0xCCCCCCFFu;
            if (model->shape_flags & SHAPE_FLAG_WATER) default_col = 0x386088FFu;
            else if (model->flags2 & MODEL_FLAG_GRASS) default_col = 0x4C783CFFu;
            else if (is_tile || (model->shape_flags & SHAPE_FLAG_ROAD)) default_col = 0x585A5EFFu;

            int poffs = 0;
            for (int p = 0; p < model->num_polys; p++) {
                int ptype = pblock[poffs] & 31;
                int psize = PolySizes[ptype];
                if (psize == 0) break;

                int cur_ttype = ttype;
                if (is_tile) {
                    ttype = tileTypes & 7;
                    tileTypes >>= 3;
                }

                int v0, v1, v2, v3, tset, tid;
                bool is_tri = false;
                UV_INFO uv0, uv1, uv2, uv3;
                if (num_faces < 160 &&
                    extract_poly_info(&pblock[poffs], ptype, &v0, &v1, &v2, &v3, &is_tri,
                                      &tset, &tid, &uv0, &uv1, &uv2, &uv3)) {
                    if (v0 < nverts && v1 < nverts && v2 < nverts && (is_tri || v3 < nverts)) {
                        uint32_t fallback = default_col;
                        if (!is_tile && ptype == 21) fallback = 0xB8A088FFu;
                        else if (!is_tile && ptype == 23) fallback = 0x8090A0FFu;

                        uint32_t col = sample_poly_color(tset, tid, uv0, uv1, uv2, uv3, is_tri, fallback);
                        if (!is_tile) {
                            col = apply_wall_contrast(col, src_verts[v0], src_verts[v1], src_verts[is_tri ? v2 : v3]);
                        }
                        FaceInfo& fi = faces[num_faces++];
                        fi.v[0] = v0; fi.v[1] = v1; fi.v[2] = v2; fi.v[3] = v3;
                        fi.is_tri = is_tri;
                        fi.sort_ofs = is_tile ? ofse_lut[cur_ttype & 7] : 0;
                        fi.tset = tset;
                        fi.tid = tid;
                        fi.col = col;
                        fi.uv[0] = uv0; fi.uv[1] = uv1; fi.uv[2] = uv2; fi.uv[3] = uv3;
                    }
                }
                poffs += psize;
            }

            if (num_faces == 0) continue;

            // Stable insertion sort on tile faces by sort_ofs descending (229 -> 133 -> 75 -> 0)
            if (is_tile && num_faces > 1) {
                for (int i = 1; i < num_faces; i++) {
                    FaceInfo key = faces[i];
                    int j = i - 1;
                    while (j >= 0 && faces[j].sort_ofs < key.sort_ofs) {
                        faces[j + 1] = faces[j];
                        j--;
                    }
                    faces[j + 1] = key;
                }
            }

            // Escopo exclusivo CI4 64x64 nesta etapa: fachada principal do Model 234 (Chicago Missao 1)
            const bool is_target_tex_model = (mid == 234);

            // Compute smooth per-vertex color for untextured buildings to share vertices (3x savings)
            uint32_t bld_vcol[512];
            const bool use_bld_vcol = (!is_tile && nverts <= 512 && !is_target_tex_model);
            if (use_bld_vcol) {
                uint16_t vr[512] = {}, vg[512] = {}, vb[512] = {}, vc[512] = {};
                for (int f = 0; f < num_faces; f++) {
                    const int nc = faces[f].is_tri ? 3 : 4;
                    uint32_t c = faces[f].col;
                    uint16_t r = (uint16_t)((c >> 24) & 0xFF);
                    uint16_t g = (uint16_t)((c >> 16) & 0xFF);
                    uint16_t b = (uint16_t)((c >> 8) & 0xFF);
                    for (int k = 0; k < nc; k++) {
                        int vi = faces[f].v[k];
                        vr[vi] += r; vg[vi] += g; vb[vi] += b; vc[vi]++;
                    }
                }
                for (int vi = 0; vi < nverts; vi++) {
                    if (vc[vi] > 0) {
                        bld_vcol[vi] = ((uint32_t)(vr[vi] / vc[vi]) << 24) |
                                       ((uint32_t)(vg[vi] / vc[vi]) << 16) |
                                       ((uint32_t)(vb[vi] / vc[vi]) << 8)  | 0xFFu;
                    } else {
                        bld_vcol[vi] = default_col;
                    }
                }
            }

            int poly_count = 0;
            if (is_target_tex_model) {
                // Processamento de polígonos com resolvedor automático de materiais para cada face
                for (int f = 0; f < num_faces; f++) {
                    if (poly_count >= 150) break;
                    const int nc = faces[f].is_tri ? 3 : 4;

                    D2MaterialDesc mat_desc;
                    bool resolved = d2_resolve_material(faces[f].tset, faces[f].tid, &mat_desc);

                    bool uv_valid = false;
                    if (resolved && mat_desc.sp != NULL) {
                        uv_valid = true;
                        for (int k = 0; k < nc; k++) {
                            int u = faces[f].uv[k].u;
                            int v = faces[f].uv[k].v;
                            if (u < mat_desc.x || u > (mat_desc.x + mat_desc.width) ||
                                v < mat_desc.y || v > (mat_desc.y + mat_desc.height)) {
                                uv_valid = false;
                                break;
                            }
                        }
                    }

                    if (resolved && mat_desc.sp != NULL && uv_valid) {
                        ConvertedPoly& cp = s_poly_buffer[poly_count++];
                        cp.mat = mat_desc;
                        cp.pass_type = 1; // CI4 texturizado
                        cp.is_tri = faces[f].is_tri;

                        uint32_t face_contrast_col = apply_wall_contrast(0xFFFFFFFFu,
                                                                         src_verts[faces[f].v[0]],
                                                                         src_verts[faces[f].v[1]],
                                                                         src_verts[faces[f].is_tri ? faces[f].v[2] : faces[f].v[3]]);

                        for (int k = 0; k < nc; k++) {
                            cp.pos[k] = src_verts[faces[f].v[k]];
                            int local_u = (int)faces[f].uv[k].u - mat_desc.x;
                            int local_v = (int)faces[f].uv[k].v - mat_desc.y;
                            cp.st[k][0] = (int16_t)(local_u << 5);
                            cp.st[k][1] = (int16_t)(local_v << 5);
                            cp.col[k] = face_contrast_col;
                        }
                    } else {
                        ConvertedPoly& cp = s_poly_buffer[poly_count++];
                        memset(&cp.mat, 0, sizeof(D2MaterialDesc));
                        cp.pass_type = 0; // fallback sólido untextured
                        cp.is_tri = faces[f].is_tri;
                        for (int k = 0; k < nc; k++) {
                            int vi = faces[f].v[k];
                            cp.pos[k] = src_verts[vi];
                            cp.st[k][0] = 0;
                            cp.st[k][1] = 0;
                            cp.col[k] = faces[f].col;
                        }
                    }
                }
            }

            rspq_block_begin();

            SVECTOR local_vtx_pos[T3D_VERT_WINDOW];
            uint32_t local_vtx_col[T3D_VERT_WINDOW];
            int16_t local_vtx_st[T3D_VERT_WINDOW][2];
            int local_vtx_orig[T3D_VERT_WINDOW];
            int local_vtx_count = 0;

            struct LocalFace {
                int lv0, lv1, lv2, lv3;
                bool is_tri;
            };
            LocalFace batch_faces[160];
            int batch_face_count = 0;
            bool model_overflow = false;
            int total_tris = 0;

            auto flush_window = [&]() {
                if (local_vtx_count == 0) return;
                int aligned_batch = (local_vtx_count + 1) & ~1;
                if (s_t3d_vtx_used + aligned_batch > D2_T3D_MAX_VERTS) {
                    model_overflow = true;
                    return;
                }
                int batch_start = s_t3d_vtx_used;
                for (int k = 0; k < local_vtx_count; k++) {
                    const SVECTOR& p = local_vtx_pos[k];
                    uint32_t col = local_vtx_col[k];
                    int vidx = batch_start + k;
                    int packed_idx = vidx / 2;
                    if (!(vidx & 1)) {
                        s_t3d_vtx_pool[packed_idx].posA[0] = p.vx;
                        s_t3d_vtx_pool[packed_idx].posA[1] = p.vy;
                        s_t3d_vtx_pool[packed_idx].posA[2] = p.vz;
                        s_t3d_vtx_pool[packed_idx].rgbaA = col;
                        s_t3d_vtx_pool[packed_idx].normA = 0;
                        s_t3d_vtx_pool[packed_idx].stA[0] = local_vtx_st[k][0];
                        s_t3d_vtx_pool[packed_idx].stA[1] = local_vtx_st[k][1];
                    } else {
                        s_t3d_vtx_pool[packed_idx].posB[0] = p.vx;
                        s_t3d_vtx_pool[packed_idx].posB[1] = p.vy;
                        s_t3d_vtx_pool[packed_idx].posB[2] = p.vz;
                        s_t3d_vtx_pool[packed_idx].rgbaB = col;
                        s_t3d_vtx_pool[packed_idx].normB = 0;
                        s_t3d_vtx_pool[packed_idx].stB[0] = local_vtx_st[k][0];
                        s_t3d_vtx_pool[packed_idx].stB[1] = local_vtx_st[k][1];
                    }
                }
                if (local_vtx_count & 1) {
                    int vidx = batch_start + local_vtx_count;
                    int packed_idx = vidx / 2;
                    s_t3d_vtx_pool[packed_idx].posB[0] = 0;
                    s_t3d_vtx_pool[packed_idx].posB[1] = 0;
                    s_t3d_vtx_pool[packed_idx].posB[2] = 0;
                    s_t3d_vtx_pool[packed_idx].rgbaB = 0;
                    s_t3d_vtx_pool[packed_idx].normB = 0;
                    s_t3d_vtx_pool[packed_idx].stB[0] = 0;
                    s_t3d_vtx_pool[packed_idx].stB[1] = 0;
                }
                data_cache_hit_writeback(&s_t3d_vtx_pool[batch_start / 2], (aligned_batch / 2) * sizeof(T3DVertPacked));

                t3d_vert_load(&s_t3d_vtx_pool[batch_start / 2], 0, aligned_batch);
                for (int q = 0; q < batch_face_count; q++) {
                    if (batch_faces[q].is_tri) {
                        t3d_tri_draw(batch_faces[q].lv0, batch_faces[q].lv1, batch_faces[q].lv2);
                    } else {
                        t3d_tri_draw(batch_faces[q].lv0, batch_faces[q].lv1, batch_faces[q].lv3);
                        t3d_tri_draw(batch_faces[q].lv1, batch_faces[q].lv2, batch_faces[q].lv3);
                    }
                }
                t3d_tri_sync();
                s_t3d_vtx_used += aligned_batch;

                local_vtx_count = 0;
                batch_face_count = 0;
            };

            auto find_local_vtx_shared = [&](int orig, uint32_t col) -> int {
                for (int i = 0; i < local_vtx_count; i++) {
                    if (local_vtx_orig[i] == orig && local_vtx_col[i] == col) return i;
                }
                return -1;
            };

            auto find_local_vtx_pos = [&](const SVECTOR& pos, uint32_t col, int16_t s, int16_t t) -> int {
                for (int i = 0; i < local_vtx_count; i++) {
                    if (local_vtx_pos[i].vx == pos.vx &&
                        local_vtx_pos[i].vy == pos.vy &&
                        local_vtx_pos[i].vz == pos.vz &&
                        local_vtx_col[i] == col &&
                        local_vtx_st[i][0] == s &&
                        local_vtx_st[i][1] == t) return i;
                }
                return -1;
            };

            auto emit_poly = [&](const SVECTOR pos[4], const int16_t st[4][2], const uint32_t cols[4], bool is_tri) {
                const int ncorner = is_tri ? 3 : 4;
                int new_needed = 0;
                for (int k = 0; k < ncorner; k++) {
                    if (find_local_vtx_pos(pos[k], cols[k], st[k][0], st[k][1]) == -1) new_needed++;
                }
                if (local_vtx_count + new_needed > T3D_VERT_WINDOW) {
                    flush_window();
                    if (model_overflow) return;
                }
                int lv[4] = { 0, 0, 0, 0 };
                for (int k = 0; k < ncorner; k++) {
                    int found = find_local_vtx_pos(pos[k], cols[k], st[k][0], st[k][1]);
                    if (found != -1) {
                        lv[k] = found;
                    } else {
                        int lidx = local_vtx_count++;
                        local_vtx_pos[lidx] = pos[k];
                        local_vtx_col[lidx] = cols[k];
                        local_vtx_st[lidx][0] = st[k][0];
                        local_vtx_st[lidx][1] = st[k][1];
                        local_vtx_orig[lidx] = -1;
                        lv[k] = lidx;
                    }
                }
                batch_faces[batch_face_count++] = { lv[0], lv[1], lv[2], lv[3], is_tri };
                total_tris += is_tri ? 1 : 2;
            };

            int active_sprite_count = 0;
            if (!is_target_tex_model) {
                // CAMINHO ULTRALEVE E COMPACTO (419 modelos normais: 1 único flush_window, ~350 bytes/bloco)
                for (int f = 0; f < num_faces; f++) {
                    const int ncorner = faces[f].is_tri ? 3 : 4;
                    int new_needed = 0;
                    for (int k = 0; k < ncorner; k++) {
                        int orig = faces[f].v[k];
                        uint32_t vcol = use_bld_vcol ? bld_vcol[orig] : faces[f].col;
                        if (find_local_vtx_shared(orig, vcol) == -1) new_needed++;
                    }
                    if (local_vtx_count + new_needed > T3D_VERT_WINDOW) {
                        flush_window();
                        if (model_overflow) break;
                    }
                    int lv[4] = { 0, 0, 0, 0 };
                    for (int k = 0; k < ncorner; k++) {
                        int orig = faces[f].v[k];
                        uint32_t vcol = use_bld_vcol ? bld_vcol[orig] : faces[f].col;
                        int found = find_local_vtx_shared(orig, vcol);
                        if (found != -1) {
                            lv[k] = found;
                        } else {
                            int lidx = local_vtx_count++;
                            local_vtx_pos[lidx] = src_verts[orig];
                            local_vtx_col[lidx] = vcol;
                            local_vtx_st[lidx][0] = 0;
                            local_vtx_st[lidx][1] = 0;
                            local_vtx_orig[lidx] = orig;
                            lv[k] = lidx;
                        }
                    }
                    batch_faces[batch_face_count++] = { lv[0], lv[1], lv[2], lv[3], faces[f].is_tri };
                    total_tris += faces[f].is_tri ? 1 : 2;
                }
                if (!model_overflow) flush_window();
            } else {
                // CAMINHO TEXTURIZADO CI4 NATIVO RESOLVIDO POR MATERIAL
                bool textured_state_active = false;

                // 1. Identificar sprites/materiais distintos usados nas faces texturizadas deste modelo
                sprite_t* active_sprites[8];
                active_sprite_count = 0;
                for (int i = 0; i < poly_count; i++) {
                    if (s_poly_buffer[i].pass_type == 1 && s_poly_buffer[i].mat.sp != NULL) {
                        sprite_t* sp = s_poly_buffer[i].mat.sp;
                        bool already = false;
                        for (int s = 0; s < active_sprite_count; s++) {
                            if (active_sprites[s] == sp) { already = true; break; }
                        }
                        if (!already && active_sprite_count < 8) {
                            active_sprites[active_sprite_count++] = sp;
                        }
                    }
                }

                // 2. Emissão das faces texturizadas agrupadas por material (sprite)
                for (int s = 0; s < active_sprite_count; s++) {
                    sprite_t* target_sp = active_sprites[s];
                    bool started_mat = false;
                    for (int i = 0; i < poly_count; i++) {
                        if (s_poly_buffer[i].pass_type != 1 || s_poly_buffer[i].mat.sp != target_sp) continue;
                        if (!started_mat) {
                            started_mat = true;
                            rdpq_sprite_upload(TILE0, target_sp, NULL);
                            if (!textured_state_active) {
                                rdpq_mode_combiner(RDPQ_COMBINER1((TEX0, ZERO, SHADE, ZERO), (ZERO, ZERO, ZERO, ONE)));
                                t3d_state_set_drawflags((enum T3DDrawFlags)(T3D_FLAG_SHADED | T3D_FLAG_TEXTURED | T3D_FLAG_NO_LIGHT));
                                textured_state_active = true;
                            }
                        }
                        emit_poly(s_poly_buffer[i].pos, s_poly_buffer[i].st, s_poly_buffer[i].col, s_poly_buffer[i].is_tri);
                        if (model_overflow) break;
                    }
                    if (started_mat && !model_overflow) flush_window();
                    if (model_overflow) break;
                }

                // 3. Emissão das faces untextured / fallback sólido do modelo
                if (!model_overflow) {
                    bool started_untextured = false;
                    for (int i = 0; i < poly_count; i++) {
                        if (s_poly_buffer[i].pass_type != 0) continue;
                        if (!started_untextured) {
                            started_untextured = true;
                            rdpq_mode_combiner(RDPQ_COMBINER1((PRIM, ZERO, SHADE, ZERO), (ZERO, ZERO, ZERO, ONE)));
                            t3d_state_set_drawflags((enum T3DDrawFlags)(T3D_FLAG_SHADED | T3D_FLAG_NO_LIGHT));
                            textured_state_active = false;
                        }
                        emit_poly(s_poly_buffer[i].pos, s_poly_buffer[i].st, s_poly_buffer[i].col, s_poly_buffer[i].is_tri);
                        if (model_overflow) break;
                    }
                    if (started_untextured && !model_overflow) flush_window();
                }

                if (textured_state_active) {
                    rdpq_mode_combiner(RDPQ_COMBINER1((PRIM, ZERO, SHADE, ZERO), (ZERO, ZERO, ZERO, ONE)));
                    t3d_state_set_drawflags((enum T3DDrawFlags)(T3D_FLAG_SHADED | T3D_FLAG_NO_LIGHT));
                }
            }

            if (model_overflow) {
                rspq_block_t* partial = rspq_block_end();
                if (partial) rspq_block_free(partial);
                debugf("[T3D] AVISO: Vtx pool cheio ao converter model %d (pass=%d usado=%d/%d)\n",
                       mid, pass, s_t3d_vtx_used, D2_T3D_MAX_VERTS);
                break;
            }

            heap_stats_t hs_pre_blk;
            sys_get_heap_stats(&hs_pre_blk);

            s_t3d_model_blocks[mid] = rspq_block_end();

            heap_stats_t hs_post_blk;
            sys_get_heap_stats(&hs_post_blk);
            int blk_bytes = hs_pre_blk.free - hs_post_blk.free;

            if (is_target_tex_model) {
                int tex_faces = 0, solid_faces = 0;
                for (int i = 0; i < poly_count; i++) {
                    if (s_poly_buffer[i].pass_type == 1) tex_faces++;
                    else solid_faces++;
                }
                debugf("[T3D_MAT] mid=%3d: materiais_resolvidos=%d faces_texturizadas=%d faces_solidas=%d tris=%d blk_alloc=%d heap_free=%d\n",
                       mid, active_sprite_count, tex_faces, solid_faces, total_tris, blk_bytes, hs_post_blk.free);
            } else if (hs_post_blk.free < 65536) {
                debugf("[T3D_MID] mid=%3d tile=%d faces_in=%2d polys_out=%2d tris=%2d vtx_used=%4d blk_alloc=%4d heap_free=%d\n",
                       mid, is_tile, num_faces, poly_count, total_tris, s_t3d_vtx_used, blk_bytes, hs_post_blk.free);
            }

            s_t3d_tile_ofs[mid] = is_tile ? (unsigned char)base_ofse : 0;
#if defined(D2_T3D_STATS) || defined(D2_ORIGIN_PROF)
            s_t3d_model_triangles[mid] = (unsigned short)total_tris;
#endif
            s_t3d_models_converted++;
            if (is_tile) s_t3d_tiles_converted++;
            else s_t3d_blds_converted++;
        }
    }

    int non_empty_models = 0;
    for (int mid = 0; mid < count; mid++) {
        MODEL* model = modelpointers[mid];
        if (model && model != &dummyModel && model->num_polys > 0)
            non_empty_models++;
    }
    int models_old_path = non_empty_models - s_t3d_models_converted;

    sys_get_heap_stats(&hs_after);
    int heap_used_by_blocks = hs_after.used - hs_before.used;

    debugf("[T3D] Convertidos %d modelos no T3D (tiles=%d, predios=%d), %d no caminho antigo (%d total). Vertices: %d/%d (%d KB de %d KB)\n",
           s_t3d_models_converted, s_t3d_tiles_converted, s_t3d_blds_converted,
           models_old_path, non_empty_models,
           s_t3d_vtx_used, D2_T3D_MAX_VERTS,
           (s_t3d_vtx_used * 16) / 1024, (D2_T3D_MAX_VERTS * 16) / 1024);
    debugf("[T3D_MEM] Heap alocado para blocos RSPQ: %d bytes (heap livre: %d -> %d)\n",
           heap_used_by_blocks, hs_before.free, hs_after.free);
}

int d2_t3d_has_model(int model_idx)
{
#ifdef D2_T3D_STATS
    s_stat_candidates++;
    if (model_idx >= 0 && model_idx < MAX_MODEL_SLOTS && s_t3d_model_blocks[model_idx])
        s_stat_native++;
#endif
    if (model_idx < 0 || model_idx >= MAX_MODEL_SLOTS) return 0;
    return s_t3d_live_model[model_idx] || s_t3d_model_blocks[model_idx] != NULL;
}

void d2_t3d_set_tile_tint(unsigned int psx_colour)
{
    unsigned int r = (psx_colour & 0xFFu) * 2u;
    unsigned int g = ((psx_colour >> 8) & 0xFFu) * 2u;
    unsigned int b = ((psx_colour >> 16) & 0xFFu) * 2u;
    if (r > 255u) r = 255u;
    if (g > 255u) g = 255u;
    if (b > 255u) b = 255u;
    s_tile_tint = RGBA32((uint8_t)r, (uint8_t)g, (uint8_t)b, 255);
}

void d2_t3d_queue_tile(int model_idx, int yang, int z, int tx, int ty, int tz)
{
    if (s_t3d_tile_queue_count >= D2_T3D_MAX_QUEUED_TILES) return;
    if (model_idx < 0 || model_idx >= MAX_MODEL_SLOTS || (!s_t3d_live_model[model_idx] && !s_t3d_model_blocks[model_idx])) return;

    int sort_z = z + ((int)s_t3d_tile_ofs[model_idx] << 1);
    D2QueuedInstance* q = &s_t3d_tile_queue[s_t3d_tile_queue_count++];
    q->model_idx = model_idx;
    q->yang = yang & 63;
    q->z = sort_z;
    q->tx = tx;
    q->ty = ty;
    q->tz = tz;
#ifdef D2_T3D_STATS
    s_stat_queued++;
    s_stat_triangles += s_t3d_model_triangles[model_idx];
#endif
#ifdef D2_ORIGIN_PROF
    g_d2_oprof.objs[OPROF_T3D_GRD]++;
    g_d2_oprof.tris[OPROF_T3D_GRD] += s_t3d_model_triangles[model_idx];
#endif
}

void d2_t3d_queue_building(int model_idx, int yang, int z, int tx, int ty, int tz)
{
    if (s_t3d_bld_queue_count >= D2_T3D_MAX_QUEUED_BLDS) return;
    if (model_idx < 0 || model_idx >= MAX_MODEL_SLOTS || (!s_t3d_live_model[model_idx] && !s_t3d_model_blocks[model_idx])) return;

    D2QueuedInstance* q = &s_t3d_bld_queue[s_t3d_bld_queue_count++];
    q->model_idx = model_idx;
    q->yang = yang & 63;
    q->z = z;
    q->tx = tx;
    q->ty = ty;
    q->tz = tz;
#ifdef D2_T3D_STATS
    s_stat_queued++;
    s_stat_triangles += s_t3d_model_triangles[model_idx];
#endif
#ifdef D2_ORIGIN_PROF
    g_d2_oprof.objs[OPROF_T3D_BLD]++;
    g_d2_oprof.tris[OPROF_T3D_BLD] += s_t3d_model_triangles[model_idx];
#endif
}

int d2_t3d_get_queued_count(void)
{
    return s_t3d_tile_queue_count + s_t3d_bld_queue_count;
}

void d2_t3d_get_stats(int* vtx_total, int* vtx_used, int* models_converted)
{
    if (vtx_total) *vtx_total = D2_T3D_MAX_VERTS;
    if (vtx_used) *vtx_used = s_t3d_vtx_used;
    if (models_converted) *models_converted = s_t3d_models_converted;
}

static void sort_instances_back_to_front(D2QueuedInstance* arr, int n)
{
    for (int i = 1; i < n; i++) {
        D2QueuedInstance key = arr[i];
        int j = i - 1;
        while (j >= 0 && arr[j].z < key.z) {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = key;
    }
}

static inline void build_instance_matrix(const D2QueuedInstance& inst, T3DMat4FP* dst_fp, bool submit = true)
{
    const float kInv4096 = 1.0f / 4096.0f;
    MATRIX2* cmat = &CompoundMatrix[inst.yang];

    T3DMat4 m;
    m.m[0][0] = (float)cmat->m[0][0] * kInv4096;
    m.m[0][1] = (float)cmat->m[1][0] * kInv4096;
    m.m[0][2] = (float)cmat->m[2][0] * kInv4096;
    m.m[0][3] = 0.0f;

    m.m[1][0] = (float)cmat->m[0][1] * kInv4096;
    m.m[1][1] = (float)cmat->m[1][1] * kInv4096;
    m.m[1][2] = (float)cmat->m[2][1] * kInv4096;
    m.m[1][3] = 0.0f;

    m.m[2][0] = (float)cmat->m[0][2] * kInv4096;
    m.m[2][1] = (float)cmat->m[1][2] * kInv4096;
    m.m[2][2] = (float)cmat->m[2][2] * kInv4096;
    m.m[2][3] = 0.0f;

    m.m[3][0] = (float)inst.tx;
    m.m[3][1] = (float)inst.ty;
    m.m[3][2] = (float)inst.tz;
    m.m[3][3] = 1.0f;

    t3d_mat4_to_fixed(dst_fp, &m);
    data_cache_hit_writeback(dst_fp, sizeof(T3DMat4FP));

    if (submit) t3d_matrix_set(dst_fp, true);
}

static inline void build_and_run_instance(const D2QueuedInstance& inst, T3DMat4FP* dst_fp)
{
    build_instance_matrix(inst, dst_fp);
    if (s_t3d_live_model[inst.model_idx]) d2_t3d_draw_live_model(inst);
    else rspq_block_run(s_t3d_model_blocks[inst.model_idx]);
}

void d2_t3d_draw_queued_buildings(void)
{
    // Frontend/HUD OTs have no world. Do not change RDP state or viewport there.
    if (s_t3d_tile_queue_count == 0 && s_t3d_bld_queue_count == 0) return;
    static int s_logged_diag = 0;
    if (s_logged_diag < 2 && (s_t3d_tile_queue_count > 0 || s_t3d_bld_queue_count > 0)) {
        extern int CameraCnt;
        debugf("[DIAG_M1] cc=%d tiles=%d blds=%d\n", CameraCnt, s_t3d_tile_queue_count, s_t3d_bld_queue_count);
        for (int i = 0; i < s_t3d_bld_queue_count && i < 20; i++) {
            debugf("  bld[%d]: mid=%d tx=%d ty=%d tz=%d\n", i, s_t3d_bld_queue[i].model_idx, s_t3d_bld_queue[i].tx, s_t3d_bld_queue[i].ty, s_t3d_bld_queue[i].tz);
        }
        for (int i = 0; i < s_t3d_tile_queue_count && i < 20; i++) {
            debugf("  tile[%d]: mid=%d tx=%d ty=%d tz=%d\n", i, s_t3d_tile_queue[i].model_idx, s_t3d_tile_queue[i].tx, s_t3d_tile_queue[i].ty, s_t3d_tile_queue[i].tz);
        }
        s_logged_diag++;
    }
    // O caminho normal de mundo deve sempre ser compilado com D2_T3D_WORLD,
    // independentemente da flag opt-in D2_T3D_SMOKE.
    if (!s_t3d_inited) {
        d2_t3d_init();
    }
    rdpq_sync_pipe();
    T3DVec3 e  = {{ 0.0f,  0.0f,   0.0f }};
    T3DVec3 tg = {{ 0.0f,  0.0f, 256.0f }};
    T3DVec3 u  = {{ 0.0f, -1.0f,   0.0f }};
    float nearP = 16.0f, farP = 32768.0f;
    float h = (float)scr_z;
    if (h < 1.0f) h = 256.0f;
    float fov = 2.0f * atanf(120.0f / h);

    t3d_viewport_set_projection(&s_t3d_viewport, fov, nearP, farP);
    t3d_viewport_look_at(&s_t3d_viewport, &e, &tg, &u);

    // Apply viewport matrices for this frame
    t3d_viewport_attach(&s_t3d_viewport);
    s_live_vertex_cursor = 0;
    s_live_faces = s_live_parts = s_live_missing = 0;

    rdpq_set_prim_color(s_tile_tint);
    rdpq_mode_begin();
        rdpq_set_mode_standard();
        rdpq_mode_persp(true);
        rdpq_mode_filter(FILTER_POINT);
        rdpq_mode_combiner(RDPQ_COMBINER1((PRIM, ZERO, SHADE, ZERO), (ZERO, ZERO, ZERO, ONE)));
    rdpq_mode_end();

    t3d_state_set_drawflags((enum T3DDrawFlags)(T3D_FLAG_SHADED | T3D_FLAG_NO_LIGHT));

    int mat_cursor = 0;
    t3d_matrix_push_pos(1);

    if (s_t3d_tile_queue_count > 0) {
        sort_instances_back_to_front(s_t3d_tile_queue, s_t3d_tile_queue_count);
        for (int i = 0; i < s_t3d_tile_queue_count; i++) {
            if (mat_cursor >= D2_T3D_MAX_MATS) break;
            T3DMat4FP* cur_fp = &s_t3d_mat_pool[s_t3d_viewport._bufferIdx][mat_cursor++];
            build_and_run_instance(s_t3d_tile_queue[i], cur_fp);
        }
    }

    if (s_t3d_bld_queue_count > 0) {
        sort_instances_back_to_front(s_t3d_bld_queue, s_t3d_bld_queue_count);
        for (int i = 0; i < s_t3d_bld_queue_count; i++) {
            if (mat_cursor >= D2_T3D_MAX_MATS) break;
            T3DMat4FP* cur_fp = &s_t3d_mat_pool[s_t3d_viewport._bufferIdx][mat_cursor++];
            build_and_run_instance(s_t3d_bld_queue[i], cur_fp);
        }
    }

    t3d_matrix_pop(1);
    static unsigned live_frames;
    if ((++live_frames % 60) == 0)
        debugf("[T3D_LIVE] faces=%u parts=%u missing=%u vertices=%d\n", s_live_faces, s_live_parts, s_live_missing, s_live_vertex_cursor);

    const bool had_tiles = (s_t3d_tile_queue_count > 0);
    const bool had_blds  = (s_t3d_bld_queue_count > 0);
    s_t3d_tile_queue_count = 0;
    s_t3d_bld_queue_count = 0;

    // Invalidate RDP backend cache if any native T3D draw calls took place this frame
    if (had_tiles || had_blds) {
        d2_rdp_invalidate_backend_cache();
    }

#ifdef D2_T3D_STATS
    uint32_t t_end = t3d_stat_clock();
    t3d_stat_frame(t_end - t_start);
#endif
}

#include "d2_t3d_world_order.inc"

#endif // D2_T3D_WORLD
