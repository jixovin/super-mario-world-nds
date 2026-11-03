#!/usr/bin/env python3
"""Draws icon.bmp: the 32x32, 16-colour icon shown in DS menus (TWiLight
Menu++, the DS menu). Original art: a glossy dark tile with a bold "SMW"
logotype and a red "DS" badge. No Nintendo artwork."""
import sys
from PIL import Image

PAL = [
    (255, 0, 255),    # 0 transparent
    (8, 10, 24),      # 1 outline / shadow
    (52, 70, 150),    # 2 tile top
    (34, 48, 112),    # 3 tile middle
    (20, 28, 72),     # 4 tile bottom
    (255, 224, 64),   # 5 letter light
    (240, 160, 24),   # 6 letter dark
    (255, 248, 200),  # 7 letter highlight
    (224, 40, 40),    # 8 badge
    (150, 20, 28),    # 9 badge dark
    (248, 248, 248),  # 10 white
    (90, 110, 190),   # 11 tile edge highlight
]

SMW = {
    'S': [".#####", "######", "##....", "##....", "#####.",
          ".#####", "....##", "....##", "######", "#####."],
    'M': ["##....##", "###..###", "########", "##.##.##", "##.##.##",
          "##....##", "##....##", "##....##", "##....##", "##....##"],
    'W': ["##....##", "##....##", "##....##", "##....##", "##....##",
          "##.##.##", "##.##.##", "########", "###..###", "##....##"],
}
SMALL = {'D': ["##.", "#.#", "#.#", "#.#", "##."], 'S': [".##", "#..", ".#.", "..#", "##."]}

def main(out):
    W = H = 32
    px = [[0] * W for _ in range(H)]
    # Tile: three bands with dithered seams, rounded corners, outline.
    for y in range(H):
        for x in range(W):
            band = 2 if y < 11 else 3 if y < 22 else 4
            if y in (10, 21) and (x + y) % 2:
                band += 1
            px[y][x] = band
    for y in range(H):
        for x in range(W):
            cx, cy = min(x, W - 1 - x), min(y, H - 1 - y)
            if cx + cy < 3:
                px[y][x] = 0
            elif cx == 0 or cy == 0 or cx + cy == 3:
                px[y][x] = 1
            elif cy == 1 and y < 16 or (cx == 1 and y < 12):
                px[y][x] = 11
    # "SMW": yellow, lighter top rows, darker bottom rows, drop shadow.
    cells = []
    x0 = 4
    for ch in "SMW":
        rows = SMW[ch]
        for r, row in enumerate(rows):
            for c, b in enumerate(row):
                if b == '#':
                    cells.append((x0 + c, 5 + r, r))
        x0 += len(rows[0]) + 1
    for x, y, r in cells:
        px[y + 1][x + 1] = 1
    for x, y, r in cells:
        px[y][x] = 7 if r == 0 else 6 if r >= 7 else 5
    # Red "DS" badge.
    bx0, by0, bx1, by1 = 10, 19, 22, 27
    for y in range(by0, by1):
        for x in range(bx0, bx1):
            corner = (x in (bx0, bx1 - 1)) and (y in (by0, by1 - 1))
            if not corner:
                px[y][x] = 9 if y == by1 - 1 else 8
    for x in range(bx0 + 1, bx1):
        px[by1][x] = 1
    x = 12
    for ch in "DS":
        for r, row in enumerate(SMALL[ch]):
            for c, b in enumerate(row):
                if b == '#':
                    px[20 + r][x + c] = 10
        x += 5
    img = Image.new('P', (W, H))
    flat = [v for c in PAL for v in c]
    img.putpalette(flat + [0] * (768 - len(flat)))
    img.putdata([c for row in px for c in row])
    img.save(out)

if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'icon.bmp')
