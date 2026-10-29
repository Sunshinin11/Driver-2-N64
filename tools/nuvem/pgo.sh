#!/usr/bin/env bash
# pgo.sh nome "flags" [orcamento_bytes] : build -> profiler -> hot.ld desta versao -> rebuild -> $D2_DIR/<nome>p.z64
set -e
N=$1; F=$2; B=${3:-65536}
D2_DIR="${D2_DIR:-/opt/d2}"
D2_ROOT="${D2_ROOT:-$D2_DIR/D2_N64}"
MK_SH="${MK_SH:-$D2_DIR/mk.sh}"
RUN_SH="${RUN_SH:-$D2_DIR/run.sh}"
IN_BENCH="${IN_BENCH:-$D2_DIR/in_bench.txt}"
[ -f "$IN_BENCH" ] || IN_BENCH="$D2_ROOT/tools/nuvem/in_bench.txt"

cd "$D2_ROOT"
"$MK_SH" "$N" "$F" >/dev/null
D2_PROF=1 D2_PROF_FROM=2100 D2_CAP_FROM=99999 "$RUN_SH" "$D2_DIR/$N.z64" 2400 0 "$IN_BENCH" "$D2_DIR/prof_$N" >/dev/null
cp /tmp/prof.txt "$D2_DIR/prof_$N.raw"
python3 tools/hot.py "$D2_DIR/$N.elf" "$D2_DIR/prof_$N.raw" hot.ld "$B"
"$MK_SH" "${N}p" "$F" >/dev/null && echo ok "${N}p"
