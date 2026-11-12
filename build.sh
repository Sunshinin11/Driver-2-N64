#!/usr/bin/env bash
# bash build.sh        -> gera d2_n64.z64      bash build.sh clean -> limpa
set -e
cd "$(dirname "$0")"
export N64_INST="${N64_INST:-/opt/libdragon}"
export PATH="$N64_INST/bin:$PATH"
[ -d port/redriver2/src_rebuild/Game ] || bash ./setup_engine.sh
bash ./patch_engine.sh
if [ -d submodules/tiny3d ] && [ ! -f submodules/tiny3d/build/libt3d.a ]; then
    make -C submodules/tiny3d
fi
if [ "${1:-}" = "clean" ]; then exec make -f Makefile.d2 clean; fi
rm -f d2_n64.z64
exec make -f Makefile.d2 -j"$(nproc)" all "$@"
