/* Transformacao 3D nativa do Driver 2 N64 (substitui o GTE do PS1 por completo).
 * Estado de transformacao em campos com nome (matriz atual, translacao, offset/distancia de tela, vertices, resultado),
 * sem numeracao de registrador, sem decodificacao de opcode, sem flags. As funcoes d2x_* aceitam os mesmos ponteiros
 * que o codigo do jogo ja passava; a aritmetica inteira e a mesma do PS1 (conferida contra a versao anterior em teste). */
#ifndef D2_XF_H
#define D2_XF_H
#include "d2_endian.h"

typedef struct D2XF
{
	short rot[3][3];  int tr[3];                 /* matriz de rotacao + translacao atuais */
	short light[3][3]; int bk[3];                /* matriz de luz + cor de fundo (ja <<4) */
	short color[3][3]; int fc[3];                /* matriz de cor + cor de neblina */
	int ofx, ofy;                                /* offset de tela (16.16) */
	unsigned short h; short dqa; int dqb;        /* distancia de projecao e depth cue */
	short zsf3, zsf4;                            /* escala do Z medio */
	short v[3][3];                               /* vertices carregados (V0..V2) */
	short ir[4]; int mac[4];                     /* vetor intermediario e acumuladores */
	short sx[3], sy[3];                          /* XY de tela, fila de 3 */
	unsigned short sz[4]; unsigned short otz;    /* Z de tela, fila de 4, e Z medio */
	unsigned char rgbc[4]; unsigned char rgb[3][4]; /* cor de entrada e fila de cores (r,g,b,code) */
} D2XF;

#ifdef __cplusplus
extern "C" { extern D2XF d2xf; extern const unsigned char d2_unr_table[257]; }
#else
extern D2XF d2xf; extern const unsigned char d2_unr_table[257];
#endif

#define D2X_AI static inline __attribute__((always_inline))
D2X_AI int d2x_clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
D2X_AI int d2x_sh(long long v, int sf) { return (int)(sf ? v >> 12 : v); }
D2X_AI int d2x_lm(int v, int lm) { return d2x_clamp(v, lm ? 0 : -0x8000, 0x7fff); }
D2X_AI int d2x_lc(int v) { return d2x_clamp(v, 0, 255); }

/* ---------------------------------------------------------------- carga de matrizes e vetores */
D2X_AI void d2x_ld_mat(short dst[3][3], const void *p) { const short *s = (const short *)p; for (int i = 0; i < 9; i++) ((short *)dst)[i] = s[i]; }
D2X_AI void d2x_st_mat(void *p, const short src[3][3]) { short *s = (short *)p; for (int i = 0; i < 9; i++) s[i] = ((const short *)src)[i]; s[9] = src[2][2] < 0 ? (short)-1 : (short)0; }
D2X_AI void d2x_SetRotMatrix(const void *m)   { d2x_ld_mat(d2xf.rot, m); }
D2X_AI void d2x_SetTransMatrix(const void *m) { const int *t = (const int *)((const char *)m + 20); d2xf.tr[0] = t[0]; d2xf.tr[1] = t[1]; d2xf.tr[2] = t[2]; }
D2X_AI void d2x_SetTransVector(const void *v) { const int *t = (const int *)v; d2xf.tr[0] = t[0]; d2xf.tr[1] = t[1]; d2xf.tr[2] = t[2]; }
D2X_AI void d2x_SetLightMatrix(const void *m) { d2x_ld_mat(d2xf.light, m); }
D2X_AI void d2x_SetColorMatrix(const void *m) { d2x_ld_mat(d2xf.color, m); }
D2X_AI void d2x_SetBackColor(int r, int g, int b) { d2xf.bk[0] = r << 4; d2xf.bk[1] = g << 4; d2xf.bk[2] = b << 4; }
D2X_AI void d2x_SetGeomOffset(int x, int y) { d2xf.ofx = x << 16; d2xf.ofy = y << 16; }
D2X_AI void d2x_ReadColorMatrix(void *m) { d2x_st_mat(m, d2xf.color); int *t = (int *)((char *)m + 20); t[0] = d2xf.fc[0]; t[1] = d2xf.fc[1]; t[2] = d2xf.fc[2]; }
D2X_AI void d2x_ReadLightMatrix(void *m) { d2x_st_mat(m, d2xf.light); int *t = (int *)((char *)m + 20); t[0] = d2xf.bk[0]; t[1] = d2xf.bk[1]; t[2] = d2xf.bk[2]; }
D2X_AI void d2x_ldfc(const void *p) { const int *t = (const int *)p; d2xf.fc[0] = t[0]; d2xf.fc[1] = t[1]; d2xf.fc[2] = t[2]; }
D2X_AI void d2x_stfc(void *p) { int *t = (int *)p; t[0] = d2xf.fc[0]; t[1] = d2xf.fc[1]; t[2] = d2xf.fc[2]; }
D2X_AI void d2x_setrgbc(unsigned int c) { d2xf.rgbc[0] = (unsigned char)c; d2xf.rgbc[1] = (unsigned char)(c >> 8); d2xf.rgbc[2] = (unsigned char)(c >> 16); d2xf.rgbc[3] = (unsigned char)(c >> 24); }

D2X_AI void d2x_ldv_n(int n, const void *p) { const short *s = (const short *)p; d2xf.v[n][0] = s[0]; d2xf.v[n][1] = s[1]; d2xf.v[n][2] = s[2]; }
D2X_AI void d2x_ldv0(const void *p) { d2x_ldv_n(0, p); }
D2X_AI void d2x_ldv1(const void *p) { d2x_ldv_n(1, p); }
D2X_AI void d2x_ldv2(const void *p) { d2x_ldv_n(2, p); }
D2X_AI void d2x_ldv3(const void *a, const void *b, const void *c) { d2x_ldv_n(0, a); d2x_ldv_n(1, b); d2x_ldv_n(2, c); }
D2X_AI void d2x_ldsv(const void *p) { const short *s = (const short *)p; d2xf.ir[1] = s[0]; d2xf.ir[2] = s[1]; d2xf.ir[3] = s[2]; }
D2X_AI void d2x_ldlvl(const void *p) { const int *t = (const int *)p; d2xf.ir[1] = (short)t[0]; d2xf.ir[2] = (short)t[1]; d2xf.ir[3] = (short)t[2]; }

/* ---------------------------------------------------------------- leitura de resultados */
D2X_AI void d2x_st_xy(void *p, int i) { short *s = (short *)p; s[0] = d2xf.sx[i]; s[1] = d2xf.sy[i]; }
D2X_AI void d2x_stsxy0(void *p) { d2x_st_xy(p, 0); }
D2X_AI void d2x_stsxy1(void *p) { d2x_st_xy(p, 1); }
D2X_AI void d2x_stsxy(void *p)  { d2x_st_xy(p, 2); }
D2X_AI void d2x_stsxy2(void *p) { d2x_st_xy(p, 2); }
D2X_AI void d2x_stsxy3(void *a, void *b, void *c) { d2x_st_xy(a, 0); d2x_st_xy(b, 1); d2x_st_xy(c, 2); }
D2X_AI void d2x_stsz(void *p)  { *(unsigned int *)p = d2xf.sz[3]; }
D2X_AI void d2x_stsz3(void *a, void *b, void *c) { *(unsigned int *)a = d2xf.sz[1]; *(unsigned int *)b = d2xf.sz[2]; *(unsigned int *)c = d2xf.sz[3]; }
D2X_AI void d2x_stszotz(void *p) { *(unsigned int *)p = (unsigned int)d2xf.sz[3] >> 2; }
D2X_AI void d2x_stotz(void *p) { *(unsigned int *)p = d2xf.otz; }
D2X_AI void d2x_stopz(void *p) { *(unsigned int *)p = (unsigned int)d2xf.mac[0]; }
D2X_AI void d2x_stsv(void *p)  { unsigned short *s = (unsigned short *)p; s[0] = (unsigned short)d2xf.ir[1]; s[1] = (unsigned short)d2xf.ir[2]; s[2] = (unsigned short)d2xf.ir[3]; }
D2X_AI void d2x_stlvl(void *p) { int *t = (int *)p; t[0] = d2xf.ir[1]; t[1] = d2xf.ir[2]; t[2] = d2xf.ir[3]; }
D2X_AI void d2x_stlvnl(void *p) { int *t = (int *)p; t[0] = d2xf.mac[1]; t[1] = d2xf.mac[2]; t[2] = d2xf.mac[3]; }
D2X_AI void d2x_st_rgb(void *p, int i) { unsigned char *b = (unsigned char *)p; b[0] = d2xf.rgb[i][0]; b[1] = d2xf.rgb[i][1]; b[2] = d2xf.rgb[i][2]; b[3] = d2xf.rgb[i][3]; }
D2X_AI void d2x_strgb(void *p) { d2x_st_rgb(p, 2); }
D2X_AI void d2x_strgb3(void *a, void *b, void *c) { d2x_st_rgb(a, 0); d2x_st_rgb(b, 1); d2x_st_rgb(c, 2); }

/* ---------------------------------------------------------------- projecao */
/* divisao H / SZ3 pelo metodo UNR do PS1, com o limite 0x1ffff do original */
D2X_AI unsigned int d2x_div(unsigned int num, unsigned int den)
{
	if (num >= den * 2) return 0x1ffff;
	// den <= 0xffff aqui (num < 2*den e o limite acima): normaliza para 0x8000..0xffff sem chamar o clz de software da libgcc
	unsigned int d = den;
	int sh = 0;
	if (d < 0x100) { sh = 8; d <<= 8; }
	if (d < 0x1000) { sh += 4; d <<= 4; }
	if (d < 0x4000) { sh += 2; d <<= 2; }
	if (d < 0x8000) { sh += 1; d <<= 1; }
	const int r1 = (int)(d & 0x7fff);
	const int r2 = d2_unr_table[(r1 + 0x40) >> 7] + 0x101;
	const int r3 = ((0x80 - (r2 * (r1 + 0x8000))) >> 8) & 0x1ffff;
	const unsigned int rec = (unsigned int)((r2 * r3) + 0x80) >> 8;
	const unsigned int q = (unsigned int)((((unsigned long long)rec * (num << sh)) + 0x8000) >> 16);
	return q > 0x1ffff ? 0x1ffff : q;
}
/* matriz, translacao, H e offset lidos uma vez para os 3 vertices */
typedef struct D2XCam { int r11, r12, r13, r21, r22, r23, r31, r32, r33; long long tx, ty, tz; int ofx, ofy; unsigned int h; } D2XCam;
D2X_AI void d2x_cam(D2XCam *c)
{
	c->r11 = d2xf.rot[0][0]; c->r12 = d2xf.rot[0][1]; c->r13 = d2xf.rot[0][2];
	c->r21 = d2xf.rot[1][0]; c->r22 = d2xf.rot[1][1]; c->r23 = d2xf.rot[1][2];
	c->r31 = d2xf.rot[2][0]; c->r32 = d2xf.rot[2][1]; c->r33 = d2xf.rot[2][2];
	c->tx = (long long)d2xf.tr[0] << 12; c->ty = (long long)d2xf.tr[1] << 12; c->tz = (long long)d2xf.tr[2] << 12;
	c->ofx = d2xf.ofx; c->ofy = d2xf.ofy; c->h = d2xf.h;
}
/* projeta um vertice; devolve sx/sy/sz/h e MAC1..3 */
D2X_AI void d2x_proj(const D2XCam *c, int vx, int vy, int vz, int *sx, int *sy, unsigned int *psz, unsigned int *ph, int *pa1, int *pa2, int *pa3)
{
	const int a1 = (int)((c->tx + (long long)(c->r11 * vx) + (long long)(c->r12 * vy) + (long long)(c->r13 * vz)) >> 12);
	const int a2 = (int)((c->ty + (long long)(c->r21 * vx) + (long long)(c->r22 * vy) + (long long)(c->r23 * vz)) >> 12);
	const int a3 = (int)((c->tz + (long long)(c->r31 * vx) + (long long)(c->r32 * vy) + (long long)(c->r33 * vz)) >> 12);
	*pa1 = a1; *pa2 = a2; *pa3 = a3;
	const unsigned int sz = (unsigned int)d2x_clamp(a3, 0, 0xffff); *psz = sz;
	const unsigned int h = d2x_div(c->h, sz); *ph = h;
	const int ir1 = d2x_clamp(a1, -0x8000, 0x7fff), ir2 = d2x_clamp(a2, -0x8000, 0x7fff);
	*sx = d2x_clamp((int)(((long long)c->ofx + (long long)ir1 * (int)h) >> 16), -0x400, 0x3ff);
	*sy = d2x_clamp((int)(((long long)c->ofy + (long long)ir2 * (int)h) >> 16), -0x400, 0x3ff);
}
D2X_AI void d2x_setmacir(int a1, int a2, int a3)
{
	d2xf.mac[1] = a1; d2xf.mac[2] = a2; d2xf.mac[3] = a3;
	d2xf.ir[1] = (short)d2x_clamp(a1, -0x8000, 0x7fff); d2xf.ir[2] = (short)d2x_clamp(a2, -0x8000, 0x7fff); d2xf.ir[3] = (short)d2x_clamp(a3, -0x8000, 0x7fff);
}
D2X_AI void d2x_depthcue(unsigned int h)
{
	const long long m0 = (long long)d2xf.dqb + (long long)d2xf.dqa * (int)h;
	d2xf.mac[0] = (int)m0;
	d2xf.ir[0] = (short)d2x_clamp((int)(m0 >> 12), 0, 0x1000);
}
D2X_AI void d2x_push_xy(int sx, int sy) { d2xf.sx[0] = d2xf.sx[1]; d2xf.sy[0] = d2xf.sy[1]; d2xf.sx[1] = d2xf.sx[2]; d2xf.sy[1] = d2xf.sy[2]; d2xf.sx[2] = (short)sx; d2xf.sy[2] = (short)sy; }
D2X_AI void d2x_push_sz(unsigned int sz) { d2xf.sz[0] = d2xf.sz[1]; d2xf.sz[1] = d2xf.sz[2]; d2xf.sz[2] = d2xf.sz[3]; d2xf.sz[3] = (unsigned short)sz; }

D2X_AI void d2x_rtps(void)
{
	D2XCam c; d2x_cam(&c);
	unsigned int sz, h; int sx, sy, a1, a2, a3;
	d2x_proj(&c, d2xf.v[0][0], d2xf.v[0][1], d2xf.v[0][2], &sx, &sy, &sz, &h, &a1, &a2, &a3);
	d2x_setmacir(a1, a2, a3);
	d2x_push_sz(sz); d2x_push_xy(sx, sy);
	d2x_depthcue(h);
}
D2X_AI void d2x_rtpt(void)
{
	D2XCam c; d2x_cam(&c);
	unsigned int sz0, sz1, sz2, h; int sx0, sy0, sx1, sy1, sx2, sy2, a1, a2, a3;
	d2x_proj(&c, d2xf.v[0][0], d2xf.v[0][1], d2xf.v[0][2], &sx0, &sy0, &sz0, &h, &a1, &a2, &a3);
	d2x_proj(&c, d2xf.v[1][0], d2xf.v[1][1], d2xf.v[1][2], &sx1, &sy1, &sz1, &h, &a1, &a2, &a3);
	d2x_proj(&c, d2xf.v[2][0], d2xf.v[2][1], d2xf.v[2][2], &sx2, &sy2, &sz2, &h, &a1, &a2, &a3);
	d2x_setmacir(a1, a2, a3);
	d2xf.sz[0] = d2xf.sz[3]; d2xf.sz[1] = (unsigned short)sz0; d2xf.sz[2] = (unsigned short)sz1; d2xf.sz[3] = (unsigned short)sz2;
	d2xf.sx[0] = (short)sx0; d2xf.sy[0] = (short)sy0; d2xf.sx[1] = (short)sx1; d2xf.sy[1] = (short)sy1; d2xf.sx[2] = (short)sx2; d2xf.sy[2] = (short)sy2;
	d2x_depthcue(h);
}
D2X_AI void d2x_nclip(void)
{
	const int x0 = d2xf.sx[0], y0 = d2xf.sy[0], x1 = d2xf.sx[1], y1 = d2xf.sy[1], x2 = d2xf.sx[2], y2 = d2xf.sy[2];
	d2xf.mac[0] = (x0 * y1) + (x1 * y2) + (x2 * y0) - (x0 * y2) - (x1 * y0) - (x2 * y1);
}
D2X_AI void d2x_avsz3(void)
{
	const long long m = (long long)d2xf.zsf3 * ((int)d2xf.sz[1] + (int)d2xf.sz[2] + (int)d2xf.sz[3]);
	d2xf.mac[0] = (int)m; d2xf.otz = (unsigned short)d2x_clamp((int)(m >> 12), 0, 0xffff);
}
D2X_AI void d2x_avsz4(void)
{
	const long long m = (long long)d2xf.zsf4 * ((int)d2xf.sz[0] + (int)d2xf.sz[1] + (int)d2xf.sz[2] + (int)d2xf.sz[3]);
	d2xf.mac[0] = (int)m; d2xf.otz = (unsigned short)d2x_clamp((int)(m >> 12), 0, 0xffff);
}

/* ---------------------------------------------------------------- matriz x vetor */
/* matriz (0 = rotacao, 1 = luz, 2 = cor) x vetor (0..2 = V0..V2, 3 = IR), com ou sem translacao */
D2X_AI void d2x_mvmva(int mx, int v, int tr, int sf, int lm)
{
	const short (*M)[3] = mx == 0 ? d2xf.rot : mx == 1 ? d2xf.light : d2xf.color;
	int vx, vy, vz;
	if (v == 3) { vx = d2xf.ir[1]; vy = d2xf.ir[2]; vz = d2xf.ir[3]; }
	else { vx = d2xf.v[v][0]; vy = d2xf.v[v][1]; vz = d2xf.v[v][2]; }
	const long long t1 = tr ? ((long long)d2xf.tr[0] << 12) : 0, t2 = tr ? ((long long)d2xf.tr[1] << 12) : 0, t3 = tr ? ((long long)d2xf.tr[2] << 12) : 0;
	const int a1 = d2x_sh(t1 + (long long)(M[0][0] * vx) + (M[0][1] * vy) + (M[0][2] * vz), sf);
	const int a2 = d2x_sh(t2 + (long long)(M[1][0] * vx) + (M[1][1] * vy) + (M[1][2] * vz), sf);
	const int a3 = d2x_sh(t3 + (long long)(M[2][0] * vx) + (M[2][1] * vy) + (M[2][2] * vz), sf);
	d2xf.mac[1] = a1; d2xf.mac[2] = a2; d2xf.mac[3] = a3;
	d2xf.ir[1] = (short)d2x_lm(a1, lm); d2xf.ir[2] = (short)d2x_lm(a2, lm); d2xf.ir[3] = (short)d2x_lm(a3, lm);
}
D2X_AI void d2x_rtv0(void)   { d2x_mvmva(0, 0, 0, 1, 0); }   /* rotacao x V0 */
D2X_AI void d2x_rtv0tr(void) { d2x_mvmva(0, 0, 1, 1, 0); }   /* rotacao x V0 + TR */
D2X_AI void d2x_rtir(void)   { d2x_mvmva(0, 3, 0, 1, 0); }   /* rotacao x IR */
D2X_AI void d2x_rtir_sf0(void) { d2x_mvmva(0, 3, 0, 0, 0); } /* rotacao x IR, sem deslocamento */
D2X_AI void d2x_llir(void)   { d2x_mvmva(1, 3, 0, 1, 0); }   /* luz x IR */
D2X_AI void d2x_lcir(void)   { d2x_mvmva(2, 3, 0, 1, 0); }   /* cor x IR */
D2X_AI void d2x_sqr0(void)
{
	const int a1 = (int)((long long)d2xf.ir[1] * d2xf.ir[1]), a2 = (int)((long long)d2xf.ir[2] * d2xf.ir[2]), a3 = (int)((long long)d2xf.ir[3] * d2xf.ir[3]);
	d2xf.mac[1] = a1; d2xf.mac[2] = a2; d2xf.mac[3] = a3;
	d2xf.ir[1] = (short)d2x_lm(a1, 1); d2xf.ir[2] = (short)d2x_lm(a2, 1); d2xf.ir[3] = (short)d2x_lm(a3, 1);
}
/* iluminacao com cor: n = 1 (NCCS, so V0) ou 3 (NCCT, V0..V2); sf=1, lm=1 */
D2X_AI void d2x_ncc(int n)
{
	for (int k = 0; k < n; k++)
	{
		const int vx = d2xf.v[k][0], vy = d2xf.v[k][1], vz = d2xf.v[k][2];
		int a1 = d2x_sh((long long)(d2xf.light[0][0] * vx) + (d2xf.light[0][1] * vy) + (d2xf.light[0][2] * vz), 1);
		int a2 = d2x_sh((long long)(d2xf.light[1][0] * vx) + (d2xf.light[1][1] * vy) + (d2xf.light[1][2] * vz), 1);
		int a3 = d2x_sh((long long)(d2xf.light[2][0] * vx) + (d2xf.light[2][1] * vy) + (d2xf.light[2][2] * vz), 1);
		int i1 = d2x_lm(a1, 1), i2 = d2x_lm(a2, 1), i3 = d2x_lm(a3, 1);
		a1 = d2x_sh(((long long)d2xf.bk[0] << 12) + (d2xf.color[0][0] * i1) + (d2xf.color[0][1] * i2) + (d2xf.color[0][2] * i3), 1);
		a2 = d2x_sh(((long long)d2xf.bk[1] << 12) + (d2xf.color[1][0] * i1) + (d2xf.color[1][1] * i2) + (d2xf.color[1][2] * i3), 1);
		a3 = d2x_sh(((long long)d2xf.bk[2] << 12) + (d2xf.color[2][0] * i1) + (d2xf.color[2][1] * i2) + (d2xf.color[2][2] * i3), 1);
		i1 = d2x_lm(a1, 1); i2 = d2x_lm(a2, 1); i3 = d2x_lm(a3, 1);
		a1 = d2x_sh((long long)(d2xf.rgbc[0] << 4) * i1, 1);
		a2 = d2x_sh((long long)(d2xf.rgbc[1] << 4) * i2, 1);
		a3 = d2x_sh((long long)(d2xf.rgbc[2] << 4) * i3, 1);
		d2xf.mac[1] = a1; d2xf.mac[2] = a2; d2xf.mac[3] = a3;
		d2xf.ir[1] = (short)d2x_lm(a1, 1); d2xf.ir[2] = (short)d2x_lm(a2, 1); d2xf.ir[3] = (short)d2x_lm(a3, 1);
		for (int j = 0; j < 4; j++) { d2xf.rgb[0][j] = d2xf.rgb[1][j]; d2xf.rgb[1][j] = d2xf.rgb[2][j]; }
		d2xf.rgb[2][3] = d2xf.rgbc[3];
		d2xf.rgb[2][0] = (unsigned char)d2x_lc(a1 >> 4); d2xf.rgb[2][1] = (unsigned char)d2x_lc(a2 >> 4); d2xf.rgb[2][2] = (unsigned char)d2x_lc(a3 >> 4);
	}
}
D2X_AI void d2x_nccs(void) { d2x_ncc(1); }
D2X_AI void d2x_ncct(void) { d2x_ncc(3); }
/* contagem de zeros a esquerda (usada pela raiz quadrada inteira) */
D2X_AI unsigned int d2x_leadingzerocount(unsigned int v) { return v ? (unsigned int)__builtin_clz(v) : 32u; }
/* transformacao simples e transformacao + perspectiva de um vertice (as flags de estouro nao sao calculadas: valem 0) */
D2X_AI void d2x_RotTrans(const void *v, void *o, void *flag) { d2x_ldv0(v); d2x_rtv0tr(); d2x_stlvnl(o); if (flag) *(unsigned int *)flag = 0; }
D2X_AI void d2x_RotTransPers(const void *v, void *sxy, void *p, void *flag, void *sz)
{
	d2x_ldv0(v); d2x_rtps(); d2x_stsxy(sxy);
	if (p) *(unsigned int *)p = (unsigned int)(int)d2xf.ir[0];
	if (flag) *(unsigned int *)flag = 0;
	d2x_stszotz(sz);
}
/* r3 = r1 * r2, coluna a coluna (cada coluna de r2 passa pela rotacao r1 e volta saturada em 16 bits); deixa r1 como matriz atual */
D2X_AI void d2x_MulMatrix0(const void *r1, const void *r2, void *r3)
{
	d2x_SetRotMatrix(r1);
	const short *b = (const short *)r2; short *o = (short *)r3;
	for (int c = 0; c < 3; c++)
	{
		d2xf.ir[1] = b[c]; d2xf.ir[2] = b[3 + c]; d2xf.ir[3] = b[6 + c];
		d2x_rtir();
		o[c] = d2xf.ir[1]; o[3 + c] = d2xf.ir[2]; o[6 + c] = d2xf.ir[3];
	}
}
#include "d2_xform.h"
#endif
