#!/usr/bin/env python3
"""Follow every screen exit (pipe/door) of every main level, two deep, in
hostrun; report destinations, deaths, and save a picture of each."""
import subprocess, re, sys, os, itertools, glob
from concurrent.futures import ThreadPoolExecutor
HR = '/home/claude/smwds/tools/hostrun/hostrun'
DAT = '/tmp/emu/sd/smw_assets.dat'
OUT = '/home/claude/ex/out'
PREFIX = """until 100 07
10 start
until 100 08
40
10 start
until 100 0a
40
10 a
until 100 14
900
10 b
until 100 0e
90
pin 1490 ff
"""
ROM = '/home/claude/ref/smw/smw.sfc'
def run(name, body, theirs=False):
    d = os.path.join(OUT, name + ('_T' if theirs else '')); os.makedirs(d, exist_ok=True)
    with open(d + '/s.txt', 'w') as f: f.write(PREFIX + body)
    env = dict(os.environ)
    if theirs: env['THEIRS'] = ROM
    env['WAV'] = d + '/a.wav'  # sound running (the original code waits on it)
    try:
        r = subprocess.run([HR, DAT, d, d + '/s.txt'], capture_output=True, text=True, timeout=120, env=env)
    except subprocess.TimeoutExpired:
        return 'TIMEOUT'
    finally:
        if os.path.exists(d + '/a.wav'): os.remove(d + '/a.wav')
    return r.stdout
def state(out):
    m = re.findall(r'\[100\]=(\w+) \[71\]=(\w+) \[94\]=(\w+) \[95\]=(\w+) \[96\]=(\w+) \[97\]=(\w+)', out)
    return m[-1] if m else None
def shotdiff(name):
    from PIL import Image, ImageChops
    a = sorted(glob.glob(os.path.join(OUT, name, 'shot*.ppm'))); b = sorted(glob.glob(os.path.join(OUT, name + '_T', 'shot*.ppm')))
    if not a or not b: return -1
    d = ImageChops.difference(Image.open(a[-1]).convert('RGB'), Image.open(b[-1]).convert('RGB'))
    data = d.get_flattened_data() if hasattr(d, 'get_flattened_data') else d.getdata()
    return 100.0 * sum(1 for p in data if max(p) > 16) / (256 * 224)
def info(out):
    ex = [(int(a), int(b, 16), int(c, 16)) for a, b, c in re.findall(r'exit screen (\d+) -> (\w+) props (\w+)', out)]
    ll = re.findall(r'lastlevel (\w+) mode=(\w+) anim=(\w+) vert=(\d) x=(\d+) y=(\d+)', out)
    return ex, ll
CASTLES = {0x004,0x007,0x00b,0x00e,0x013,0x01a,0x01b,0x01f,0x020,0x021,0x101,0x107,0x10d,0x10e,0x110,0x111,
           0x114,0x11c,0x11d,0x12e,0x12f,0x131,0x133,0x137,0x138,0x139,0x13a,0x13b}
def enter(level):
    t = level if level < 0x100 else level - 0xdc
    poke = f"poke d000 {t:02x} 2048\npoke 1f11 {0 if level < 0x100 else 1:02x} 1\n"
    s = poke + "10 b\nuntil 100 14\n30\n"
    if level in CASTLES:  # walk-in entrance: use the test hook (the map table gets rebuilt)
        s = f"level {level:03x}\n10 b\nuntil 100 14\n30\nuntil 100 0f\nuntil 100 14\n30\n"
    return s
def take(depth, screen, vert):
    return (f"poke 141a {depth:02x}\npoke {'97' if vert else '95'} {screen:02x}\npoke 100 0f\n"
            "until 100 14\n120\n")
def check(level):
    lines = []
    out = run(f'{level:03x}', enter(level) + "exits\nlastlevel\n")
    ex, ll = info(out)
    vert = int(ll[-1][3]) if ll else 0
    for s, dest, props in ex:
        body = enter(level) + take(1, s, vert) + "lastlevel\nexits\nram 100 71 94 95 96 97\nshot\n"
        o = run(f'{level:03x}_{s}', body)
        ot = o if level in CASTLES else run(f'{level:03x}_{s}', body, theirs=True)
        same = state(o) == state(ot)
        sd = shotdiff(f'{level:03x}_{s}')
        ex2, ll2 = info(o)
        d = ll2[-1] if ll2 else ('?',)*6
        lines.append(f"{level:03x} s{s} ->{dest:02x}/{props:02x} loaded {d[0]} mode {d[1]} anim {d[2]} pos {d[4]},{d[5]}  ref {'SAME' if same else 'DIFF ' + str(state(ot))} pic {sd:.1f}%")
        v2 = int(d[3]) if ll2 else 0
        for s2, dest2, props2 in ex2:
            b2 = body.replace("shot\n", "") + take(2, s2, v2) + "lastlevel\nram 100 71 94 95 96 97\nshot\n"
            o2 = run(f'{level:03x}_{s}_{s2}', b2)
            o2t = o2 if level in CASTLES else run(f'{level:03x}_{s}_{s2}', b2, theirs=True)
            same2 = state(o2) == state(o2t); sd2 = shotdiff(f'{level:03x}_{s}_{s2}')
            _, ll3 = info(o2)
            e = ll3[-1] if ll3 else ('?',)*6
            lines.append(f"   {d[0]} s{s2} ->{dest2:02x}/{props2:02x} loaded {e[0]} mode {e[1]} anim {e[2]} pos {e[4]},{e[5]}  ref {'SAME' if same2 else 'DIFF ' + str(state(o2t))} pic {sd2:.1f}%")
    return lines
levels = list(range(1, 0x25)) + list(range(0x101, 0x13c))
if __name__ != "__main__": levels = []
if __name__ == '__main__' and len(sys.argv) > 1: levels = [int(x, 16) for x in sys.argv[1:]]
if __name__ == '__main__':
  with ThreadPoolExecutor(2) as ex:
    for res in ex.map(check, levels):
      for l in res: print(l, flush=True)
