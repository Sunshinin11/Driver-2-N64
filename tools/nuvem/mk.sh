#!/usr/bin/env bash
# mk.sh <nome> [D2_EXTRA]  -> $D2_DIR/<nome>.z64
D2_DIR="${D2_DIR:-/opt/d2}"
D2_ROOT="${D2_ROOT:-$D2_DIR/D2_N64}"
export N64_INST="${N64_INST:-/opt/libdragon}"
export PATH="$N64_INST/bin:$PATH"

cd "$D2_ROOT"
touch port/compat/d2_gte_inline.h port/n64/psyx_rdp.cpp port/n64/psyx_backend.cpp port/n64/gte_native.cpp port/n64/d2_vtxcache.cpp port/redriver2/src_rebuild/PsyCross/src/gpu/PsyX_GPU.cpp port/redriver2/src_rebuild/PsyCross/src/psx/LIBGPU.C
make -f Makefile.d2 -j"$(nproc 2>/dev/null || echo 2)" all D2_EXTRA="${2:-}" > "$D2_DIR/mk_$1.log" 2>&1 || { grep -m5 error "$D2_DIR/mk_$1.log"; exit 1; }
cp d2_n64.z64 "$D2_DIR/$1.z64" && cp build_d2/d2_n64.map "$D2_DIR/$1.map" && cp build_d2/d2_n64.elf "$D2_DIR/$1.elf" && echo ok $1
