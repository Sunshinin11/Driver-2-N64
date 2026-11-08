// VRAM de textura do port N64. Sem a VRAM plana do PS1: a metade esquerda (x < 512: imagens de 16 bits e paginas de 8 bits) fica com os bytes trocados (ordem do N64) e a
// metade direita (x >= 512: paginas de textura de 4 bits e paletas) ja fica no formato do RDP (bytes e nibbles trocados), convertida
// uma unica vez na hora do upload. Nao existe copia "sombra".
#ifndef D2_VRAM_H
#define D2_VRAM_H
#ifdef __cplusplus
extern "C" {
#endif
extern unsigned short d2_vramL[512 * 512];      // x < 512, bytes de cada palavra trocados (ordem de memoria do N64; paginas de 8 bits ficam prontas para o RDP), passo 512 palavras
extern unsigned short d2_vramR[512 * 512];      // x >= 512, formato RDP, passo de linha 512 palavras (x - 512)
extern unsigned char d2_vcol[(1024 * 512) / 8]; // 1 bit por palavra: 1 = guarda cor ja no formato do RDP (RGBA5551), enviada por d2_LoadColor
extern unsigned char d2_vstp[(1024 * 512) / 8]; // 1 bit por palavra de cor: bit de transparencia (STP, bit 15) do PS1, que o RGBA5551 nao comporta
void d2_vram_r_barrier(void);                   // espera o RDP terminar de ler a metade direita antes de reescreve-la
#ifdef __cplusplus
}
#endif
// troca os bytes de cada palavra e os nibbles de cada byte (a conversao e sua propria inversa)
static inline unsigned short d2_vconv(unsigned short w)
{
    const unsigned int b = (unsigned short)((w << 8) | (w >> 8));
    return (unsigned short)(((b & 0x0F0Fu) << 4) | ((b >> 4) & 0x0F0Fu));
}
// cor do PS1 (xBBBBBGGGGGRRRRR, 0 = transparente) <-> cor do RDP (RRRRRGGGGGBBBBBA)
static inline unsigned short d2_ps1_to_rdp(unsigned short v)
{
    return (unsigned short)(((v & 31) << 11) | (((v >> 5) & 31) << 6) | (((v >> 10) & 31) << 1) | (v != 0));
}
static inline int d2_vbit(const unsigned char *m, int x, int y) { const int i = (y << 10) | x; return (m[i >> 3] >> (i & 7)) & 1; }
static inline void d2_vbit_set(unsigned char *m, int x, int y, int on)
{
    const int i = (y << 10) | x;
    if (on) m[i >> 3] |= (unsigned char)(1u << (i & 7)); else m[i >> 3] &= (unsigned char)~(1u << (i & 7));
}
static inline unsigned short d2_rdp_to_ps1(unsigned short n, int stp)
{
    return (unsigned short)(((n >> 11) & 31) | (((n >> 6) & 31) << 5) | (((n >> 1) & 31) << 10) | (stp ? 0x8000 : 0));
}
static inline unsigned short d2_vswap(unsigned short w) { return (unsigned short)((w << 8) | (w >> 8)); }
static inline unsigned short d2_vram_rd(int x, int y)
{
    x &= 1023; y &= 511;
    if (d2_vbit(d2_vcol, x, y)) return d2_rdp_to_ps1(x < 512 ? d2_vramL[(y << 9) + x] : d2_vramR[(y << 9) + x - 512], d2_vbit(d2_vstp, x, y));
    return x < 512 ? d2_vswap(d2_vramL[(y << 9) + x]) : d2_vconv(d2_vramR[(y << 9) + x - 512]);
}
// cor no formato do RDP (paletas e imagens de 16 bits): direto quando a celula veio de d2_LoadColor
static inline unsigned short d2_vram_rdn(int x, int y)
{
    x &= 1023; y &= 511;
    if (d2_vbit(d2_vcol, x, y)) return x < 512 ? d2_vramL[(y << 9) + x] : d2_vramR[(y << 9) + x - 512];
    return d2_ps1_to_rdp(d2_vram_rd(x, y));
}
static inline void d2_vram_wr(int x, int y, unsigned short v)
{
    x &= 1023; y &= 511;
    if (d2_vbit(d2_vcol, x, y)) { d2_vbit_set(d2_vcol, x, y, 0); d2_vbit_set(d2_vstp, x, y, 0); }
    if (x < 512) d2_vramL[(y << 9) + x] = d2_vswap(v); else d2_vramR[(y << 9) + x - 512] = d2_vconv(v);
}
#endif
