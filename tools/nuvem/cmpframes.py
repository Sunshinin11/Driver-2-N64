#!/usr/bin/env python3
# cmpframes.py <pasta_base> <pasta_nova> : diferenca media absoluta por quadro (0 = identico)
import sys, glob, os
from PIL import Image, ImageChops
a, b = sys.argv[1], sys.argv[2]
for fa in sorted(glob.glob(os.path.join(a, 'frame_*.ppm'))):
    fb = os.path.join(b, os.path.basename(fa))
    if not os.path.exists(fb): print(os.path.basename(fa), 'ausente'); continue
    d = ImageChops.difference(Image.open(fa).convert('RGB'), Image.open(fb).convert('RGB'))
    h = d.histogram(); n = sum(h)
    mean = sum(i % 256 * c for i, c in enumerate(h)) / n
    bbox = d.getbbox()
    print(os.path.basename(fa), 'media=%.4f' % mean, 'bbox=%s' % (bbox,))
