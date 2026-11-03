#!/usr/bin/env python3
"""Draws icon.bmp: the 32x32, 16-colour icon shown in DS menus (TWiLight
Menu++, the DS Lite menu). Original pixel art: "SMW" over a hill and sky."""
from PIL import Image

PAL = [
    (255, 0, 255),    # 0 transparent
    (16, 16, 40),     # 1 outline
    (104, 176, 248),  # 2 sky
    (160, 208, 248),  # 3 light sky
    (248, 248, 248),  # 4 white
    (40, 160, 56),    # 5 hill
    (96, 208, 80),    # 6 hill light
    (16, 88, 32),     # 7 hill dark
    (216, 152, 72),   # 8 dirt
    (152, 96, 40),    # 9 dirt dark
    (232, 48, 40),    # 10 red
    (248, 128, 96),   # 11 red light
    (248, 208, 32),   # 12 yellow
    (176, 120, 8),    # 13 yellow dark
    (136, 24, 24),    # 14 red dark
    (64, 120, 200),   # 15 sky dark
]

# 5x7 letters.
FONT = {
    'S': ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
    'M': ["10001", "11011", "10101", "10101", "10001", "10001", "10001"],
    'W': ["10001", "10001", "10001", "10101", "10101", "11011", "10001"],
    'D': ["11110", "10001", "10001", "10001", "10001", "10001", "11110"],
}

def main(out):
    W = H = 32
    px = [[0] * W for _ in range(H)]
    def put(x, y, c):
        if 0 <= x < W and 0 <= y < H: px[y][x] = c
    # Rounded tile: sky bands, transparent corners, outline.
    for y in range(H):
        for x in range(W):
            px[y][x] = 3 if y > 14 or (y == 14 and x % 2) else 2
    for y in range(H):
        for x in range(W):
            cx = min(x, W - 1 - x); cy = min(y, H - 1 - y)
            if cx + cy < 2: px[y][x] = 0
            elif cx == 0 or cy == 0 or cx + cy == 2: px[y][x] = 1
    # Cloud.
    for x, y in [(22,11),(23,11),(24,11),(21,12),(22,12),(23,12),(24,12),(25,12),(26,12),(20,13),(21,13),(22,13),(23,13),(24,13),(25,13),(26,13),(27,13)]:
        put(x, y, 4)
    for x in range(21, 27):
        put(x, 14, 2)
    # Hill and ground.
    for y in range(17, 26):
        for x in range(1, 31):
            dx = (x - 22) / 9.0; dy = (y - 26) / 9.0
            if dx * dx + dy * dy <= 1.0:
                inner = ((x - 22) / 8.0) ** 2 + ((y - 26) / 8.0) ** 2 <= 1.0
                put(x, y, (6 if (x < 20 and y < 22) else 5) if inner else 7)
    for x in range(1, 31):
        put(x, 25, 7)
        put(x, 26, 5)
        for y in range(27, 31):
            put(x, y, 9 if (x + y) % 4 == 0 else 8)
    # Letters with a 1px dark outline: "SMW" in red, "DS" in yellow.
    def text(s, x0, y0, fill, shade):
        cells = set()
        for i, ch in enumerate(s):
            for r, row in enumerate(FONT[ch]):
                for c, bit in enumerate(row):
                    if bit == '1': cells.add((x0 + i * 6 + c, y0 + r))
        for (x, y) in cells:
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    if (x + dx, y + dy) not in cells: put(x + dx, y + dy, 1)
        for (x, y) in cells:
            put(x, y, shade if y == y0 + 6 else fill)
    text("SMW", 7, 4, 10, 14)
    text("DS", 4, 17, 12, 13)
    img = Image.new('P', (W, H))
    flat = []
    for c in PAL: flat += list(c)
    img.putpalette(flat + [0] * (768 - len(flat)))
    img.putdata([c for row in px for c in row])
    img.save(out)

if __name__ == '__main__':
    import sys
    main(sys.argv[1] if len(sys.argv) > 1 else 'icon.bmp')
