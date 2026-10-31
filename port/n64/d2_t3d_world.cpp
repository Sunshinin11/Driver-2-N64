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

void d2_t3d_on_models_loaded(void)
{
    if (!s_t3d_inited) {
        d2_t3d_init();
    }

    s_t3d_vtx_used = 0;
    s_t3d_models_converted = 0;
    memset(s_t3d_model_blocks, 0, sizeof(s_t3d_model_blocks));

    int count = num_models_in_pack;
    if (count > MAX_MODEL_SLOTS) count = MAX_MODEL_SLOTS;

    for (int mid = 0; mid < count; mid++) {
        MODEL* model = modelpointers[mid];
        if (!model || model == &dummyModel || model->num_polys == 0) continue;

        int nverts = model->num_vertices;
        if (nverts <= 0 || nverts > 64) {
            // Models with > 64 verts continue in legacy PlotBuildingModel
            continue;
        }

        // Check if model contains any building polygons (ptype 11, 21, 23)
        unsigned char* pblock = (unsigned char*)GET_MODEL_DATA(unsigned char, model, poly_block);
        int bld_quad_count = 0;
        int poffs = 0;
        for (int p = 0; p < model->num_polys; p++) {
            int ptype = pblock[poffs] & 31;
            int psize = PolySizes[ptype];
            if (psize == 0) break;
            if (ptype == 11 || ptype == 21 || ptype == 23) {
                bld_quad_count++;
            }
            poffs += psize;
        }

        if (bld_quad_count == 0) continue;

        // Check vertex capacity
        int vtx_aligned = (nverts + 1) & ~1;
        if (s_t3d_vtx_used + vtx_aligned > D2_T3D_MAX_VERTS) {
            debugf("[T3D] AVISO: Vtx pool cheio ao converter model %d (usado=%d/%d)\n",
                   mid, s_t3d_vtx_used, D2_T3D_MAX_VERTS);
            break;
        }

        int start_vtx = s_t3d_vtx_used;
        SVECTOR* src_verts = GET_MODEL_DATA(SVECTOR, model, vertices);

        // Copy vertices into static pool with default wall color (0xCCCCCCFF)
        for (int v = 0; v < nverts; v++) {
            int packed_idx = (start_vtx + v) / 2;
            int is_b = (start_vtx + v) & 1;
            if (!is_b) {
                s_t3d_vtx_pool[packed_idx].posA[0] = src_verts[v].vx;
                s_t3d_vtx_pool[packed_idx].posA[1] = src_verts[v].vy;
                s_t3d_vtx_pool[packed_idx].posA[2] = src_verts[v].vz;
                s_t3d_vtx_pool[packed_idx].rgbaA = 0xCCCCCCFF;
                s_t3d_vtx_pool[packed_idx].normA = 0;
            } else {
                s_t3d_vtx_pool[packed_idx].posB[0] = src_verts[v].vx;
                s_t3d_vtx_pool[packed_idx].posB[1] = src_verts[v].vy;
                s_t3d_vtx_pool[packed_idx].posB[2] = src_verts[v].vz;
                s_t3d_vtx_pool[packed_idx].rgbaB = 0xCCCCCCFF;
                s_t3d_vtx_pool[packed_idx].normB = 0;
            }
        }
        if (nverts & 1) {
            int packed_idx = (start_vtx + nverts) / 2;
            s_t3d_vtx_pool[packed_idx].posB[0] = 0;
            s_t3d_vtx_pool[packed_idx].posB[1] = 0;
            s_t3d_vtx_pool[packed_idx].posB[2] = 0;
            s_t3d_vtx_pool[packed_idx].rgbaB = 0;
            s_t3d_vtx_pool[packed_idx].normB = 0;
        }

        // Apply colors per quad type (ptype 21 = detail/cornice, ptype 23 = window/roof)
        poffs = 0;
        for (int p = 0; p < model->num_polys; p++) {
            int ptype = pblock[poffs] & 31;
            int psize = PolySizes[ptype];
            if (psize == 0) break;
            if (ptype == 21 || ptype == 23) {
                uint32_t col = (ptype == 21) ? 0xD0A878FF : 0x8090A0FF;
                PL_POLYFT4* poly = (PL_POLYFT4*)&pblock[poffs];
                int v[4] = { poly->v0, poly->v1, poly->v2, poly->v3 };
                for (int k = 0; k < 4; k++) {
                    if (v[k] < nverts) {
                        int vidx = start_vtx + v[k];
                        if (vidx & 1)
                            s_t3d_vtx_pool[vidx / 2].rgbaB = col;
                        else
                            s_t3d_vtx_pool[vidx / 2].rgbaA = col;
                    }
                }
            }
            poffs += psize;
        }

        s_t3d_vtx_used += vtx_aligned;

        // Flush L1 D-Cache for the uploaded vertices
        data_cache_hit_writeback(&s_t3d_vtx_pool[start_vtx / 2], (vtx_aligned / 2) * sizeof(T3DVertPacked));

        // Record RSPQ block
        rspq_block_begin();
        t3d_vert_load(&s_t3d_vtx_pool[start_vtx / 2], 0, vtx_aligned);

        poffs = 0;
        for (int p = 0; p < model->num_polys; p++) {
            int ptype = pblock[poffs] & 31;
            int psize = PolySizes[ptype];
            if (psize == 0) break;
            if (ptype == 11 || ptype == 21 || ptype == 23) {
                PL_POLYFT4* poly = (PL_POLYFT4*)&pblock[poffs];
                // Quad: A=v0, B=v1, C=v3, D=v2
                // Triangles: (v0, v1, v3) and (v1, v2, v3)
                t3d_tri_draw(poly->v0, poly->v1, poly->v3);
                t3d_tri_draw(poly->v1, poly->v2, poly->v3);
            }
            poffs += psize;
        }
        t3d_tri_sync();
        s_t3d_model_blocks[mid] = rspq_block_end();
        s_t3d_models_converted++;
    }

    debugf("[T3D] Convertidos %d modelos de predios, %d/%d vertices (%d KB de %d KB)\n",
           s_t3d_models_converted, s_t3d_vtx_used, D2_T3D_MAX_VERTS,
           (s_t3d_vtx_used * 16) / 1024, (D2_T3D_MAX_VERTS * 16) / 1024);
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
