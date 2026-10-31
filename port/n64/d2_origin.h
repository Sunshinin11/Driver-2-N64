#ifndef D2_ORIGIN_H
#define D2_ORIGIN_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef D2_ORIGIN

struct D2OriginRecord {
    const void *prim;
    const char *func_name;
    short model_type;
    short ptype;
    int cell_pos[3];
    short sxy[4][2];
    short sz[4];
    int opz;
    int otz;
};

void d2_origin_init(void);
void d2_origin_frame_begin(void);
void d2_origin_set_cell(int model_type, int cx, int cy, int cz);
void d2_origin_set_scope(const char *func_name);
const char *d2_origin_get_scope(void);
void d2_origin_tag(const void *prim, const char *func, int model_type, const int cell_pos[3], int ptype, const short sxy[4][2], const short sz[4]);
void d2_origin_tag_cull(const void *prim, const char *func, int model_type, const int cell_pos[3], int ptype, const short sxy[4][2], const short sz[4], int opz, int otz);
void d2_origin_tag_auto(const void *prim);
void d2_origin_tag_chain(const void *p0, const void *p1);
const struct D2OriginRecord *d2_origin_lookup(const void *prim);

void d2_origin_check_large(const void *prim, int code, int tpage, int clut, const short xs[4], const short ys[4], const unsigned char us[4], const unsigned char vs[4], int nv);
void d2_origin_log_hud(const void *prim, int code, int semi, unsigned int col, const short xs[4], const short ys[4], int nv, int textured, int fmt, int blend);
#ifdef D2_TRANSDBG
void d2_transdbg_inspect(const void *prim, int code, int semi, int bm, int tpage, int clut,
                         const short xs[4], const short ys[4],
                         const unsigned char us[4], const unsigned char vs[4], int nv,
                         const unsigned char cr[4], const unsigned char cg[4], const unsigned char cb[4]);
#endif

#define D2_ORIGIN_SET_CELL(m, x, y, z) d2_origin_set_cell(m, x, y, z)
#define D2_ORIGIN_SET_SCOPE(f)         d2_origin_set_scope(f)
#define D2_ORIGIN_TAG(p, f, m, c, pt, xy, z) d2_origin_tag(p, f, m, c, pt, xy, z)
#define D2_ORIGIN_TAG_CULL(p, f, m, c, pt, xy, z, opz, otz) d2_origin_tag_cull(p, f, m, c, pt, xy, z, opz, otz)
#define D2_ORIGIN_TAG_AUTO(p)          d2_origin_tag_auto(p)
#define D2_ORIGIN_TAG_CHAIN(p0, p1)    d2_origin_tag_chain(p0, p1)

#else // !D2_ORIGIN

#define D2_ORIGIN_SET_CELL(m, x, y, z) ((void)0)
#define D2_ORIGIN_SET_SCOPE(f)         ((void)0)
#define D2_ORIGIN_TAG(p, f, m, c, pt, xy, z) ((void)0)
#define D2_ORIGIN_TAG_CULL(p, f, m, c, pt, xy, z, opz, otz) ((void)0)
#define D2_ORIGIN_TAG_AUTO(p)          ((void)0)
#define D2_ORIGIN_TAG_CHAIN(p0, p1)    ((void)0)

#endif // D2_ORIGIN

#ifdef __cplusplus
}
#endif

#endif // D2_ORIGIN_H

