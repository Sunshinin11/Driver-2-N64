// VRAM de textura do port N64. Sem a VRAM plana do PS1: a metade esquerda (x < 512: imagens de 16 bits e paginas de 8 bits) fica com os bytes trocados (ordem do N64) e a
// metade direita (x >= 512: paginas de textura de 4 bits e paletas) ja fica no formato do RDP (bytes e nibbles trocados), convertida
// uma unica vez na hora do upload. Nao existe copia "sombra".
#ifndef D2_VRAM_H
#define D2_VRAM_H
#ifdef __cplusplus
extern "C" {
#endif
extern unsigned short d2_vramL[512 * 512];      // x < 512, bytes de cada palavra trocados (ordem de memoria do N64; paginas de 8 bits ficam prontas para o RDP), passo 512 palavras
extern unsigned short d2_vramLc[512 * 512];     // copia da metade esquerda no formato do RDP (so para paginas de 4 bits lidas por DMA)
extern unsigned short d2_vramR[512 * 512];      // x >= 512, formato RDP, passo de linha 512 palavras (x - 512)
// Paletas (CLUT): o jogo so enxerga um handle opaco (slot + 1). Cada celula de 16 palavras de paleta vira um slot do pool (palavras do PS1, bit STP incluso);
// a VRAM plana nao guarda mais paletas. Escritas e leituras por coordenada numa celula ja internada vao para o pool.
#define D2_CLUT_MAX 1024
extern unsigned short d2_clut_map[512 * 64];          // celula (y * 64 + x / 16) -> slot + 1 (0 = nao e paleta)
extern unsigned short d2_clut_pool[D2_CLUT_MAX][16];  // palavras do PS1 de cada slot
extern unsigned short d2_clut_cell[D2_CLUT_MAX];      // slot -> celula
extern int d2_clut_n, d2_clut_bad;
unsigned short d2_clut_h(int x, int y);               // handle da paleta que comeca em (x, y); interna a celula se preciso
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
static inline unsigned short d2_vswap(unsigned short w) { return (unsigned short)((w << 8) | (w >> 8)); }
// handle -> coordenada da primeira palavra da paleta
static inline void d2_clut_xy(unsigned int h, int *x, int *y)
{
    if (h == 0 || h > (unsigned int)d2_clut_n) { d2_clut_bad++; *x = 0; *y = 0; return; }
    const unsigned int c = d2_clut_cell[h - 1];
    *x = (int)(c & 63) * 16; *y = (int)(c >> 6);
}
static inline unsigned short d2_vram_rd(int x, int y)
{
    x &= 1023; y &= 511;
    const unsigned int s = d2_clut_map[(y << 6) | (x >> 4)];
    if (s) return d2_clut_pool[s - 1][x & 15];
    return x < 512 ? d2_vswap(d2_vramL[(y << 9) + x]) : d2_vconv(d2_vramR[(y << 9) + x - 512]);
}
// cor no formato do RDP (paletas e imagens de 16 bits)
static inline unsigned short d2_vram_rdn(int x, int y) { return d2_ps1_to_rdp(d2_vram_rd(x, y)); }
static inline void d2_vram_wr(int x, int y, unsigned short v)
{
    x &= 1023; y &= 511;
    const unsigned int s = d2_clut_map[(y << 6) | (x >> 4)];
    if (s) { d2_clut_pool[s - 1][x & 15] = v; return; }
    if (x < 512) { d2_vramL[(y << 9) + x] = d2_vswap(v); d2_vramLc[(y << 9) + x] = d2_vconv(v); } else d2_vramR[(y << 9) + x - 512] = d2_vconv(v);
}
#endif
