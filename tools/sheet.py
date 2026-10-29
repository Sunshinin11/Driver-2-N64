#!/usr/bin/env python3
"""contact sheet: python3 tools/sheet.py [dir] [cols] -> dir/sheet.png (recorta os 320x240 visiveis de cada quadro)"""
import sys, glob
from PIL import Image
d = sys.argv[1] if len(sys.argv) > 1 else '/opt/run1'
cols = int(sys.argv[2]) if len(sys.argv) > 2 else 4
fs = sorted(glob.glob(d + '/frame_*.ppm'))
W, H = 320, 240
rows = (len(fs) + cols - 1) // cols
sheet = Image.new('RGB', (cols * W, rows * H))
for i, f in enumerate(fs):
    im = Image.open(f).convert('RGB').resize((W, H))
    sheet.paste(im, ((i % cols) * W, (i // cols) * H))
sheet.save(d + '/sheet.png')
print(len(fs), 'quadros ->', d + '/sheet.png')
