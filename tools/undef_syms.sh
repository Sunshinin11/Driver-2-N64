#!/bin/bash
# Lista os simbolos que o jogo (Game/C + Frontend) pede e que NAO sao definidos por ele mesmo:
# e a superficie de API que a camada N64 precisa fornecer. Roda no PC (g++ comum), sem toolchain MIPS.
# uso: bash tools/undef_syms.sh <pasta src_rebuild>
SRC="${1:?uso: undef_syms.sh <src_rebuild>}"; HERE="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${TMPDIR:-/tmp}/d2_objs"; rm -rf "$OUT"; mkdir -p "$OUT"
cd "$SRC"
fail=0
for f in $(find Game -name "*.c" | sort); do
  o="$OUT/$(echo $f | tr / _).o"
  g++ -x c++ -std=gnu++14 -fpermissive -w -c -DNTSC_VERSION -DBUILD_CONFIGURATION_STRING='"n64"' \
    -IGame -IPsyCross/include -IPsyCross/include/psx -I"$HERE/port/compat" "$f" -o "$o" 2>"$o.err" || { echo "FALHOU: $f"; head -3 "$o.err"; fail=1; }
done
cd "$OUT"
nm -C --defined-only *.o 2>/dev/null | awk '$2 ~ /[TDBRCV]/ {sub(/^[^ ]+ [A-Za-z] /,""); print}' | sed 's/(.*//' | sort -u > defined.txt
nm -C -u *.o 2>/dev/null | awk '{sub(/^ +U /,""); print}' | sed 's/(.*//' | sort -u > undefined_all.txt
comm -23 undefined_all.txt defined.txt > "$HERE/tools/undef_syms.txt"
echo "objetos: $(ls *.o | wc -l)  indefinidos (a fornecer): $(wc -l < "$HERE/tools/undef_syms.txt")  falhas: $fail"
