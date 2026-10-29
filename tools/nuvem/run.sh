#!/usr/bin/env bash
# uso: run.sh rom quadros a_cada entradas outdir
HARNESS="${HARNESS:-/opt/harness/harness}"
export VK_ICD_FILENAMES="${VK_ICD_FILENAMES:-/usr/share/vulkan/icd.d/lvp_icd.json}" D2_VULKAN="${D2_VULKAN:-1}"
OUT=${5:-/opt/run1}; rm -rf "$OUT"; mkdir -p "$OUT"
"$HARNESS" "$1" "$2" "$OUT" "$3" "${4:-}" > "$OUT/run.log" 2>&1
python3 - "$OUT" <<'PY'
import sys,glob
from PIL import Image
o=sys.argv[1]; fs=sorted(glob.glob(o+'/frame_*.ppm'))[-24:]
if fs:
  ims=[Image.open(f).resize((320,120)) for f in fs]
  W=Image.new('RGB',(320*4,120*((len(ims)+3)//4)))
  for i,im in enumerate(ims): W.paste(im,((i%4)*320,(i//4)*120))
  W.save(o+'/sheet.png')
try: Image.open(o+'/final.ppm').save(o+'/final.png')
except: pass
PY
grep -E "^\[cyc\]|^\[fps\]|^\[stats\]|STUB|crash|excecao" "$OUT/run.log" | tail -${TAILN:-12} | cut -c1-260
