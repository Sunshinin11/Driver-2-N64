/* Incluido automaticamente (-include) em todos os arquivos do jogo no N64. Cobre o que o PC (Windows/Linux) fornecia. */
#ifndef D2_N64_COMPAT_H
#define D2_N64_COMPAT_H
#include <stdio.h>
#include "d2_endian.h"
#define HOME_ENV "HOME"          /* nao existe pasta de usuario no N64; saves irao para Controller Pak/SRAM (etapa futura) */
#define _mkdir(str) ((void)(str)) /* sem diretorios gravaveis no N64 */
#include "../n64/d2_origin.h"
#include "../n64/d2_perfdbg.h"
#ifdef __cplusplus
extern "C" {
#endif
FILE *d2_fopen(const char *path, const char *mode); /* port/n64/sys_n64.c: redireciona para rom:/ (DragonFS) */
void d2_audio_pump(void);
#ifdef __cplusplus
}
#endif
#define fopen(p, m) d2_fopen((p), (m))

/* Fase 1.1 (port/n64/d2_vtxcache.cpp): as versoes originais destas funcoes de desenho ficam com o sufixo _orig */
#if defined(D2_VTXCACHE) && defined(D2_GTE_NATIVE)
#define D2_VC_ORIG(f) f##_orig
#else
#define D2_VC_ORIG(f) f
#endif

/* buffer de vertices do PsyX (GrVertex, 20 bytes cada): com o backend nativo (d2_native_*),
   todas as primitivas (poligonos, retangulos, linhas, sprites) sao despachadas diretamente ao RDP.
   O buffer legado do PsyX e mantido em 64 vertices apenas por seguranca (economiza mais ~162 KB de RAM). */
#ifndef D2_MAX_VERTS
#ifdef D2_DIRECT
#define D2_MAX_VERTS 64
#else
#define D2_MAX_VERTS 24576
#endif
#endif
#endif
