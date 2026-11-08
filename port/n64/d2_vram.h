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
static inline unsigned short d2_vswap(unsigned short w) { return (unsigned short)((w << 8) | (w >> 8)); }
static inline unsigned short d2_vram_rd(int x, int y)
{
    x &= 1023; y &= 511;
    return x < 512 ? d2_vswap(d2_vramL[(y << 9) + x]) : d2_vconv(d2_vramR[(y << 9) + x - 512]);
}
static inline void d2_vram_wr(int x, int y, unsigned short v)
{
    x &= 1023; y &= 511;
    if (x < 512) d2_vramL[(y << 9) + x] = d2_vswap(v); else d2_vramR[(y << 9) + x - 512] = d2_vconv(v);
}
#endif
