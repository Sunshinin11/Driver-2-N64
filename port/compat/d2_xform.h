/* Matematica nativa de transformacao (substitui o uso do GTE nos calculos de fisica/colisao/camera).
 * Sem registradores virtuais: a matriz e o vetor entram por parametro e o resultado sai por parametro.
 * Semantica identica ao GTE (sf=1): resultado = (soma 64 bits) >> 12, aritmetico. */
#ifndef D2_XFORM_H
#define D2_XFORM_H

/* saida = (M * v) >> 12 + T   (equivale a: SetRotMatrix; SetTransMatrix; ldv0; rtv0tr; stlvnl) */
static inline void d2x_rtv_tr(const MATRIX *M, const SVECTOR *v, VECTOR *o)
{
	const long long vx = v->vx, vy = v->vy, vz = v->vz;
	o->vx = (int)((((long long)M->t[0] << 12) + M->m[0][0] * vx + M->m[0][1] * vy + M->m[0][2] * vz) >> 12);
	o->vy = (int)((((long long)M->t[1] << 12) + M->m[1][0] * vx + M->m[1][1] * vy + M->m[1][2] * vz) >> 12);
	o->vz = (int)((((long long)M->t[2] << 12) + M->m[2][0] * vx + M->m[2][1] * vy + M->m[2][2] * vz) >> 12);
}

/* idem, saida em array de 3 inteiros (LONGVECTOR/pointPos/basePos) */
static inline void d2x_rtv_tr3(const MATRIX *M, const SVECTOR *v, int *o)
{
	VECTOR r;
	d2x_rtv_tr(M, v, &r);
	o[0] = r.vx; o[1] = r.vy; o[2] = r.vz;
}

static inline short d2x_sat16(long long x) { return x > 32767 ? 32767 : (x < -32768 ? -32768 : (short)x); }

/* saida = sat16((M * v) >> 12), sem translacao   (equivale a: ldsv; rtir; stsv) */
static inline void d2x_rt_sv(const MATRIX *M, const SVECTOR *v, SVECTOR *o)
{
	const long long vx = v->vx, vy = v->vy, vz = v->vz;
	o->vx = d2x_sat16((M->m[0][0] * vx + M->m[0][1] * vy + M->m[0][2] * vz) >> 12);
	o->vy = d2x_sat16((M->m[1][0] * vx + M->m[1][1] * vy + M->m[1][2] * vz) >> 12);
	o->vz = d2x_sat16((M->m[2][0] * vx + M->m[2][1] * vy + M->m[2][2] * vz) >> 12);
}

#endif
