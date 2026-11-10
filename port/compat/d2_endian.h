/* Os dados do PS1 (arquivos do CD) sao little-endian; o N64 e big-endian. Estes helpers convertem no ponto de uso. */
#ifndef D2_ENDIAN_H
#define D2_ENDIAN_H
#ifdef __cplusplus
extern "C" {
#endif
static inline unsigned short d2_bs16(unsigned short v) { return (unsigned short)((v >> 8) | (v << 8)); }
static inline unsigned int d2_bs32(unsigned int v) { return (v >> 24) | ((v >> 8) & 0xff00u) | ((v << 8) & 0xff0000u) | (v << 24); }
/* troca in-place um vetor de n shorts / n ints */
static inline void d2_swap16_array(void *p, int n) { unsigned short *s = (unsigned short *)p; while (n-- > 0) { *s = d2_bs16(*s); s++; } }
static inline void d2_swap32_array(void *p, int n) { unsigned int *s = (unsigned int *)p; while (n-- > 0) { *s = d2_bs32(*s); s++; } }
/* leitura/escrita de campos little-endian em memoria nao alinhada/crua */
static inline unsigned short d2_rd16(const void *p) { const unsigned char *b = (const unsigned char *)p; return (unsigned short)(b[0] | (b[1] << 8)); }
static inline unsigned int d2_rd32(const void *p) { const unsigned char *b = (const unsigned char *)p; return b[0] | (b[1] << 8) | (b[2] << 16) | ((unsigned)b[3] << 24); }
/* GTE/PS1: palavras "empacotadas" com a semantica little-endian */
static inline unsigned int d2_ld_ss(const void *p) { const unsigned short *s = (const unsigned short *)p; return (unsigned int)s[0] | ((unsigned int)s[1] << 16); }
static inline void d2_st_ss(void *p, unsigned int v) { unsigned short *s = (unsigned short *)p; s[0] = (unsigned short)v; s[1] = (unsigned short)(v >> 16); }
static inline void d2_st_b4(void *p, unsigned int v) { unsigned char *b = (unsigned char *)p; b[0] = (unsigned char)v; b[1] = (unsigned char)(v >> 8); b[2] = (unsigned char)(v >> 16); b[3] = (unsigned char)(v >> 24); }
/* primitiva: palavra u,v (bytes) + u_short (clut/tpage) */
static inline void d2_put_uvw(void *p, unsigned int v) { unsigned char *b = (unsigned char *)p; b[0] = (unsigned char)v; b[1] = (unsigned char)(v >> 8); *(unsigned short *)(b + 2) = (unsigned short)(v >> 16); }
/* modelos (MDS/modelos de carro): troca os campos curtos/inteiros no lugar */
void d2_swap_model(void *model, int size);
void d2_swap_car_models(char *lump, int lump_size);
#ifdef __cplusplus
}
#endif
/* cabecalho de cutscene (.R): int maxsize + 15 x {u16 offset, u16 size} */
static inline void d2_swap_cuthdr(void *h) { d2_swap32_array(h, 1); d2_swap16_array((char *)h + 4, 30); }
#endif
