#!/usr/bin/env bash
# gameplay.sh [rom] [quadros] -> roda a ROM no ares (headless) ate a gameplay (menu -> Praticar) e monta /tmp/o/m.png com as capturas
set -e
D2_DIR="${D2_DIR:-/opt/d2}"; HARNESS="${HARNESS:-/opt/harness/harness}"
ROM="${1:-$D2_DIR/d2_n64.z64}"; FR="${2:-6000}"
python3 - <<'PY'
ev=[(1500,'D'),(1508,'-')]
for k in range(1,40): ev+=[(1560+k*150,'A'),(1568+k*150,'-')]
open('/tmp/in.txt','w').write('\n'.join('%d %s'%e for e in ev)+'\n')
PY
rm -rf /tmp/o; mkdir -p /tmp/o
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json D2_VULKAN=1 timeout 580 "$HARNESS" "$ROM" "$FR" /tmp/o 500 /tmp/in.txt > /tmp/run.log 2>&1 || true
grep -ciE "stub|assert|panic|native-ot|native-gpu" /tmp/run.log || true
python3 - <<'PY'
from PIL import Image
import glob
fs=sorted(glob.glob('/tmp/o/frame_*.ppm'))[2:]
c=Image.new('RGB',(960,((len(fs)+2)//3)*120))
for k,f in enumerate(fs): c.paste(Image.open(f).resize((320,120)),((k%3)*320,(k//3)*120))
c.save('/tmp/o/m.png')
PY
