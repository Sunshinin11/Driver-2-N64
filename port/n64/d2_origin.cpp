#include "d2_origin.h"
#include <libdragon.h>
#include <string.h>
#include <stdio.h>

#ifdef D2_ORIGIN

#define ORIGIN_TABLE_SIZE 8192
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

#endif // D2_ORIGIN

