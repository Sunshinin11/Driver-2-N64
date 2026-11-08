// Estado de tela/desenho que o jogo le e escreve pela API de GPU do PS1 (DISPENV/DRAWENV). Sao so dados do jogo: o N64 nao tem GPU PS1.
#include "psx/libgte.h"
#include "psx/libgpu.h"

DISPENV currentDispEnv;
DISPENV activeDispEnv;
DRAWENV activeDrawEnv;
int g_GPUDisabledState = 0;
