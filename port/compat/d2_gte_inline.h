/* N64: MTC2/MFC2/CTC2 do GTE emulado como funcoes inline (o indice do registrador e constante nos macros gte_*; o switch some).
   Incluido de PsyCross/include/psx/inline_c.h. Semantica identica a PsyCross/src/psx/INLINE_C.C. */
#ifndef D2_GTE_INLINE_H
#define D2_GTE_INLINE_H
#include "psx/gtereg.h"
#ifdef __cplusplus
extern "C" unsigned int gte_leadingzerocount(unsigned int lzcs);
#else
extern unsigned int gte_leadingzerocount(unsigned int lzcs);
#endif

#define D2_AI static inline __attribute__((always_inline))
D2_AI int d2_lim(int value, int max, int min, unsigned int flag) { if (value > max) { C2_FLAG |= flag; return max; } if (value < min) { C2_FLAG |= flag; return min; } return value; }

D2_AI unsigned int d2_mfc2(int reg)
{
	switch (reg) {
	case 1: case 3: case 5: case 8: case 9: case 10: case 11:
		gteRegs.CP2D.p[reg].d = (int)gteRegs.CP2D.p[reg].sw.l; break;
	case 7: case 16: case 17: case 18: case 19:
		gteRegs.CP2D.p[reg].d = (unsigned int)gteRegs.CP2D.p[reg].w.l; break;
	case 15:
		gteRegs.CP2D.p[reg].d = C2_SXY2; break;
	case 28: case 29:
		gteRegs.CP2D.p[reg].d = d2_lim(C2_IR1 >> 7, 0x1f, 0, 0) | (d2_lim(C2_IR2 >> 7, 0x1f, 0, 0) << 5) | (d2_lim(C2_IR3 >> 7, 0x1f, 0, 0) << 10); break;
	}
	return gteRegs.CP2D.p[reg].d;
}
D2_AI int d2_mfc2_s(int reg) { d2_mfc2(reg); return gteRegs.CP2D.p[reg].sd; }

D2_AI void d2_mtc2(unsigned int value, int reg)
{
	switch (reg) {
	case 15: C2_SXY0 = C2_SXY1; C2_SXY1 = C2_SXY2; C2_SXY2 = value; break;
	case 28: C2_IR1 = (value & 0x1f) << 7; C2_IR2 = (value & 0x3e0) << 2; C2_IR3 = (value & 0x7c00) >> 3; break;
	case 30: C2_LZCR = gte_leadingzerocount(value); break;
	case 31: return;
	}
	gteRegs.CP2D.p[reg].d = value;
}
D2_AI void d2_mtc2_s(int value, int reg) { d2_mtc2((unsigned int)value, reg); }

D2_AI void d2_ctc2(unsigned int value, int reg)
{
	switch (reg) {
	case 4: case 12: case 20: case 26: case 27: case 29: case 30:
		value = (int)(short)value; break;
	case 31:
		value = value & 0x7ffff000;
		if ((value & 0x7f87e000) != 0) value |= 0x80000000;
		break;
	}
	gteRegs.CP2C.p[reg].d = value;
}

#define MTC2(v, r)   d2_mtc2((unsigned int)(v), (r))
#define MTC2_S(v, r) d2_mtc2_s((int)(v), (r))
#define MFC2(r)      d2_mfc2(r)
#define MFC2_S(r)    d2_mfc2_s(r)
#define CTC2(v, r)   d2_ctc2((unsigned int)(v), (r))

/* ===================================================================================================================
   GTE NATIVO (D2_GTE_NATIVE): as operacoes mais usadas pelo jogo (RTPS, RTPT, NCLIP, AVSZ3/4, MVMVA de rotacao) sao feitas
   aqui, inline e sem o emulador (GTE_operator). Mesma aritmetica inteira do PsyX (mesmo resultado nos registradores que o jogo
   le: SXY, SZ, OTZ, MAC, IR), sem calcular as flags de estouro (o Driver 2 nunca le o FLAG). Outras operacoes vao para o emulador.
   =================================================================================================================== */
#ifdef D2_GTE_NATIVE
#ifdef __cplusplus
extern "C" const unsigned char d2_unr_table[257];
#else
extern const unsigned char d2_unr_table[257];
#endif
D2_AI int d2n_clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
/* divisao do GTE (H / SZ3) pelo metodo UNR do PS1, com o limite 0x1ffff do original */
D2_AI unsigned int d2n_div(unsigned int num, unsigned int den)
{
	if (num >= den * 2) return 0x1ffff;
	const int lz = __builtin_clz(den) - 16;
	const int sh = lz > 0 ? lz : 0;
	const unsigned int d = den << sh;
	const int r1 = (int)(d & 0x7fff);
	const int r2 = d2_unr_table[(r1 + 0x40) >> 7] + 0x101;
	const int r3 = ((0x80 - (r2 * (r1 + 0x8000))) >> 8) & 0x1ffff;
	const unsigned int rec = (unsigned int)((r2 * r3) + 0x80) >> 8;
	const unsigned int q = (unsigned int)((((unsigned long long)rec * (num << sh)) + 0x8000) >> 16);
	return q > 0x1ffff ? 0x1ffff : q;
}
/* projeta o vertice idx (0..2) - RTPS/RTPT com sf=1, lm=0; devolve h/sz3 */
D2_AI unsigned int d2n_rtp(int idx)
{
	const int vx = gteRegs.CP2D.p[idx << 1].sw.l, vy = gteRegs.CP2D.p[idx << 1].sw.h, vz = gteRegs.CP2D.p[(idx << 1) + 1].sw.l;
	const int a1 = (int)((((long long)C2_TRX << 12) + (long long)(C2_R11 * vx) + (long long)(C2_R12 * vy) + (long long)(C2_R13 * vz)) >> 12);
	const int a2 = (int)((((long long)C2_TRY << 12) + (long long)(C2_R21 * vx) + (long long)(C2_R22 * vy) + (long long)(C2_R23 * vz)) >> 12);
	const int a3 = (int)((((long long)C2_TRZ << 12) + (long long)(C2_R31 * vx) + (long long)(C2_R32 * vy) + (long long)(C2_R33 * vz)) >> 12);
	C2_MAC1 = a1; C2_MAC2 = a2; C2_MAC3 = a3;
	const int ir1 = d2n_clamp(a1, -0x8000, 0x7fff), ir2 = d2n_clamp(a2, -0x8000, 0x7fff);
	C2_IR1 = ir1; C2_IR2 = ir2; C2_IR3 = d2n_clamp(a3, -0x8000, 0x7fff);
	const unsigned int sz = (unsigned int)d2n_clamp(a3, 0, 0xffff);
	C2_SZ0 = C2_SZ1; C2_SZ1 = C2_SZ2; C2_SZ2 = C2_SZ3; C2_SZ3 = sz;
	const unsigned int h = d2n_div((unsigned short)C2_H, sz);
	const int sx = (int)(((long long)C2_OFX + (long long)ir1 * (int)h) >> 16), sy = (int)(((long long)C2_OFY + (long long)ir2 * (int)h) >> 16);
	C2_SXY0 = C2_SXY1; C2_SXY1 = C2_SXY2;
	C2_SX2 = (short)d2n_clamp(sx, -0x400, 0x3ff); C2_SY2 = (short)d2n_clamp(sy, -0x400, 0x3ff);
	return h;
}
D2_AI void d2n_depthcue(unsigned int h)
{
	const long long m0 = (long long)C2_DQB + (long long)C2_DQA * (int)h;
	C2_MAC0 = (int)m0;
	C2_IR0 = d2n_clamp((int)(m0 >> 12), 0, 0x1000);
}
/* MVMVA com matriz de rotacao (mx=0), sf=1; v: 0..2 = V0..V2, 3 = IR; cv: 0 = TR, 3 = nenhum */
D2_AI void d2n_mvmva_r(int v, int cv, int lm)
{
	int vx, vy, vz;
	if (v == 3) { vx = C2_IR1; vy = C2_IR2; vz = C2_IR3; }
	else { vx = gteRegs.CP2D.p[v << 1].sw.l; vy = gteRegs.CP2D.p[v << 1].sw.h; vz = gteRegs.CP2D.p[(v << 1) + 1].sw.l; }
	const long long t1 = cv == 0 ? ((long long)C2_TRX << 12) : 0, t2 = cv == 0 ? ((long long)C2_TRY << 12) : 0, t3 = cv == 0 ? ((long long)C2_TRZ << 12) : 0;
	const int a1 = (int)((t1 + (long long)(C2_R11 * vx) + (long long)(C2_R12 * vy) + (long long)(C2_R13 * vz)) >> 12);
	const int a2 = (int)((t2 + (long long)(C2_R21 * vx) + (long long)(C2_R22 * vy) + (long long)(C2_R23 * vz)) >> 12);
	const int a3 = (int)((t3 + (long long)(C2_R31 * vx) + (long long)(C2_R32 * vy) + (long long)(C2_R33 * vz)) >> 12);
	C2_MAC1 = a1; C2_MAC2 = a2; C2_MAC3 = a3;
	const int lo = lm ? 0 : -0x8000;
	C2_IR1 = d2n_clamp(a1, lo, 0x7fff); C2_IR2 = d2n_clamp(a2, lo, 0x7fff); C2_IR3 = d2n_clamp(a3, lo, 0x7fff);
}
// RTPT/RTPS enxutos: matriz, translacao, H e offset lidos UMA vez para os 3 vertices (as escritas nos registradores do GTE
// impediam o compilador de reaproveitar as leituras); MAC/IR so do ultimo vertice (os anteriores sao sobrescritos no PS1 tambem).
typedef struct D2nCam { int r11, r12, r13, r21, r22, r23, r31, r32, r33; long long tx, ty, tz; int ofx, ofy; unsigned int h; } D2nCam;
D2_AI void d2n_cam(D2nCam *c)
{
	c->r11 = C2_R11; c->r12 = C2_R12; c->r13 = C2_R13; c->r21 = C2_R21; c->r22 = C2_R22; c->r23 = C2_R23; c->r31 = C2_R31; c->r32 = C2_R32; c->r33 = C2_R33;
	c->tx = (long long)C2_TRX << 12; c->ty = (long long)C2_TRY << 12; c->tz = (long long)C2_TRZ << 12;
	c->ofx = C2_OFX; c->ofy = C2_OFY; c->h = (unsigned short)C2_H;
}
// projeta um vertice; devolve sxy empacotado (como SXY), sz e h; a1..a3 = MAC
D2_AI unsigned int d2n_proj(const D2nCam *c, int vx, int vy, int vz, unsigned int *psz, unsigned int *ph, int *pa1, int *pa2, int *pa3)
{
	const int a1 = (int)((c->tx + (long long)(c->r11 * vx) + (long long)(c->r12 * vy) + (long long)(c->r13 * vz)) >> 12);
	const int a2 = (int)((c->ty + (long long)(c->r21 * vx) + (long long)(c->r22 * vy) + (long long)(c->r23 * vz)) >> 12);
	const int a3 = (int)((c->tz + (long long)(c->r31 * vx) + (long long)(c->r32 * vy) + (long long)(c->r33 * vz)) >> 12);
	*pa1 = a1; *pa2 = a2; *pa3 = a3;
	const unsigned int sz = (unsigned int)d2n_clamp(a3, 0, 0xffff); *psz = sz;
	const unsigned int h = d2n_div(c->h, sz); *ph = h;
	const int ir1 = d2n_clamp(a1, -0x8000, 0x7fff), ir2 = d2n_clamp(a2, -0x8000, 0x7fff);
	const int sx = d2n_clamp((int)(((long long)c->ofx + (long long)ir1 * (int)h) >> 16), -0x400, 0x3ff);
	const int sy = d2n_clamp((int)(((long long)c->ofy + (long long)ir2 * (int)h) >> 16), -0x400, 0x3ff);
	return ((unsigned int)(unsigned short)sy << 16) | (unsigned short)sx;   /* mesmo valor numerico do registrador SXY */
}
D2_AI void d2n_setmacir(int a1, int a2, int a3)
{
	C2_MAC1 = a1; C2_MAC2 = a2; C2_MAC3 = a3;
	C2_IR1 = d2n_clamp(a1, -0x8000, 0x7fff); C2_IR2 = d2n_clamp(a2, -0x8000, 0x7fff); C2_IR3 = d2n_clamp(a3, -0x8000, 0x7fff);
}

#ifdef D2_PERFDBG
#include "d2_perfdbg.h"
#endif

#ifdef __cplusplus
extern "C" { int doCOP2(int op); void d2n_op_rtps(void); void d2n_op_rtpt(void); void d2n_op_mvmva(int op); }
#else
extern int doCOP2(int op); extern void d2n_op_rtps(void); extern void d2n_op_rtpt(void); extern void d2n_op_mvmva(int op);
#endif
D2_AI int d2n_cop2(int op)
{
#ifdef D2_PERFDBG
	uint32_t t0 = 0;
	if (g_in_pedestrians) t0 = perf_cnt0();
#endif
	switch (op)
	{
	case 0x0180001:
		d2n_op_rtps();
#ifdef D2_PERFDBG
		if (g_in_pedestrians) { g_perf_cur.gte_peds_rtps_cnt++; g_perf_cur.gte_peds_rtps_ticks += perf_cnt0() - t0; }
#endif
		return 1;                                          /* RTPS  */
	case 0x0280030:
		d2n_op_rtpt();
#ifdef D2_PERFDBG
		if (g_in_pedestrians) { g_perf_cur.gte_peds_rtpt_cnt++; g_perf_cur.gte_peds_rtpt_ticks += perf_cnt0() - t0; }
#endif
		return 1;                   /* RTPT  */
	case 0x01400006:                                                                                            /* NCLIP */
		C2_FLAG = 0;
		C2_MAC0 = (C2_SX0 * C2_SY1) + (C2_SX1 * C2_SY2) + (C2_SX2 * C2_SY0) - (C2_SX0 * C2_SY2) - (C2_SX1 * C2_SY0) - (C2_SX2 * C2_SY1);
#ifdef D2_PERFDBG
		if (g_in_pedestrians) { g_perf_cur.gte_peds_nclip_cnt++; g_perf_cur.gte_peds_nclip_ticks += perf_cnt0() - t0; }
#endif
		return 1;
	case 0x0158002D:                                                                                            /* AVSZ3 */
	{
		C2_FLAG = 0;
		const long long m = (long long)C2_ZSF3 * ((int)C2_SZ1 + (int)C2_SZ2 + (int)C2_SZ3);
		C2_MAC0 = (int)m; C2_OTZ = d2n_clamp((int)(m >> 12), 0, 0xffff);
#ifdef D2_PERFDBG
		if (g_in_pedestrians) { g_perf_cur.gte_peds_avsz3_cnt++; g_perf_cur.gte_peds_avsz3_ticks += perf_cnt0() - t0; }
#endif
		return 1;
	}
	case 0x0168002E:                                                                                            /* AVSZ4 */
	{
		C2_FLAG = 0;
		const long long m = (long long)C2_ZSF4 * ((int)C2_SZ0 + (int)C2_SZ1 + (int)C2_SZ2 + (int)C2_SZ3);
		C2_MAC0 = (int)m; C2_OTZ = d2n_clamp((int)(m >> 12), 0, 0xffff);
#ifdef D2_PERFDBG
		if (g_in_pedestrians) { g_perf_cur.gte_peds_avsz4_cnt++; g_perf_cur.gte_peds_avsz4_ticks += perf_cnt0() - t0; }
#endif
		return 1;
	}
	default:
		/* MVMVA (funct 0x12) com sf=1, matriz de rotacao, traducao TR ou nenhuma */
		if ((op & 0x3f) == 0x12 && (op & (1 << 19)) && ((op >> 17) & 3) == 0 && (((op >> 13) & 3) == 0 || ((op >> 13) & 3) == 3))
		{
			d2n_op_mvmva(op);
#ifdef D2_PERFDBG
			if (g_in_pedestrians) { g_perf_cur.gte_peds_mvmva_cnt++; g_perf_cur.gte_peds_mvmva_ticks += perf_cnt0() - t0; }
#endif
			return 1;
		}
		{
			int res = (doCOP2)(op);
#ifdef D2_PERFDBG
			if (g_in_pedestrians) { g_perf_cur.gte_peds_emul_cnt++; g_perf_cur.gte_peds_emul_ticks += perf_cnt0() - t0; }
#endif
			return res;
		}
	}
}
#define doCOP2(op) d2n_cop2(op)
#endif /* D2_GTE_NATIVE */
#endif
