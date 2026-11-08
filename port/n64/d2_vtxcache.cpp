// Driver 2 N64 - Fase 1.1: cada vertice de um modelo e transformado UMA vez (D2_VTXCACHE).
// No original (PS1), Tile1x1/PlotBuildingModel/drawMesh transformam os 4 vertices de cada poligono; numa malha cada vertice e
// compartilhado por ate 4 poligonos, entao a mesma conta era repetida 3 a 4 vezes. Aqui a projecao de cada vertice e guardada
// num cache por modelo (preenchido sob demanda, na primeira vez que um poligono usa o vertice) e os poligonos so leem o cache.
// A conta e a mesma da projecao nativa (d2x_proj em port/compat/d2_xf.h): mesmos SXY/SZ/OTZ que o RTPT/RTPS dariam.
// As versoes originais continuam no motor com o sufixo _orig (macro D2_VC_ORIG em d2_n64_compat.h) e sao descartadas no link.
#include "driver2.h"
#include "C/draw.h"
#include "C/main.h"
#include "C/models.h"
#include "C/system.h"
#include "C/mission.h"
#include "C/tile.h"
#include "C/texture.h"
#include "ASM/rndrasm.h"
#include "d2_origin.h"
#ifdef D2_PERFDBG
#include "d2_perfdbg.h"
#endif

#if defined(D2_VTXCACHE) && defined(D2_XF_NATIVE)

// definidos em draw.c (sem declaracao em header)
struct MVERTEX5x5 { MVERTEX verts[5][5]; };
void ConvertPolygonTypes(MODEL *model, _pct *pc);
extern int combointensity;

// ---------------------------------------------------------------- cache de projecao (indices de vertice de modelo sao u_char)
// uma entrada de 8 bytes por vertice (um acesso de cache de dados): SXY empacotado (x baixo, y alto), SZ e a geracao
struct VCEnt { unsigned int xy; unsigned short z, gen; };
static VCEnt s_vc[256] __attribute__((aligned(16)));
static short s_vcCam[256][3];        // IR1..IR3 (posicao na camera): so as versoes com luz dinamica usam
static unsigned short s_vcCur = 1;
static D2XCam s_vcCam0;
static const SVECTOR *s_vcSrc;

static inline void vc_begin(const SVECTOR *src)
{
	s_vcSrc = src;
	d2x_cam(&s_vcCam0);
	if (++s_vcCur == 0) { for (int i = 0; i < 256; i++) s_vc[i].gen = 0; s_vcCur = 1; }
}
static __attribute__((noinline)) void vc_proj(const SVECTOR *v, unsigned int *xy, unsigned int *z, short *cam)
{
	unsigned int sz, h; int sx, sy, a1, a2, a3;
	d2x_proj(&s_vcCam0, v->vx, v->vy, v->vz, &sx, &sy, &sz, &h, &a1, &a2, &a3);
	*xy = ((unsigned int)(unsigned short)sy << 16) | (unsigned short)sx;
	*z = sz;
	if (cam) { cam[0] = (short)d2x_clamp(a1, -0x8000, 0x7fff); cam[1] = (short)d2x_clamp(a2, -0x8000, 0x7fff); cam[2] = (short)d2x_clamp(a3, -0x8000, 0x7fff); }
}
// projeta o vertice i do modelo (uma vez por modelo); devolve a entrada
static inline const VCEnt *vc_get(int i)
{
	VCEnt *e = &s_vc[i];
	if (__builtin_expect(e->gen != s_vcCur, 0))
	{
		unsigned int xy, z;
		vc_proj(&s_vcSrc[i], &xy, &z, 0);
		e->xy = xy; e->z = (unsigned short)z; e->gen = s_vcCur;
	}
	return e;
}
// versao com luz dinamica: tambem guarda a posicao na camera
static inline const VCEnt *vc_get_lit(int i)
{
	VCEnt *e = &s_vc[i];
	if (__builtin_expect(e->gen != s_vcCur, 0))
	{
		unsigned int xy, z;
		vc_proj(&s_vcSrc[i], &xy, &z, s_vcCam[i]);
		e->xy = xy; e->z = (unsigned short)z; e->gen = s_vcCur;
	}
	return e;
}
static inline short vc_sx(unsigned int xy) { return (short)(xy & 0xffff); }
static inline short vc_sy(unsigned int xy) { return (short)(xy >> 16); }
// NCLIP com os tres SXY
static inline int vc_nclip(unsigned int a, unsigned int b, unsigned int c)
{
	const int x0 = vc_sx(a), y0 = vc_sy(a), x1 = vc_sx(b), y1 = vc_sy(b), x2 = vc_sx(c), y2 = vc_sy(c);
	return x0 * y1 + x1 * y2 + x2 * y0 - x0 * y2 - x1 * y0 - x2 * y1;
}
// AVSZ3/AVSZ4 (OTZ)
static inline int vc_avsz3(unsigned int z1, unsigned int z2, unsigned int z3)
{
	const long long m = (long long)d2xf.zsf3 * (int)(z1 + z2 + z3);
	return d2x_clamp((int)(m >> 12), 0, 0xffff);
}
static inline int vc_avsz4(unsigned int z0, unsigned int z1, unsigned int z2, unsigned int z3)
{
	const long long m = (long long)d2xf.zsf4 * (int)(z0 + z1 + z2 + z3);
	return d2x_clamp((int)(m >> 12), 0, 0xffff);
}
static inline bool vc_saturated4(unsigned int z0, unsigned int z1, unsigned int z2, unsigned int z3, unsigned int h)
{
	return (2 * z0 <= h) || (2 * z1 <= h) || (2 * z2 <= h) || (2 * z3 <= h);
}
static inline void vc_campos(int i, SVECTOR *out) { out->vx = s_vcCam[i][0]; out->vy = s_vcCam[i][1]; out->vz = s_vcCam[i][2]; out->pad = 0; }

// Pacote próprio do N64. Mantém o tamanho/layout compacto do FT4 para caber na
// primtab e na OT original, mas não representa um comando da GPU do PS1.
static inline void d2n_set_ft4(POLY_FT4 *p) { setlen(p, 9); setcode(p, 0xB3); }

// ---------------------------------------------------------------- Tile1x1 (chao, calcadas, agua)
void Tile1x1(MODEL *model)
{
	int Z, ofse, i;
	PL_POLYFT4 *polys;
	u_char ptype;
	POLY_FT4 *prims;

	vc_begin(GET_MODEL_DATA(SVECTOR, model, vertices));
	polys = GET_MODEL_DATA(PL_POLYFT4, model, poly_block);

	if ((model->shape_flags & SHAPE_FLAG_WATER) || (model->flags2 & MODEL_FLAG_GRASS))
		ofse = 229;
	else
		ofse = 133;

	i = model->num_polys;
	while (i-- > 0)
	{
		ptype = polys->id & 0x1f;
		const VCEnt *A = vc_get(polys->v0), *B = vc_get(polys->v1), *C = vc_get(polys->v3);
		const int opz = vc_nclip(A->xy, B->xy, C->xy);
		if (opz > 0)
		{
			const VCEnt *D = vc_get(polys->v2);
			if (vc_saturated4(A->z, B->z, C->z, D->z, s_vcCam0.h))
			{
#ifdef D2_PERFDBG
				g_cull_cnt_tile1x1++;
#endif
				polys = (PL_POLYFT4 *)((char *)polys + plotContext.polySizes[ptype]);
				continue;
			}
			prims = (POLY_FT4 *)plotContext.primptr;

			d2_st_b4(&prims->r0, plotContext.colour);
			d2n_set_ft4(prims);

			d2_st_ss(&prims->x0, A->xy);
			d2_st_ss(&prims->x1, B->xy);
			d2_st_ss(&prims->x2, C->xy);
			d2_st_ss(&prims->x3, D->xy);
			Z = vc_avsz4(A->z, B->z, C->z, D->z);

			prims->tpage = (*plotContext.ptexture_pages)[polys->texture_set];
			prims->clut = (*plotContext.ptexture_cluts)[polys->texture_set][polys->texture_id];

			*(ushort *)&prims->u0 = *(ushort *)&polys->uv0;
			*(ushort *)&prims->u1 = *(ushort *)&polys->uv1;
			*(ushort *)&prims->u2 = *(ushort *)&polys->uv3;
			*(ushort *)&prims->u3 = *(ushort *)&polys->uv2;

#ifdef D2_ORIGIN
			short _sxy[4][2] = { {vc_sx(A->xy), vc_sy(A->xy)}, {vc_sx(B->xy), vc_sy(B->xy)}, {vc_sx(C->xy), vc_sy(C->xy)}, {vc_sx(D->xy), vc_sy(D->xy)} };
			short _sz[4] = { (short)A->z, (short)B->z, (short)C->z, (short)D->z };
			d2_origin_tag_cull(prims, "Tile1x1", -1, NULL, ptype, _sxy, _sz, opz, Z);
#endif
			addPrim(plotContext.ot + (Z >> 1) + ofse, prims);
			plotContext.primptr += sizeof(POLY_FT4);
		}
		polys = (PL_POLYFT4 *)((char *)polys + plotContext.polySizes[ptype]);
	}
	plotContext.current->primptr = plotContext.primptr;
}

#ifdef DYNAMIC_LIGHTING
// ---------------------------------------------------------------- Tile1x1Lit (com luzes dinamicas)
void Tile1x1Lit(MODEL *model)
{
	int Z, ofse, i;
	PL_POLYFT4 *polys;
	u_char ptype;
	POLY_GT4 *prims;

	vc_begin(GET_MODEL_DATA(SVECTOR, model, vertices));
	polys = GET_MODEL_DATA(PL_POLYFT4, model, poly_block);

	if ((model->shape_flags & SHAPE_FLAG_WATER) || (model->flags2 & MODEL_FLAG_GRASS))
		ofse = 229;
	else
		ofse = 133;

	i = model->num_polys;
	while (i-- > 0)
	{
		ptype = polys->id & 0x1f;
		const VCEnt *A = vc_get_lit(polys->v0), *B = vc_get_lit(polys->v1), *C = vc_get_lit(polys->v3);
		const int opz = vc_nclip(A->xy, B->xy, C->xy);
		if (opz > 0)
		{
			const VCEnt *D = vc_get_lit(polys->v2);
			if (vc_saturated4(A->z, B->z, C->z, D->z, s_vcCam0.h))
			{
#ifdef D2_PERFDBG
				g_cull_cnt_tile1x1lit++;
#endif
				polys = (PL_POLYFT4 *)((char *)polys + plotContext.polySizes[ptype]);
				continue;
			}
			prims = (POLY_GT4 *)plotContext.primptr;

			d2_st_b4(&prims->r0, plotContext.colour);
			d2_st_b4(&prims->r1, plotContext.colour);
			d2_st_b4(&prims->r2, plotContext.colour);
			d2_st_b4(&prims->r3, plotContext.colour);
			setPolyGT4(prims);

			d2_st_ss(&prims->x0, A->xy);
			d2_st_ss(&prims->x1, B->xy);
			d2_st_ss(&prims->x2, C->xy);
			d2_st_ss(&prims->x3, D->xy);
			Z = vc_avsz4(A->z, B->z, C->z, D->z);

			prims->tpage = (*plotContext.ptexture_pages)[polys->texture_set];
			prims->clut = (*plotContext.ptexture_cluts)[polys->texture_set][polys->texture_id];

			*(ushort *)&prims->u0 = *(ushort *)&polys->uv0;
			*(ushort *)&prims->u1 = *(ushort *)&polys->uv1;
			*(ushort *)&prims->u2 = *(ushort *)&polys->uv3;
			*(ushort *)&prims->u3 = *(ushort *)&polys->uv2;

			SVECTOR tmpPos;
			vc_campos(polys->v0, &tmpPos); GetDLightLevel(&tmpPos, (u_int *)&prims->r0);
			vc_campos(polys->v1, &tmpPos); GetDLightLevel(&tmpPos, (u_int *)&prims->r1);
			vc_campos(polys->v3, &tmpPos); GetDLightLevel(&tmpPos, (u_int *)&prims->r2);
			vc_campos(polys->v2, &tmpPos); GetDLightLevel(&tmpPos, (u_int *)&prims->r3);

#ifdef D2_ORIGIN
			short _sxy[4][2] = { {vc_sx(A->xy), vc_sy(A->xy)}, {vc_sx(B->xy), vc_sy(B->xy)}, {vc_sx(C->xy), vc_sy(C->xy)}, {vc_sx(D->xy), vc_sy(D->xy)} };
			short _sz[4] = { (short)A->z, (short)B->z, (short)C->z, (short)D->z };
			d2_origin_tag_cull(prims, "Tile1x1Lit", -1, NULL, ptype, _sxy, _sz, opz, Z);
#endif
			addPrim(plotContext.ot + (Z >> 1) + ofse, prims);
			plotContext.primptr += sizeof(POLY_GT4);
		}
		polys = (PL_POLYFT4 *)((char *)polys + plotContext.polySizes[ptype]);
	}
	plotContext.current->primptr = plotContext.primptr;
}
#endif

// ---------------------------------------------------------------- drawMesh / drawMeshLit (quad subdividido por makeMesh)
// makeMesh deixa 4 sub-quads com 9 vertices distintos: v1 p1 p3 p5 v2 p4 p2 v3 v4 (ver tile.c)
static const unsigned char s_meshMap[4][4] = { { 0, 1, 2, 3 }, { 1, 4, 3, 5 }, { 3, 5, 6, 7 }, { 2, 3, 8, 6 } };
static inline void mesh_project(MVERTEX (*VSP)[5][5], int numPolys, unsigned int *xy, unsigned int *z, short (*cam)[3], const MVERTEX **src)
{
	d2x_cam(&s_vcCam0);
	if (numPolys == 1)
	{
		for (int k = 0; k < 4; k++) { src[k] = &(*VSP)[0][k]; vc_proj((const SVECTOR *)src[k], &xy[k], &z[k], cam ? cam[k] : 0); }
		return;
	}
	const MVERTEX *u[9] = { &(*VSP)[0][0], &(*VSP)[0][1], &(*VSP)[0][2], &(*VSP)[0][3], &(*VSP)[1][1], &(*VSP)[1][3], &(*VSP)[2][2], &(*VSP)[2][3], &(*VSP)[3][2] };
	for (int k = 0; k < 9; k++) { src[k] = u[k]; vc_proj((const SVECTOR *)u[k], &xy[k], &z[k], cam ? cam[k] : 0); }
}

void drawMesh(MVERTEX (*VSP)[5][5], int m, int n, _pct *pc)
{
	POLY_FT4 *prim = (POLY_FT4 *)pc->primptr;
	const int numPolys = n < 2 ? 1 : 4;
	unsigned int xy[9], z[9]; const MVERTEX *src[9];
	mesh_project(VSP, numPolys, xy, z, 0, src);

	for (int index = 0; index < numPolys; index++)
	{
		const unsigned char *mp = numPolys == 1 ? s_meshMap[0] : s_meshMap[index];
		d2n_set_ft4(prim);
		d2_st_b4(&prim->r0, pc->colour);

		int opz = vc_nclip(xy[mp[0]], xy[mp[1]], xy[mp[2]]);
		const int zz = vc_avsz3(z[mp[0]], z[mp[1]], z[mp[2]]);
		if (pc->flags & (PLOT_NO_CULL | PLOT_INV_CULL))
		{
			if (pc->flags & PLOT_NO_CULL) opz = 1;
			else opz = -opz;
		}
		if (opz > 0 && zz > 5)
		{
			if (vc_saturated4(z[mp[0]], z[mp[1]], z[mp[2]], z[mp[3]], s_vcCam0.h))
			{
#ifdef D2_PERFDBG
				g_cull_cnt_drawmesh++;
#endif
				continue;
			}

			d2_st_ss(&prim->x0, xy[mp[0]]);
			d2_st_ss(&prim->x1, xy[mp[1]]);
			d2_st_ss(&prim->x2, xy[mp[2]]);
			d2_st_ss(&prim->x3, xy[mp[3]]);

			*(ushort *)&prim->u0 = (*VSP)[index][0].uv.val;
			*(ushort *)&prim->u1 = (*VSP)[index][1].uv.val;
			*(ushort *)&prim->u2 = (*VSP)[index][2].uv.val;
			*(ushort *)&prim->u3 = (*VSP)[index][3].uv.val;

			prim->clut = pc->clut;
			prim->tpage = pc->tpage;
#ifdef D2_ORIGIN
			short _sxy[4][2] = { {vc_sx(xy[mp[0]]), vc_sy(xy[mp[0]])}, {vc_sx(xy[mp[1]]), vc_sy(xy[mp[1]])}, {vc_sx(xy[mp[2]]), vc_sy(xy[mp[2]])}, {vc_sx(xy[mp[3]]), vc_sy(xy[mp[3]])} };
			short _sz[4] = { (short)z[mp[0]], (short)z[mp[1]], (short)z[mp[2]], (short)z[mp[3]] };
			d2_origin_tag_cull(prim, "drawMesh", -1, NULL, 0, _sxy, _sz, opz, zz);
#endif
			addPrim(pc->ot + (zz >> 1), prim);
			prim++;
		}
	}
	pc->primptr = (char *)prim;
}

#ifdef DYNAMIC_LIGHTING
void drawMeshLit(MVERTEX (*VSP)[5][5], int m, int n, _pct *pc)
{
	POLY_GT4 *prim = (POLY_GT4 *)pc->primptr;
	const int numPolys = n < 2 ? 1 : 4;
	unsigned int xy[9], z[9]; short cam[9][3]; const MVERTEX *src[9];
	mesh_project(VSP, numPolys, xy, z, cam, src);

	for (int index = 0; index < numPolys; index++)
	{
		const unsigned char *mp = numPolys == 1 ? s_meshMap[0] : s_meshMap[index];
		setPolyGT4(prim);

		int opz = vc_nclip(xy[mp[0]], xy[mp[1]], xy[mp[2]]);
		const int zz = vc_avsz3(z[mp[0]], z[mp[1]], z[mp[2]]);
		if (pc->flags & (PLOT_NO_CULL | PLOT_INV_CULL))
		{
			if (pc->flags & PLOT_NO_CULL) opz = 1;
			else opz = -opz;
		}
		if (opz > 0 && zz > 5)
		{
			if (vc_saturated4(z[mp[0]], z[mp[1]], z[mp[2]], z[mp[3]], s_vcCam0.h))
			{
#ifdef D2_PERFDBG
				g_cull_cnt_drawmeshlit++;
#endif
				continue;
			}

			d2_st_ss(&prim->x0, xy[mp[0]]);
			d2_st_ss(&prim->x1, xy[mp[1]]);
			d2_st_ss(&prim->x2, xy[mp[2]]);
			d2_st_ss(&prim->x3, xy[mp[3]]);

			d2_st_b4(&prim->r0, plotContext.colour);
			d2_st_b4(&prim->r1, plotContext.colour);
			d2_st_b4(&prim->r2, plotContext.colour);
			d2_st_b4(&prim->r3, plotContext.colour);

			SVECTOR tmpPos;
			u_int *cols[4] = { (u_int *)&prim->r0, (u_int *)&prim->r1, (u_int *)&prim->r2, (u_int *)&prim->r3 };
			for (int k = 0; k < 4; k++)
			{
				tmpPos.vx = cam[mp[k]][0]; tmpPos.vy = cam[mp[k]][1]; tmpPos.vz = cam[mp[k]][2]; tmpPos.pad = 0;
				GetDLightLevel(&tmpPos, cols[k]);
			}
			setPolyGT4(prim);

			*(ushort *)&prim->u0 = (*VSP)[index][0].uv.val;
			*(ushort *)&prim->u1 = (*VSP)[index][1].uv.val;
			*(ushort *)&prim->u2 = (*VSP)[index][2].uv.val;
			*(ushort *)&prim->u3 = (*VSP)[index][3].uv.val;

			prim->clut = pc->clut;
			prim->tpage = pc->tpage;
#ifdef D2_ORIGIN
			short _sxy[4][2] = { {vc_sx(xy[mp[0]]), vc_sy(xy[mp[0]])}, {vc_sx(xy[mp[1]]), vc_sy(xy[mp[1]])}, {vc_sx(xy[mp[2]]), vc_sy(xy[mp[2]])}, {vc_sx(xy[mp[3]]), vc_sy(xy[mp[3]])} };
			short _sz[4] = { (short)z[mp[0]], (short)z[mp[1]], (short)z[mp[2]], (short)z[mp[3]] };
			d2_origin_tag_cull(prim, "drawMeshLit", -1, NULL, 0, _sxy, _sz, opz, zz);
#endif
			addPrim(pc->ot + (zz >> 1), prim);
			prim++;
		}
	}
	pc->primptr = (char *)prim;
}
#endif

// ---------------------------------------------------------------- predios
void PlotBuildingModel(MODEL *model, int rot, _pct *pc)
{
	int Z, r, i;
	PL_POLYFT4 *polys;
	u_char ptype;
	POLY_FT4 *prims;
	int combo;

	vc_begin(GET_MODEL_DATA(SVECTOR, model, vertices));
	polys = GET_MODEL_DATA(PL_POLYFT4, model, poly_block);
	combo = combointensity;
	ConvertPolygonTypes(model, pc);
	r = (rot >> 3) * 4;

	i = model->num_polys;
	while (i-- > 0)
	{
		ptype = polys->id & 31;
		if (ptype != 11 && ptype != 21 && ptype != 23)
		{
			polys = (PL_POLYFT4 *)((char *)polys + pc->polySizes[ptype]);
			continue;
		}
		const VCEnt *A = vc_get(polys->v0), *B = vc_get(polys->v1), *C = vc_get(polys->v3);
		const int opz = vc_nclip(A->xy, B->xy, C->xy);

		if (ptype == 21)
			pc->colour = combo & 0x2ffffff | 0x2c000000;
		else
			pc->colour = pc->f4colourTable[r - polys->th & 31];

		if (opz > 0)
		{
			const VCEnt *D = vc_get(polys->v2);
			if (vc_saturated4(A->z, B->z, C->z, D->z, s_vcCam0.h))
			{
#ifdef D2_PERFDBG
				g_cull_cnt_bld++;
#endif
				polys = (PL_POLYFT4 *)((char *)polys + pc->polySizes[ptype]);
				continue;
			}
			pc->tpage = (*pc->ptexture_pages)[polys->texture_set];
			pc->clut = (*pc->ptexture_cluts)[polys->texture_set][polys->texture_id];

			prims = (POLY_FT4 *)pc->primptr;
			d2n_set_ft4(prims);
			d2_st_b4(&prims->r0, pc->colour);

			d2_st_ss(&prims->x0, A->xy);
			d2_st_ss(&prims->x1, B->xy);
			d2_st_ss(&prims->x2, C->xy);
			d2_st_ss(&prims->x3, D->xy);
			Z = vc_avsz4(A->z, B->z, C->z, D->z);

			prims->tpage = pc->tpage;
			prims->clut = pc->clut;

			ushort uv0, uv1, uv2, uv3;
			if (ptype == 23)
			{
				POLYGT4 *pgt4 = (POLYGT4 *)polys;
				uv0 = *(ushort *)&pgt4->uv0; uv1 = *(ushort *)&pgt4->uv1; uv2 = *(ushort *)&pgt4->uv2; uv3 = *(ushort *)&pgt4->uv3;
			}
			else
			{
				uv0 = *(ushort *)&polys->uv0; uv1 = *(ushort *)&polys->uv1; uv2 = *(ushort *)&polys->uv2; uv3 = *(ushort *)&polys->uv3;
			}

			*(ushort *)&prims->u0 = uv0;
			*(ushort *)&prims->u1 = uv1;
			*(ushort *)&prims->u2 = uv3;
			*(ushort *)&prims->u3 = uv2;

#ifdef D2_ORIGIN
			short _sxy[4][2] = { {vc_sx(A->xy), vc_sy(A->xy)}, {vc_sx(B->xy), vc_sy(B->xy)}, {vc_sx(C->xy), vc_sy(C->xy)}, {vc_sx(D->xy), vc_sy(D->xy)} };
			short _sz[4] = { (short)A->z, (short)B->z, (short)C->z, (short)D->z };
			d2_origin_tag_cull(prims, "PlotBuildingModel", -1, NULL, ptype, _sxy, _sz, opz, Z);
#endif
			addPrim(pc->ot + (Z >> 1), prims);
			pc->primptr += sizeof(POLY_FT4);
		}
		polys = (PL_POLYFT4 *)((char *)polys + pc->polySizes[ptype]);
	}
}

void PlotBuildingModelSubdivNxN(MODEL *model, int rot, _pct *pc, int n)
{
	int Z, r, i;
	int diff, minZ, maxZ;
	PL_POLYFT4 *polys;
	u_char ptype;
	POLY_FT4 *prims;
	SVECTOR *srcVerts;
	int combo;
	MVERTEX5x5 subdiVerts;

	srcVerts = GET_MODEL_DATA(SVECTOR, model, vertices);
	vc_begin(srcVerts);
	polys = GET_MODEL_DATA(PL_POLYFT4, model, poly_block);
	combo = combointensity;
	ConvertPolygonTypes(model, pc);
	r = (rot >> 3) * 4;

	i = model->num_polys;
	while (i-- > 0)
	{
		ptype = polys->id & 31;
		if (ptype != 11 && ptype != 21 && ptype != 23)
		{
			polys = (PL_POLYFT4 *)((char *)polys + pc->polySizes[ptype]);
			continue;
		}
		const VCEnt *A = vc_get(polys->v0), *B = vc_get(polys->v1), *C = vc_get(polys->v3);
		const int opz = vc_nclip(A->xy, B->xy, C->xy);

		if (ptype == 21)
			pc->colour = combo & 0x2ffffffU | 0x2c000000;
		else
			pc->colour = pc->f4colourTable[r - polys->th & 31];

		if (opz > 0)
		{
			pc->scribble[0] = A->z; pc->scribble[1] = B->z; pc->scribble[2] = C->z;

			pc->tpage = (*pc->ptexture_pages)[polys->texture_set];
			pc->clut = (*pc->ptexture_cluts)[polys->texture_set][polys->texture_id];

			minZ = pc->scribble[2];
			if (pc->scribble[1] < minZ) minZ = pc->scribble[1];
			if (pc->scribble[0] < minZ) minZ = pc->scribble[0];

			maxZ = pc->scribble[2];
			if (maxZ < pc->scribble[1]) maxZ = pc->scribble[1];

			diff = maxZ - minZ;
			if (maxZ < pc->scribble[0]) diff = pc->scribble[0] - minZ;

			ushort uv0, uv1, uv2, uv3;
			if (ptype == 23)
			{
				POLYGT4 *pgt4 = (POLYGT4 *)polys;
				uv0 = *(ushort *)&pgt4->uv0; uv1 = *(ushort *)&pgt4->uv1; uv2 = *(ushort *)&pgt4->uv2; uv3 = *(ushort *)&pgt4->uv3;
			}
			else
			{
				uv0 = *(ushort *)&polys->uv0; uv1 = *(ushort *)&polys->uv1; uv2 = *(ushort *)&polys->uv2; uv3 = *(ushort *)&polys->uv3;
			}

			if (n == 0 || diff << 2 <= minZ - 350)
			{
				const VCEnt *D = vc_get(polys->v2);
				if (vc_saturated4(A->z, B->z, C->z, D->z, s_vcCam0.h))
				{
#ifdef D2_PERFDBG
					g_cull_cnt_bld_subdiv++;
#endif
					polys = (PL_POLYFT4 *)((char *)polys + pc->polySizes[ptype]);
					continue;
				}
				prims = (POLY_FT4 *)pc->primptr;
				d2n_set_ft4(prims);
				d2_st_b4(&prims->r0, pc->colour);

				d2_st_ss(&prims->x0, A->xy);
				d2_st_ss(&prims->x1, B->xy);
				d2_st_ss(&prims->x2, C->xy);
				d2_st_ss(&prims->x3, D->xy);
				Z = vc_avsz4(A->z, B->z, C->z, D->z);

				prims->tpage = pc->tpage;
				prims->clut = pc->clut;

				*(ushort *)&prims->u0 = uv0;
				*(ushort *)&prims->u1 = uv1;
				*(ushort *)&prims->u2 = uv3;
				*(ushort *)&prims->u3 = uv2;

#ifdef D2_ORIGIN
				short _sxy[4][2] = { {vc_sx(A->xy), vc_sy(A->xy)}, {vc_sx(B->xy), vc_sy(B->xy)}, {vc_sx(C->xy), vc_sy(C->xy)}, {vc_sx(D->xy), vc_sy(D->xy)} };
				short _sz[4] = { (short)A->z, (short)B->z, (short)C->z, (short)D->z };
				d2_origin_tag_cull(prims, "PlotBuildingModelSubdivNxN", -1, NULL, ptype, _sxy, _sz, opz, Z);
#endif
				addPrim(pc->ot + (Z >> 1), prims);
				pc->primptr += sizeof(POLY_FT4);
			}
			else
			{
				int sub = n;
				if (n == 1)
				{
					if (minZ - 150 < (diff << 1)) sub = 4;
					else sub = 2;
				}

				copyVector(&subdiVerts.verts[0][0], &srcVerts[polys->v0]);
				subdiVerts.verts[0][0].uv.val = uv0;
				copyVector(&subdiVerts.verts[0][1], &srcVerts[polys->v1]);
				subdiVerts.verts[0][1].uv.val = uv1;
				copyVector(&subdiVerts.verts[0][2], &srcVerts[polys->v3]);
				subdiVerts.verts[0][2].uv.val = uv3;
				copyVector(&subdiVerts.verts[0][3], &srcVerts[polys->v2]);
				subdiVerts.verts[0][3].uv.val = uv2;

				makeMesh((MVERTEX(*)[5][5])subdiVerts.verts, sub, sub);
				drawMesh((MVERTEX(*)[5][5])subdiVerts.verts, sub, sub, pc);
			}
		}
		polys = (PL_POLYFT4 *)((char *)polys + pc->polySizes[ptype]);
	}
}

#endif // D2_VTXCACHE && D2_XF_NATIVE
