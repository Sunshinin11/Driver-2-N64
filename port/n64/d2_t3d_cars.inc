// Carroceria dos carros (POLY_GT3) transformada pelo RSP (Tiny3D) em vez de projetada triangulo a triangulo na CPU.
// O modelo, as texturas, as paletas e a luz por vertice sao os do jogo: so muda quem faz a conta da geometria.
// - Topologia (cantos unicos, grupos por janela de textura 64x64) calculada uma vez por modelo de carro.
// - Por quadro e por carro: copia posicao (vlist, ja com a deformacao) e cor (nlist) para vertices do T3D e deixa um
//   marcador na OT na profundidade do carro; o percurso da OT desenha a carroceria inteira nesse ponto.
// - Carros com dano de textura (UV deslocado), noite, luz dinamica ou triangulos que nao cabem numa janela ficam no
//   caminho original (plotCarPolyGT3), no todo ou so nesses triangulos.
extern "C" const unsigned char* d2_rdp_shade_lut(void);
extern "C" int d2_rdp_frame_count(void);

enum { D2_CAR_CODE = 0xB5, D2_CAR_MAX_INST = 24, D2_CAR_MAX_BAKE = 40, D2_CAR_GROUP_VERTS = 64 };
struct D2CarPacket { P_TAG tag; unsigned int inst; };

struct D2CarCorner { uint8_t v, n; int16_t s, t; };
struct D2CarGroup { uint16_t firstCorner, firstTri; uint8_t ncorners, ntris, wu0, wv0; uint16_t tpage, clutIdx; };
struct D2CarTri { uint8_t a, b, c; };
struct D2CarBake {
    const CAR_POLY* src; int num;          // chave: lista de poligonos do modelo
    D2CarCorner* corners; D2CarGroup* groups; D2CarTri* tris; CAR_POLY* rest;
    int ncorners, ngroups, ntris, nrest, nverts;
    short bmin[3], bmax[3];                // caixa da carroceria (para a profundidade do marcador na OT)   // nverts: soma dos cantos de cada grupo arredondados para par
    void* block;
};
static D2CarBake s_carBake[D2_CAR_MAX_BAKE];
static int s_carBakeCount;
struct D2CarInst { const D2CarBake* bake; T3DVertPacked* verts; T3DMat4FP* mat; const unsigned short* cluts; int palette; };
static D2CarInst s_carInst[D2_CAR_MAX_INST];
static T3DMat4FP s_carMat[2][D2_CAR_MAX_INST] __attribute__((aligned(16)));
static int s_carInstCount, s_carFrame = -1, s_carVtxCursor, s_carViewFrame = -1;
static unsigned s_carDrawn, s_carOld, s_carTris, s_carRest;

static void d2_car_bake_reset(void)
{
    for (int i = 0; i < s_carBakeCount; i++) if (s_carBake[i].block) free(s_carBake[i].block);
    s_carBakeCount = 0; s_carInstCount = 0;
}

extern "C" void d2_t3d_car_models_changed(void) { d2_car_bake_reset(); }

static const D2CarBake* d2_car_bake(const CAR_POLY* src, int num, const SVECTOR* vlist)
{
    for (int i = 0; i < s_carBakeCount; i++) if (s_carBake[i].src == src && s_carBake[i].num == num) return s_carBake[i].block ? &s_carBake[i] : NULL;
    if (s_carBakeCount >= D2_CAR_MAX_BAKE || num <= 0 || num > 1024) return NULL;
    D2CarBake& bk = s_carBake[s_carBakeCount++];
    bk = {}; bk.src = src; bk.num = num;
    // pior caso: 3 cantos por triangulo, 1 grupo por triangulo
    size_t bytes = (size_t)num * (3 * sizeof(D2CarCorner) + sizeof(D2CarGroup) + sizeof(D2CarTri) + sizeof(CAR_POLY));
    void* block = malloc(bytes);
    if (!block) return NULL;
    D2CarCorner* corners = (D2CarCorner*)block;
    D2CarGroup* groups = (D2CarGroup*)(corners + num * 3);
    D2CarTri* tris = (D2CarTri*)(groups + num);
    CAR_POLY* rest = (CAR_POLY*)(((uintptr_t)(tris + num) + 3) & ~(uintptr_t)3);
    // 1a passada: janela de cada triangulo (ou -1 = fica no caminho original). Cobertura gulosa: para cada (pagina,
    // paleta), escolhe a janela 64x64 (origem multipla de 16) que cobre mais triangulos ainda sem janela, e repete.
    static short win[1024]; static unsigned char used[1024];
    static unsigned char bb[1024][4];      // umin, umax, vmin, vmax
    struct Win { uint16_t tpage, clutIdx; uint8_t wu0, wv0; };
    static Win wins[256]; int nwins = 0;
    for (int i = 0; i < num; i++) {
        const CAR_POLY& p = src[i];
        const int u[3] = { p.clut_uv0 & 0xff, p.tpage_uv1 & 0xff, p.uv3_uv2 & 0xff };
        const int v[3] = { (p.clut_uv0 >> 8) & 0xff, (p.tpage_uv1 >> 8) & 0xff, (p.uv3_uv2 >> 8) & 0xff };
        int umin = u[0], umax = u[0], vmin = v[0], vmax = v[0];
        for (int k = 1; k < 3; k++) { if (u[k] < umin) umin = u[k]; if (u[k] > umax) umax = u[k]; if (v[k] < vmin) vmin = v[k]; if (v[k] > vmax) vmax = v[k]; }
        bb[i][0] = (unsigned char)umin; bb[i][1] = (unsigned char)umax; bb[i][2] = (unsigned char)vmin; bb[i][3] = (unsigned char)vmax;
        used[i] = 0;
        const int tpage = (p.tpage_uv1 >> 16) & 0xffff;
        // -2 = ainda sem janela; -1 = nunca cabe (nao e CI4 ou maior que 64 numa janela alinhada)
        win[i] = (((tpage >> 7) & 3) != 0) ? -1 : -2;
        if (win[i] == -2) {
            int wu0 = umin & ~15, wv0 = vmin & ~15;
            if (wu0 > 192) wu0 = 192;
            if (wv0 > 192) wv0 = 192;
            if (umax > wu0 + 64 || vmax > wv0 + 64) win[i] = -1;
        }
    }
    for (;;) {
        int first = -1;
        for (int i = 0; i < num; i++) if (win[i] == -2) { first = i; break; }
        if (first < 0 || nwins >= 256) break;
        const int tpage = (src[first].tpage_uv1 >> 16) & 0xffff, clutIdx = (src[first].clut_uv0 >> 16) & 0xffff;
        int best = 0, bu = 0, bv = 0;
        for (int wv0 = 0; wv0 <= 192; wv0 += 16) for (int wu0 = 0; wu0 <= 192; wu0 += 16) {
            int n = 0;
            for (int i = first; i < num; i++)
                if (win[i] == -2 && bb[i][0] >= wu0 && bb[i][1] <= wu0 + 64 && bb[i][2] >= wv0 && bb[i][3] <= wv0 + 64 &&
                    ((src[i].tpage_uv1 >> 16) & 0xffff) == tpage && ((src[i].clut_uv0 >> 16) & 0xffff) == clutIdx) n++;
            if (n > best) { best = n; bu = wu0; bv = wv0; }
        }
        if (!best) { win[first] = -1; continue; }
        wins[nwins] = { (uint16_t)tpage, (uint16_t)clutIdx, (uint8_t)bu, (uint8_t)bv };
        for (int i = first; i < num; i++)
            if (win[i] == -2 && bb[i][0] >= bu && bb[i][1] <= bu + 64 && bb[i][2] >= bv && bb[i][3] <= bv + 64 &&
                ((src[i].tpage_uv1 >> 16) & 0xffff) == tpage && ((src[i].clut_uv0 >> 16) & 0xffff) == clutIdx) win[i] = (short)nwins;
        nwins++;
    }
    for (int i = 0; i < num; i++) if (win[i] == -2) win[i] = -1;
    // 2a passada: grupos (janela, ate D2_CAR_GROUP_VERTS cantos), na ordem dos triangulos do modelo dentro de cada janela
    int nc = 0, ng = 0, nt = 0, nrest = 0, nverts = 0;
    for (int i = 0; i < num; i++) if (win[i] < 0) rest[nrest++] = src[i];
    for (int w = 0; w < nwins; w++) {
        int i = 0;
        while (i < num) {
            D2CarGroup g = {}; g.firstCorner = (uint16_t)nc; g.firstTri = (uint16_t)nt;
            g.wu0 = wins[w].wu0; g.wv0 = wins[w].wv0; g.tpage = wins[w].tpage; g.clutIdx = wins[w].clutIdx;
            int gc = 0, gt = 0;
            for (; i < num; i++) {
                if (win[i] != w || used[i]) continue;
                const CAR_POLY& p = src[i];
                const unsigned uvw[3] = { (unsigned)p.clut_uv0 & 0xffff, (unsigned)p.tpage_uv1 & 0xffff, (unsigned)p.uv3_uv2 & 0xffff };
                int idx[3], added = 0;
                for (int k = 0; k < 3; k++) {
                    D2CarCorner c;
                    c.v = (uint8_t)((p.vindices >> (8 * k)) & 0xff); c.n = (uint8_t)((p.nindices >> (8 * k)) & 0xff);
                    c.s = (int16_t)((((int)uvw[k] & 0xff) - g.wu0) * 32); c.t = (int16_t)((((int)(uvw[k] >> 8) & 0xff) - g.wv0) * 32);
                    int f = -1;
                    for (int q = 0; q < gc + added; q++) { const D2CarCorner& o = corners[nc + q]; if (o.v == c.v && o.n == c.n && o.s == c.s && o.t == c.t) { f = q; break; } }
                    if (f < 0) { f = gc + added; corners[nc + f] = c; added++; }
                    idx[k] = f;
                }
                if (gc + added > D2_CAR_GROUP_VERTS || gt >= 255) break;    // grupo cheio: este triangulo abre o proximo
                gc += added; used[i] = 1;
                tris[nt + gt] = { (uint8_t)idx[0], (uint8_t)idx[1], (uint8_t)idx[2] }; gt++;
            }
            if (!gt) break;
            g.ncorners = (uint8_t)gc; g.ntris = (uint8_t)gt;
            groups[ng++] = g; nc += gc; nt += gt; nverts += (gc + 1) & ~1;
            if (i >= num) { // ainda pode haver triangulos desta janela que ficaram para tras (grupo cheio)
                bool left = false;
                for (int q = 0; q < num; q++) if (win[q] == w && !used[q]) { left = true; break; }
                if (left) i = 0;
            }
        }
    }
    {   // caixa da carroceria
        int mn[3] = { 32767, 32767, 32767 }, mx[3] = { -32768, -32768, -32768 };
        for (int q = 0; q < nc; q++) {
            const SVECTOR& p = vlist[corners[q].v]; const int c[3] = { p.vx, p.vy, p.vz };
            for (int k = 0; k < 3; k++) { if (c[k] < mn[k]) mn[k] = c[k]; if (c[k] > mx[k]) mx[k] = c[k]; }
        }
        for (int k = 0; k < 3; k++) { bk.bmin[k] = (short)mn[k]; bk.bmax[k] = (short)mx[k]; }
    }
    bk.corners = corners; bk.groups = groups; bk.tris = tris; bk.rest = rest;
    bk.ncorners = nc; bk.ngroups = ng; bk.ntris = nt; bk.nrest = nrest; bk.nverts = nverts; bk.block = block;
    debugf("[CAR_BAKE] polys=%d grupos=%d cantos=%d tris=%d fora=%d janelas=%d\n", num, ng, nc, nt, nrest, nwins);
    return &bk;
}

// Chamado pelo motor no lugar de plotCarPolyGT3 (de dia, sem luz dinamica). Devolve 0 = nao tratou (o motor desenha tudo
// como antes); 1 = carroceria na fila do T3D; *rest/*nrest = triangulos que o motor ainda deve desenhar pelo caminho original.
extern "C" int d2_t3d_car_body(int numGT3, CAR_POLY* pGT3, SVECTOR* vlist, SVECTOR* nlist, unsigned short* pciv_clut,
                               unsigned char* damageLevel, int palette, unsigned char** primptr, void* ot_base,
                               CAR_POLY** rest, int* nrest)
{
#ifdef D2_NO_T3D_CARS
    return 0;
#else
    if (!s_t3d_inited || numGT3 <= 0) return 0;
    const int frame = d2_rdp_frame_count();
    if (frame != s_carFrame) { s_carFrame = frame; s_carInstCount = 0; s_carVtxCursor = 0; }
    if (s_carInstCount >= D2_CAR_MAX_INST) { s_carOld++; return 0; }
    for (int i = 0; i < numGT3; i++) if (damageLevel[pGT3[i].originalindex]) { s_carOld++; return 0; }   // textura de dano
    const D2CarBake* bk = d2_car_bake(pGT3, numGT3, vlist);
    const int capacity = D2_T3D_MAX_VERTS / 2;
    if (!bk || !bk->ngroups || s_carVtxCursor + bk->nverts > capacity) { s_carOld++; return 0; }
    const int buf = frame & 1;
    T3DVertPacked* verts = &s_t3d_vtx_pool[(buf * capacity + s_carVtxCursor) / 2];
    s_carVtxCursor += bk->nverts;
    // vertices: posicao e cor deste carro neste quadro
    const unsigned char* lut = d2_rdp_shade_lut();
    {
        int k = 0;
        for (int g = 0; g < bk->ngroups; g++) {
            const D2CarGroup& grp = bk->groups[g];
            const D2CarCorner* c = bk->corners + grp.firstCorner;
            const int aligned = (grp.ncorners + 1) & ~1;
            for (int q = 0; q < aligned; q++, k++) {
                const D2CarCorner& cc = c[q < grp.ncorners ? q : 0];
                const SVECTOR& p = vlist[cc.v];
                const unsigned pad = (unsigned short)nlist[cc.n].pad;
                const uint32_t rgba = ((uint32_t)lut[pad & 0xff] << 24) | ((uint32_t)lut[(pad >> 8) & 0xff] << 16) | ((uint32_t)lut[pad & 0xff] << 8) | 255u;
                T3DVertPacked& pk = verts[k / 2];
                if (k & 1) { pk.posB[0] = p.vx; pk.posB[1] = p.vy; pk.posB[2] = p.vz; pk.normB = 0; pk.rgbaB = rgba; pk.stB[0] = cc.s; pk.stB[1] = cc.t; }
                else       { pk.posA[0] = p.vx; pk.posA[1] = p.vy; pk.posA[2] = p.vz; pk.normA = 0; pk.rgbaA = rgba; pk.stA[0] = cc.s; pk.stA[1] = cc.t; }
            }
        }
        data_cache_hit_writeback(verts, (size_t)bk->nverts * 16);
    }
    // matriz modelo -> camera (a mesma rotacao/translacao que o motor carregou para este carro)
    T3DMat4FP* mat = &s_carMat[buf][s_carInstCount];
    {
        const float k = 1.0f / 4096.0f;
        T3DMat4 m;
        for (int c = 0; c < 3; c++) { m.m[c][0] = (float)d2xf.rot[0][c] * k; m.m[c][1] = (float)d2xf.rot[1][c] * k; m.m[c][2] = (float)d2xf.rot[2][c] * k; m.m[c][3] = 0.0f; }
        m.m[3][0] = (float)d2xf.tr[0]; m.m[3][1] = (float)d2xf.tr[1]; m.m[3][2] = (float)d2xf.tr[2]; m.m[3][3] = 1.0f;
        t3d_mat4_to_fixed(mat, &m);
        data_cache_hit_writeback(mat, sizeof(T3DMat4FP));
    }
    s_carInst[s_carInstCount] = { bk, verts, mat, pciv_clut, palette };
    // marcador na OT, na profundidade do centro do carro (mesma escala do OTZ dos triangulos)
    {
        // profundidade um pouco alem do canto mais proximo da caixa: a carroceria cobre as rodas traseiras/fundo, mas
        // pedestres e postes ao lado do carro ficam na frente dela
        int zn = 0, zf = 0;
        for (int k = 0; k < 3; k++) {
            const int a = d2xf.rot[2][k] * bk->bmin[k], b = d2xf.rot[2][k] * bk->bmax[k];
            zn += a < b ? a : b; zf += a < b ? b : a;
        }
        zn = d2xf.tr[2] + ((zn + ((zf - zn) * 15) / 100) >> 12);     // 15% do caminho do canto mais proximo ao mais distante
        if (zn < 0) zn = 0;
        long long m = (long long)d2xf.zsf3 * 3 * (long long)zn;
        int otz = (int)(m >> 12);
        if (otz < 1) otz = 1;
        if (otz > 0xffff) otz = 0xffff;
        D2CarPacket* pk = (D2CarPacket*)*primptr;
        setlen(pk, 2); setcode(pk, D2_CAR_CODE);
        pk->inst = (unsigned int)s_carInstCount;
        OTTYPE* ot = (OTTYPE*)ot_base;
        addPrim(ot + (otz >> 1), pk);
        *primptr = (unsigned char*)(pk + 1);
    }
    s_carInstCount++;
    *rest = bk->rest; *nrest = bk->nrest;
    s_carRest += (unsigned)bk->nrest;
    return 1;
#endif
}

// Percurso da OT chegou ao marcador: desenha a carroceria
extern "C" void d2_t3d_car_draw(unsigned int index)
{
    if (index >= (unsigned)s_carInstCount) return;
    const D2CarInst& in = s_carInst[index];
    const D2CarBake* bk = in.bake;
    const int frame = d2_rdp_frame_count();
    d2_rdp_invalidate_backend_cache();
    if (s_carViewFrame != frame) {
        s_carViewFrame = frame;
        float h = scr_z > 0 ? (float)scr_z : 256.0f;
        T3DVec3 eye = {{0,0,0}}, target = {{0,0,256}}, up = {{0,-1,0}};
        t3d_viewport_set_projection(&s_t3d_viewport, 2.0f * atanf(120.0f / h), 16.0f, 32768.0f);
        t3d_viewport_look_at(&s_t3d_viewport, &eye, &target, &up);
        t3d_viewport_attach(&s_t3d_viewport);
    }
    rdpq_mode_begin();
    rdpq_set_mode_standard();
    rdpq_mode_persp(true);
    rdpq_mode_filter(FILTER_POINT);
    rdpq_mode_zbuf(false, false);
    rdpq_mode_combiner(RDPQ_COMBINER1((TEX0,ZERO,SHADE,ZERO),(ZERO,ZERO,ZERO,TEX0)));
    rdpq_mode_alphacompare(1);
    rdpq_mode_end();
    t3d_state_set_drawflags((enum T3DDrawFlags)(T3D_FLAG_SHADED | T3D_FLAG_TEXTURED | T3D_FLAG_NO_LIGHT | D2_CAR_CULL));
    t3d_matrix_push_pos(1);
    t3d_matrix_set(in.mat, true);
    const T3DVertPacked* v = in.verts;
    for (int g = 0; g < bk->ngroups; g++) {
        const D2CarGroup& grp = bk->groups[g];
        const int aligned = (grp.ncorners + 1) & ~1;
        int ou, ov;
        const int clut = in.cluts[grp.clutIdx + in.palette];
        if (d2_rdp_t3d_texture(grp.tpage, clut, grp.wu0, grp.wv0, 64, 64, &ou, &ov) && ou == grp.wu0 && ov == grp.wv0) {
            t3d_vert_load(v, 0, aligned);
            const D2CarTri* t = bk->tris + grp.firstTri;
            for (int k = 0; k < grp.ntris; k++) t3d_tri_draw(t[k].a, t[k].b, t[k].c);
            t3d_tri_sync();
            s_carTris += grp.ntris;
        }
        v += aligned / 2;
    }
    t3d_matrix_pop(1);
    d2_rdp_invalidate_backend_cache();
    s_carDrawn++;
    if ((s_carDrawn & 255) == 0) { debugf("[CAR_T3D] t3d=%u antigo=%u tris=%u fora=%u (por 256 carros)\n", 256u, s_carOld, s_carTris, s_carRest); s_carOld = s_carTris = s_carRest = 0; }
}
