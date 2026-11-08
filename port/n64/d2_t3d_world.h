#ifndef D2_T3D_WORLD_H
#define D2_T3D_WORLD_H

#ifdef __cplusplus
extern "C" {
#endif


void d2_t3d_init(void);
void d2_t3d_on_models_loaded(void);
int  d2_t3d_has_model(int model_idx);
void d2_t3d_set_tile_tint(unsigned int psx_colour);
void d2_t3d_queue_tile(int model_idx, int yang, int z, int tx, int ty, int tz);
void d2_t3d_queue_building(int model_idx, int yang, int z, int tx, int ty, int tz);
void d2_t3d_draw_queued_buildings(void);
int d2_t3d_begin_ordered_world(void);
void d2_t3d_draw_world_until(int bucket);
void d2_t3d_end_ordered_world(void);
int  d2_t3d_get_queued_count(void);
void d2_t3d_get_stats(int* vtx_total, int* vtx_used, int* models_converted);


#ifdef __cplusplus
}
#endif

#endif // D2_T3D_WORLD_H
