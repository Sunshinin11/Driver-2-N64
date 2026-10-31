#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>

#include "d2_t3d_world.h"

#ifdef D2_T3D_WORLD

#include "driver2.h"
#include "C/models.h"
#include "engine/mdl.h"
#include "C/draw.h"
#include "C/main.h"
#include "psx/gtereg.h"

// Static arenas in .bss (never from heap)
#define D2_T3D_MAX_VERTS 8192
#define D2_T3D_MAX_QUEUED 192

// Vertices for all building models (8192 verts = 4096 packed structs = 128 KB)
static T3DVertPacked s_t3d_vtx_pool[D2_T3D_MAX_VERTS / 2] __attribute__((aligned(16)));
static int s_t3d_vtx_used = 0;

// Model blocks
static rspq_block_t* s_t3d_model_blocks[MAX_MODEL_SLOTS];

// Matrix pool for queued instances (double buffered: 2 frames x 192 instances x 64B = 24 KB)
static T3DMat4FP s_t3d_mat_pool[2][D2_T3D_MAX_QUEUED] __attribute__((aligned(16)));

// Viewport matrix buffers (2 frames x 2 matrices x 64B = 256 bytes)
static T3DMat4FP s_t3d_vp_matFP[2][2] __attribute__((aligned(16)));

static T3DViewport s_t3d_viewport;
static bool s_t3d_inited = false;
static int s_t3d_models_converted = 0;

typedef struct {
    int model_idx;
    int yang;
    int z;
    int tx, ty, tz;
} D2QueuedBuilding;

static D2QueuedBuilding s_t3d_queue[D2_T3D_MAX_QUEUED];
static int s_t3d_queue_count = 0;

extern "C" void d2_rdp_invalidate_backend_cache(void);
extern "C" int d2_rdp_frame_count(void);

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

// Spooled models note: Models loaded dynamically by region spooling (init_spooled_models)
// remain on the legacy PS1 path. Only permanent level models loaded at level init
// (ProcessMDSLump) are converted to Tiny3D. This ensures 0 heap fragmentation and 0 dynamic
// allocation overhead during gameplay driving across regions.

void d2_t3d_on_models_loaded(void)
{
    if (!s_t3d_inited) {
        d2_t3d_init();
    }

    // Free all previously allocated rspq blocks to prevent memory leaks across mission loads
    for (int i = 0; i < MAX_MODEL_SLOTS; i++) {
        if (s_t3d_model_blocks[i]) {
            rspq_block_free(s_t3d_model_blocks[i]);
            s_t3d_model_blocks[i] = NULL;
        }
    }

    s_t3d_vtx_used = 0;
    s_t3d_models_converted = 0;

    heap_stats_t hs_before, hs_after;
    sys_get_heap_stats(&hs_before);

    int count = num_models_in_pack;
    if (count > MAX_MODEL_SLOTS) count = MAX_MODEL_SLOTS;

    for (int mid = 0; mid < count; mid++) {
        MODEL* model = modelpointers[mid];
        if (!model || model == &dummyModel || model->num_polys == 0) continue;

        int nverts = model->num_vertices;
        if (nverts <= 0) continue;

        // Disqualify non-building models: sprites, tiles/roads, water, transparent/alpha,
        // smashable objects (boxes, trash cans), lamp posts, trees, barriers, animating objects
        if (model->shape_flags & (SHAPE_FLAG_SPRITE | SHAPE_FLAG_WATER | SHAPE_FLAG_TILE |
                                  SHAPE_FLAG_ROAD | SHAPE_FLAG_TRANS | SHAPE_FLAG_ALPHA)) {
            continue;
        }
        if (model->flags2 & (MODEL_FLAG_ANIMOBJ | MODEL_FLAG_MEDIAN | MODEL_FLAG_JUNC |
                             MODEL_FLAG_ALLEY | MODEL_FLAG_CHAIR | MODEL_FLAG_BARRIER |
                             MODEL_FLAG_SMASHABLE | MODEL_FLAG_LAMP | MODEL_FLAG_TREE |
                             MODEL_FLAG_GRASS | MODEL_FLAG_PATH)) {
            continue;
        }

#define T3D_VERT_WINDOW 64

        // Collect building quads
        struct BldQuadInfo {
            int v0, v1, v2, v3;
            uint32_t col;
        };
        BldQuadInfo quads[128];
        int num_quads = 0;

        unsigned char* pblock = (unsigned char*)GET_MODEL_DATA(unsigned char, model, poly_block);
        int poffs = 0;
        for (int p = 0; p < model->num_polys; p++) {
            int ptype = pblock[poffs] & 31;
            int psize = PolySizes[ptype];
            if (psize == 0) break;
            if (ptype == 11 || ptype == 21 || ptype == 23) {
                if (num_quads < 128) {
                    PL_POLYFT4* poly = (PL_POLYFT4*)&pblock[poffs];
                    uint32_t col = (ptype == 21) ? 0xD0A878FF : ((ptype == 23) ? 0x8090A0FF : 0xCCCCCCFF);
                    quads[num_quads++] = { poly->v0, poly->v1, poly->v2, poly->v3, col };
                }
            }
            poffs += psize;
        }

        if (num_quads == 0) continue;

        // Determine vertex colors (priority: 21/23 over default wall)
        uint32_t vtx_col[256];
        for (int v = 0; v < nverts && v < 256; v++) vtx_col[v] = 0xCCCCCCFF;
        for (int q = 0; q < num_quads; q++) {
            if (quads[q].col != 0xCCCCCCFF) {
                if (quads[q].v0 < 256) vtx_col[quads[q].v0] = quads[q].col;
                if (quads[q].v1 < 256) vtx_col[quads[q].v1] = quads[q].col;
                if (quads[q].v2 < 256) vtx_col[quads[q].v2] = quads[q].col;
                if (quads[q].v3 < 256) vtx_col[quads[q].v3] = quads[q].col;
            }
        }

        SVECTOR* src_verts = GET_MODEL_DATA(SVECTOR, model, vertices);

        // Record RSPQ block with vertex windowing (max 64 verts per t3d_vert_load)
        rspq_block_begin();

        int local_map[256];
        memset(local_map, -1, sizeof(local_map));
        int local_vtx_orig[T3D_VERT_WINDOW];
        int local_vtx_count = 0;

        struct LocalQuad {
            int lv0, lv1, lv2, lv3;
        };
        LocalQuad batch_quads[128];
        int batch_quad_count = 0;
        bool model_overflow = false;

        auto flush_window = [&]() {
            if (local_vtx_count == 0) return;
            int aligned_batch = (local_vtx_count + 1) & ~1;
            if (s_t3d_vtx_used + aligned_batch > D2_T3D_MAX_VERTS) {
                model_overflow = true;
                return;
            }
            int batch_start = s_t3d_vtx_used;
            for (int k = 0; k < local_vtx_count; k++) {
                int orig = local_vtx_orig[k];
                uint32_t col = (orig < 256) ? vtx_col[orig] : 0xCCCCCCFF;
                int vidx = batch_start + k;
                int packed_idx = vidx / 2;
                if (!(vidx & 1)) {
                    s_t3d_vtx_pool[packed_idx].posA[0] = src_verts[orig].vx;
                    s_t3d_vtx_pool[packed_idx].posA[1] = src_verts[orig].vy;
                    s_t3d_vtx_pool[packed_idx].posA[2] = src_verts[orig].vz;
                    s_t3d_vtx_pool[packed_idx].rgbaA = col;
                    s_t3d_vtx_pool[packed_idx].normA = 0;
                } else {
                    s_t3d_vtx_pool[packed_idx].posB[0] = src_verts[orig].vx;
                    s_t3d_vtx_pool[packed_idx].posB[1] = src_verts[orig].vy;
                    s_t3d_vtx_pool[packed_idx].posB[2] = src_verts[orig].vz;
                    s_t3d_vtx_pool[packed_idx].rgbaB = col;
                    s_t3d_vtx_pool[packed_idx].normB = 0;
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
            }
            data_cache_hit_writeback(&s_t3d_vtx_pool[batch_start / 2], (aligned_batch / 2) * sizeof(T3DVertPacked));

            t3d_vert_load(&s_t3d_vtx_pool[batch_start / 2], 0, aligned_batch);
            for (int q = 0; q < batch_quad_count; q++) {
                t3d_tri_draw(batch_quads[q].lv0, batch_quads[q].lv1, batch_quads[q].lv3);
                t3d_tri_draw(batch_quads[q].lv1, batch_quads[q].lv2, batch_quads[q].lv3);
            }
            t3d_tri_sync();
            s_t3d_vtx_used += aligned_batch;

            memset(local_map, -1, sizeof(local_map));
            local_vtx_count = 0;
            batch_quad_count = 0;
        };

        for (int q = 0; q < num_quads; q++) {
            int v[4] = { quads[q].v0, quads[q].v1, quads[q].v2, quads[q].v3 };
            int new_needed = 0;
            for (int k = 0; k < 4; k++) {
                if (v[k] >= 256 || local_map[v[k]] == -1) new_needed++;
            }
            if (local_vtx_count + new_needed > T3D_VERT_WINDOW) {
                flush_window();
                if (model_overflow) break;
            }
            int lv[4];
            for (int k = 0; k < 4; k++) {
                int orig = v[k];
                if (orig < 256 && local_map[orig] != -1) {
                    lv[k] = local_map[orig];
                } else {
                    int lidx = local_vtx_count++;
                    local_vtx_orig[lidx] = orig;
                    if (orig < 256) local_map[orig] = lidx;
                    lv[k] = lidx;
                }
            }
            batch_quads[batch_quad_count++] = { lv[0], lv[1], lv[2], lv[3] };
        }

        if (!model_overflow) {
            flush_window();
        }

        if (model_overflow) {
            rspq_block_t* partial = rspq_block_end();
            if (partial) rspq_block_free(partial);
            debugf("[T3D] AVISO: Vtx pool cheio ao converter model %d (usado=%d/%d)\n",
                   mid, s_t3d_vtx_used, D2_T3D_MAX_VERTS);
            break;
        }

        s_t3d_model_blocks[mid] = rspq_block_end();
        s_t3d_models_converted++;
    }

    int non_empty_models = 0;
    for (int mid = 0; mid < count; mid++) {
        MODEL* model = modelpointers[mid];
        if (model && model != &dummyModel && model->num_polys > 0 && model->num_vertices > 0)
            non_empty_models++;
    }
    int models_old_path = non_empty_models - s_t3d_models_converted;

    sys_get_heap_stats(&hs_after);
    int heap_used_by_blocks = hs_after.used - hs_before.used;

    debugf("[T3D] Convertidos %d predios no T3D, %d no caminho antigo (%d total). Vertices: %d/%d (%d KB de %d KB)\n",
           s_t3d_models_converted, models_old_path, non_empty_models,
           s_t3d_vtx_used, D2_T3D_MAX_VERTS,
           (s_t3d_vtx_used * 16) / 1024, (D2_T3D_MAX_VERTS * 16) / 1024);
    debugf("[T3D_MEM] Heap alocado para blocos RSPQ: %d bytes (heap livre: %d -> %d)\n",
           heap_used_by_blocks, hs_before.free, hs_after.free);
}

int d2_t3d_has_model(int model_idx)
{
    if (model_idx < 0 || model_idx >= MAX_MODEL_SLOTS) return 0;
    return s_t3d_model_blocks[model_idx] != NULL;
}

void d2_t3d_queue_building(int model_idx, int yang, int z, int tx, int ty, int tz)
{
    if (s_t3d_queue_count >= D2_T3D_MAX_QUEUED) return;
    if (!s_t3d_model_blocks[model_idx]) return;

    D2QueuedBuilding* q = &s_t3d_queue[s_t3d_queue_count++];
    q->model_idx = model_idx;
    q->yang = yang;
    q->z = z;
    q->tx = tx;
    q->ty = ty;
    q->tz = tz;
}

int d2_t3d_get_queued_count(void)
{
    return s_t3d_queue_count;
}

void d2_t3d_get_stats(int* vtx_total, int* vtx_used, int* models_converted)
{
    if (vtx_total) *vtx_total = D2_T3D_MAX_VERTS;
    if (vtx_used) *vtx_used = s_t3d_vtx_used;
    if (models_converted) *models_converted = s_t3d_models_converted;
}

void d2_t3d_draw_queued_buildings(void)
{
    if (s_t3d_queue_count <= 0) return;

    rdpq_sync_pipe();

    // Camera look-at (identity with PS1 -> N64 axis flip)
    T3DVec3 e  = {{ 0.0f,  0.0f,   0.0f }};
    T3DVec3 tg = {{ 0.0f,  0.0f, 256.0f }};
    T3DVec3 u  = {{ 0.0f, -1.0f,   0.0f }};

    float nearP = 16.0f, farP = 32768.0f;
    float h = (float)scr_z;
    if (h < 1.0f) h = 256.0f;

    float fov = 2.0f * atanf(120.0f / h);
    s_t3d_viewport._bufferIdx = d2_rdp_frame_count() & 1;

    t3d_viewport_set_perspective(&s_t3d_viewport, fov, 320.0f / 240.0f, nearP, farP);
    t3d_viewport_look_at(&s_t3d_viewport, &e, &tg, &u);
    t3d_viewport_attach(&s_t3d_viewport);

    // Flat shaded render mode without Z-buffer (proof of concept)
    rdpq_mode_begin();
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
        rdpq_mode_blender(0);
        rdpq_mode_zbuf(false, false);
    rdpq_mode_end();

    t3d_state_set_drawflags(T3D_FLAG_SHADED);

    // Sort queued buildings back-to-front (furthest Z first)
    for (int i = 0; i < s_t3d_queue_count - 1; i++) {
        for (int j = i + 1; j < s_t3d_queue_count; j++) {
            if (s_t3d_queue[i].z < s_t3d_queue[j].z) {
                D2QueuedBuilding tmp = s_t3d_queue[i];
                s_t3d_queue[i] = s_t3d_queue[j];
                s_t3d_queue[j] = tmp;
            }
        }
    }

    int frame_idx = d2_rdp_frame_count() & 1;

    for (int i = 0; i < s_t3d_queue_count; i++) {
        int mid = s_t3d_queue[i].model_idx;
        if (!s_t3d_model_blocks[mid]) continue;

        int yang = s_t3d_queue[i].yang;
        MATRIX2* cmat = &CompoundMatrix[yang];

        // T3DMat4 is column-major: m[column][row]
        // CompoundMatrix[yang] (from draw.c) is row-major: cmat->m[row][col] (4.12 fixed point, / 4096.0f)
        // Meaning of CompoundMatrix: inv_camera_matrix * matrixtable[yang], which is the model's
        // orientation in world space already combined with the camera's orientation (expressed in camera space).
        // Meaning of tx, ty, tz: the model object's origin in camera space (C2_TRX, C2_TRY, C2_TRZ),
        // calculated via Apply_InvCameraMatrixAndSetMatrix(&cop->pos, ...).
        T3DMat4 m;
        // Column 0: X-coefficients of output X, Y, Z, W
        m.m[0][0] = (float)cmat->m[0][0] / 4096.0f;
        m.m[0][1] = (float)cmat->m[1][0] / 4096.0f;
        m.m[0][2] = (float)cmat->m[2][0] / 4096.0f;
        m.m[0][3] = 0.0f;

        // Column 1: Y-coefficients of output X, Y, Z, W
        m.m[1][0] = (float)cmat->m[0][1] / 4096.0f;
        m.m[1][1] = (float)cmat->m[1][1] / 4096.0f;
        m.m[1][2] = (float)cmat->m[2][1] / 4096.0f;
        m.m[1][3] = 0.0f;

        // Column 2: Z-coefficients of output X, Y, Z, W
        m.m[2][0] = (float)cmat->m[0][2] / 4096.0f;
        m.m[2][1] = (float)cmat->m[1][2] / 4096.0f;
        m.m[2][2] = (float)cmat->m[2][2] / 4096.0f;
        m.m[2][3] = 0.0f;

        // Column 3: Translation (camera space)
        m.m[3][0] = (float)s_t3d_queue[i].tx;
        m.m[3][1] = (float)s_t3d_queue[i].ty;
        m.m[3][2] = (float)s_t3d_queue[i].tz;
        m.m[3][3] = 1.0f;

        static bool s_validated_vertex = false;
        if (!s_validated_vertex && s_t3d_queue[i].z > 500) {
            s_validated_vertex = true;
            MODEL* model = modelpointers[mid];
            if (model && model->num_vertices > 0) {
                SVECTOR* src_verts = GET_MODEL_DATA(SVECTOR, model, vertices);
                float vx = (float)src_verts[0].vx;
                float vy = (float)src_verts[0].vy;
                float vz = (float)src_verts[0].vz;

                float x_cam = ((float)cmat->m[0][0] * vx + (float)cmat->m[0][1] * vy + (float)cmat->m[0][2] * vz) / 4096.0f + (float)s_t3d_queue[i].tx;
                float y_cam = ((float)cmat->m[1][0] * vx + (float)cmat->m[1][1] * vy + (float)cmat->m[1][2] * vz) / 4096.0f + (float)s_t3d_queue[i].ty;
                float z_cam = ((float)cmat->m[2][0] * vx + (float)cmat->m[2][1] * vy + (float)cmat->m[2][2] * vz) / 4096.0f + (float)s_t3d_queue[i].tz;

                float gte_sx = 160.0f + (x_cam * h) / z_cam;
                float gte_sy = 120.0f + (y_cam * h) / z_cam;

                T3DVec3 worldPos = {{
                    m.m[0][0] * vx + m.m[1][0] * vy + m.m[2][0] * vz + m.m[3][0],
                    m.m[0][1] * vx + m.m[1][1] * vy + m.m[2][1] * vz + m.m[3][1],
                    m.m[0][2] * vx + m.m[1][2] * vy + m.m[2][2] * vz + m.m[3][2]
                }};

                T3DVec3 screenPos;
                t3d_viewport_calc_viewspace_pos(&s_t3d_viewport, &screenPos, &worldPos);
                float t3d_sx = 160.0f + screenPos.v[0] * 160.0f;
                float t3d_sy = 120.0f - screenPos.v[1] * 120.0f;

                debugf("[T3D_VAL] model=%d v0=(%.0f,%.0f,%.0f) GTE=(%.2f, %.2f) T3D=(%.2f, %.2f) diff=(%.2f, %.2f)\n",
                       mid, vx, vy, vz, gte_sx, gte_sy, t3d_sx, t3d_sy,
                       fabsf(gte_sx - t3d_sx), fabsf(gte_sy - t3d_sy));
            }
        }

        t3d_mat4_to_fixed(&s_t3d_mat_pool[frame_idx][i], &m);

        t3d_matrix_set(&s_t3d_mat_pool[frame_idx][i], true);
        rspq_block_run(s_t3d_model_blocks[mid]);
    }

    s_t3d_queue_count = 0;

    // Invalidate backend cache and synchronize
    d2_rdp_invalidate_backend_cache();
}

#endif // D2_T3D_WORLD
