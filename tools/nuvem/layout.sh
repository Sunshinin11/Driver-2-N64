#!/usr/bin/env bash
# layout.sh nome "flags" perfil.raw elf_do_perfil : gera hot.ld em varios orcamentos, relinka, mede e escolhe o melhor
N=$1; F=$2; P=$3; E=$4
D2_DIR="${D2_DIR:-/opt/d2}"
D2_ROOT="${D2_ROOT:-$D2_DIR/D2_N64}"
export N64_INST="${N64_INST:-/opt/libdragon}"
export PATH="$N64_INST/bin:$PATH"

cd "$D2_ROOT"
L=""
for b in 12 16 24 32 48 64 96; do
  python3 tools/hot.py "$E" "$P" "$D2_DIR/hot_${N}_$b.ld" $((b*1024)) >/dev/null
  cp "$D2_DIR/hot_${N}_$b.ld" hot.ld; rm -f build_d2/d2_n64.elf d2_n64.z64
  make -f Makefile.d2 -j"$(nproc 2>/dev/null || echo 2)" all D2_EXTRA="$F" > "$D2_DIR/mk_lay.log" 2>&1 || { echo falhou $b; continue; }
  cp d2_n64.z64 "$D2_DIR/${N}_$b.z64"; cp build_d2/d2_n64.elf "$D2_DIR/${N}_$b.elf"; L="$L ${N}_$b"
done
cd "$D2_DIR" && BF=2600 ./bench.sh $L >/dev/null; ./cc.sh $L
