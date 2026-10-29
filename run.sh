#!/usr/bin/env bash
# build + roda no emulador sem janela: bash run.sh [quadros] [a_cada] [entradas]   -> imagens em $OUTDIR, log em /tmp/run.log
cd "$(dirname "$0")"
HARNESS="${HARNESS:-/opt/harness/harness}"
OUTDIR="${OUTDIR:-/opt/run1}"
mkdir -p "$OUTDIR"
bash build.sh > /tmp/build.log 2>&1 || true
grep -n "error" /tmp/build.log | head -20
[ -f d2_n64.z64 ] || exit 1
export VK_ICD_FILENAMES="${VK_ICD_FILENAMES:-/usr/share/vulkan/icd.d/lvp_icd.json}" D2_VULKAN="${D2_VULKAN:-1}"
rm -f "$OUTDIR"/*
"$HARNESS" d2_n64.z64 ${1:-900} "$OUTDIR" ${2:-0} ${3:-} > /tmp/run.log 2>&1
grep -v "unusual\|Cacheline\|^$\|REGS\|Stalled compile\|present\]" /tmp/run.log | head -${LINES_OUT:-30} | cut -c1-260
python3 -c "
from PIL import Image
Image.open('$OUTDIR/final.ppm').save('$OUTDIR/final.png')" 2>/dev/null
