#!/usr/bin/env python3
"""Make an SMW DS save (saves/smw-a.srm) with the whole game unlocked.

Three files, every path open, all switch palaces pressed. Funky is left
unbeaten so enemies keep their normal look (beating it changes them).
  File A: Valley of Bowser, on the Front Door (the final castle).
  File B: Vanilla Dome, on Lemmy's Castle.
  File C: Yoshi's Island, on Iggy's Castle.
Castles and fortresses are left unbeaten (and not destroyed on the map) so
each can be played with its boss and cutscene; beaten castles can't be
entered again in SMW.
"""
import struct, sys

SRAM_SIZE = 0x800
FILES = [  # (submap, grid x, grid y)
    (4, 23, 3),
    (2, 12, 18),
    (1, 9, 2),
]

# Castle/fortress translevel -> its destruction event (normal exit).
CASTLES = {0x07: 0x0F, 0x0B: 0x21, 0x0E: 0x24, 0x1A: 0x4D, 0x1B: 0x4A, 0x1F: 0x35,
           0x20: 0x61, 0x25: 0x06, 0x34: 0x40, 0x35: 0x41, 0x36: 0x43, 0x40: 0x1C}

def save_buffer(submap, gx, gy):
    b = bytearray(0x8D)
    for t in range(1, 0x60):
        b[t] = 0x8F                      # beaten, can walk every way
    b[0x49] = 0x0F                       # Funky: not beaten (see above)
    for i in range(15):
        b[0x60 + i] = 0xFF               # every event (paths, bridges, ...)
    for tl, ev in CASTLES.items():
        b[tl] = 0x0F
        b[0x60 + (ev >> 3)] &= ~(0x80 >> (ev & 7))
    b[0x6F] = b[0x70] = submap           # $1F11/12 submap, both players
    px, py = gx * 16 + 8, gy * 16 + 8
    struct.pack_into('<4H', b, 0x75, px, py, px, py)   # $1F17 positions
    struct.pack_into('<4H', b, 0x7D, gx, gy, gx, gy)   # $1F1F grid positions
    for i in range(4):
        b[0x85 + i] = 1                  # $1F27 switch palaces
    b[0x8C] = min(96, sum(bin(x).count('1') for x in b[0x60:0x6F]))  # exits shown
    return b

def main(out):
    sram = bytearray(SRAM_SIZE)
    for f, spec in enumerate(FILES):
        buf = save_buffer(*spec)
        for base in ([0x000, 0x08F, 0x11E][f], [0x000, 0x08F, 0x11E][f] + 0x1AD):
            sram[base:base + 0x8D] = buf
            struct.pack_into('<H', sram, base + 0x8D, (0x5A5A - sum(buf)) & 0xFFFF)
    h = 2166136261
    for c in sram:
        h = ((h ^ c) * 16777619) & 0xFFFFFFFF
    with open(out, 'wb') as fp:
        fp.write(sram + struct.pack('<4I', 0x53444D53, 1000000, SRAM_SIZE, h))

main(sys.argv[1] if len(sys.argv) > 1 else 'smw-a.srm')
