#!/usr/bin/env python3
# compare.py OUTDIR : compare each DS top screen (ds*.ppm) with the reference
# picture saved right after it (ref*.ppm), cropped to the shown 192 lines.
import sys, glob, re
from PIL import Image, ImageChops
out = sys.argv[1]
for ref in sorted(glob.glob(out + '/ref*.ppm')):
    m = re.search(r'ref(\d+)_f(\d+)_c(\d+)', ref)
    shot, crop = int(m.group(1)), int(m.group(3))
    ds = glob.glob('%s/ds%02d_*.ppm' % (out, shot - 1)) or glob.glob('%s/ds%02d_*.ppm' % (out, shot))
    top = Image.open(ds[0]).convert('RGB').crop((0, 0, 256, 192))
    r = Image.open(ref).convert('RGB').crop((0, crop, 256, crop + 192))
    q = lambda im: im.point(lambda v: v >> 3)
    d = ImageChops.difference(q(top), q(r))
    if crop != 16 and crop <= 32:  # level: SNES lines above ~38 are the status bar
        from PIL import ImageDraw
        ImageDraw.Draw(d).rectangle((0, 0, 255, max(0, 38 - crop) - 1), fill=(0, 0, 0))
    data = d.get_flattened_data() if hasattr(d, 'get_flattened_data') else d.getdata()
    bad = sum(1 for p in data if max(p) > 1)
    pct = 100.0 * bad / (256 * 192)
    W = Image.new('RGB', (256 * 3, 192)); W.paste(top, (0, 0)); W.paste(r, (256, 0))
    W.paste(d.point(lambda v: 255 if v > 1 else 0), (512, 0))
    W.save(ref.replace('.ppm', '_cmp.png'))
    print('%s crop=%d mismatch=%.2f%%' % (ref.split('/')[-1], crop, pct))
