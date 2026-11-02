#!/usr/bin/env python3
"""Play each level the same way with the port's C code (MINE) and the
original game code (THEIRS, emulated 65816) and compare Mario's state and
the picture every 60 frames."""
import sys, os, re, glob
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0, '/home/claude/ex')
import exits as E
from PIL import Image, ImageChops
N = 20
def body(level):
    b = E.enter(level) + "pin 1490 ff\n"
    for i in range(N):
        b += "45 right+y\n15 right+y+b\nram 100 71 94 95 96 97 13 14\nshot\n"
    return b
def cmp(level):
    name = f'dp_{level:03x}'
    o = E.run(name, body(level)); t = E.run(name, body(level), theirs=True)
    sm = re.findall(r'frame \d+:(.*)', o); st = re.findall(r'frame \d+:(.*)', t)
    a = sorted(glob.glob(os.path.join(E.OUT, name, 'shot*.ppm'))); b = sorted(glob.glob(os.path.join(E.OUT, name + '_T', 'shot*.ppm')))
    first = None
    for i in range(min(len(sm), len(st))):
        pic = 0
        if i < len(a) and i < len(b):
            d = ImageChops.difference(Image.open(a[i]).convert('RGB'), Image.open(b[i]).convert('RGB'))
            data = d.get_flattened_data() if hasattr(d, 'get_flattened_data') else d.getdata()
            pic = 100.0 * sum(1 for p in data if max(p) > 16) / (256 * 224)
        if sm[i] != st[i] or pic > 0.5:
            first = (i, sm[i].strip(), st[i].strip(), pic); break
    # keep only the first differing pictures
    for f in a + b:
        if not first or os.path.basename(f) not in (os.path.basename(a[first[0]]) if first[0] < len(a) else '',):
            os.remove(f)
    return f"{level:03x} " + ("OK" if not first else f"DIFF at {first[0]}: mine {first[1]} | theirs {first[2]} | pic {first[3]:.1f}%")
levels = list(range(1, 0x25)) + list(range(0x101, 0x13c))
if len(sys.argv) > 1: levels = [int(x, 16) for x in sys.argv[1:]]
with ThreadPoolExecutor(2) as ex:
    for r in ex.map(cmp, levels): print(r, flush=True)
