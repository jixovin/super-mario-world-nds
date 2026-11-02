#!/usr/bin/env python3
"""From UBSan 'index N out of bounds for type T[M]' reports, list the tables
and how big they need to be; with --apply, extend them with the bytes that
follow them in the ROM (where the original code would have read)."""
import re, sys, glob
rom = open('/home/claude/ref/smw/smw.sfc', 'rb').read()
SRC = '/home/claude/smwds/src/'
need = {}
for line in open(sys.argv[1]):
    m = re.search(r'(smw_\w+\.c):(\d+):(\d+): runtime error: index (-?\d+) out of bounds for type \'(u?int(?:8|16)) \[(\d+)\]\'', line)
    if not m: continue
    f, ln, col, idx, typ, size = m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4)), m.group(5), int(m.group(6))
    text = open(SRC + f).read().split('\n')[ln - 1]
    names = re.findall(r'(k\w+)\[', text[:col + 40])
    # the array being indexed at col: last k-name starting before col
    cand = [(mm.start(), mm.group(1)) for mm in re.finditer(r'(k\w+)\[', text) if mm.start() < col]
    if not cand: print('?', f, ln, text.strip()); continue
    name = cand[-1][1]
    key = (name, typ)
    need[key] = max(need.get(key, 0), idx + 1)
for (name, typ), n in sorted(need.items()):
    print(name, typ, 'needs', n)
