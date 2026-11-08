#!/usr/bin/env bash
# Ajustes no motor copiado (port/redriver2). Idempotente: roda a cada build (build.sh) e no setup_engine.sh.
cd "$(dirname "$0")"
D="port/redriver2/src_rebuild"
[ -d "$D/Game" ] || exit 0
# x86 "int3" nao existe no MIPS
sed -i 's|#define trap(ode) {__asm__("int3");}|#define trap(ode) {printError("EXCEPTION code: %x\\n", ode); __builtin_trap();}|' "$D/Game/driver2.h"
# PsyX_render.h so conhece renderers OpenGL: aceitar o renderer N64
sed -i 's|^#if defined(RENDERER_OGLES) \|\| defined(RENDERER_OGL)$|#if defined(RENDERER_OGLES) \|\| defined(RENDERER_OGL) \|\| defined(D2_N64)|' "$D/PsyCross/include/PsyX/PsyX_render.h"
# Memoria (N64 tem 8 MB): buffers de vertices/lotes do PsyX (PC usa 65536 vertices), e o "malloc" de nivel (PC usa 2 MB; PS1 usava ~0,8 MB)
sed -i 's|^#define MAX_VERTEX_BUFFER_SIZE.*$|#define MAX_VERTEX_BUFFER_SIZE D2_MAX_VERTS|' "$D/PsyCross/include/PsyX/PsyX_render.h"
sed -i 's|^char g_allocatedMem\[0x200000\];|char g_allocatedMem[0x140000];|' "$D/Game/C/system.c"

# GTE: cabecalhos de macros/registradores substituidos pela transformacao nativa (port/compat/d2_xf.h)
for _h in inline_c gtemac gtereg; do
  printf '#pragma once\n#include "d2_xf.h"\n' > "$D/PsyCross/include/psx/$_h.h"
done

# patches de fonte (endianness etc.)
python3 tools/patch_engine.py
