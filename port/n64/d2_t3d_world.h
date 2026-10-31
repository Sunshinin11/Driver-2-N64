#ifndef D2_T3D_WORLD_H
#define D2_T3D_WORLD_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef D2_T3D_WORLD

void d2_t3d_init(void);
void d2_t3d_on_models_loaded(void);
int  d2_t3d_has_model(int model_idx);
void d2_t3d_queue_building(int model_idx, int yang, int z, int tx, int ty, int tz);
void d2_t3d_draw_queued_buildings(void);
int  d2_t3d_get_queued_count(void);
void d2_t3d_get_stats(int* vtx_total, int* vtx_used, int* models_converted);

#else

static inline void d2_t3d_init(void) {}
static inline void d2_t3d_on_models_loaded(void) {}
static inline int  d2_t3d_has_model(int model_idx) { (void)model_idx; return 0; }
static inline void d2_t3d_queue_building(int model_idx, int yang, int z, int tx, int ty, int tz) {
    (void)model_idx; (void)yang; (void)z; (void)tx; (void)ty; (void)tz;
}
static inline void d2_t3d_draw_queued_buildings(void) {}
static inline int  d2_t3d_get_queued_count(void) { return 0; }
static inline void d2_t3d_get_stats(int* vtx_total, int* vtx_used, int* models_converted) {
    if (vtx_total) *vtx_total = 0;
    if (vtx_used) *vtx_used = 0;
    if (models_converted) *models_converted = 0;
}

#endif // D2_T3D_WORLD

#ifdef __cplusplus
}
#endif

#endif // D2_T3D_WORLD_H
