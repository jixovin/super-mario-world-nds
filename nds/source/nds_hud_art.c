/* Themed pixel-art backgrounds for the bottom screen (see nds_hud_art.h).
 *
 * The screen is 256x192; the art grid is 128x96 cells of 2x2 pixels. The
 * panels cover the middle of the screen, so the scenery that matters sits
 * in the strip below them (cells 68-87) and the ground (cells 88-95), which
 * the level progress marker walks along. */
#include "nds_hud_art.h"

#include <stdbool.h>

enum { kW = 256, kCW = 128, kCH = 96, kGround = 88 };

#define C(r, g, b) ((uint16_t)(0x8000 | (r) | ((g) << 5) | ((b) << 10)))

static uint16_t *s_fb;

static inline void P(int x, int y, uint16_t c) {
  if ((unsigned)x >= kCW || (unsigned)y >= kCH) return;
  uint16_t *p = &s_fb[(y * 2) * kW + x * 2];
  p[0] = p[1] = p[kW] = p[kW + 1] = c;
}

static void Rect(int x, int y, int w, int h, uint16_t c) {
  for (int j = y; j < y + h; j++)
    for (int i = x; i < x + w; i++) P(i, j, c);
}

/* Checkerboard of two colours. */
static void Dither(int x, int y, int w, int h, uint16_t a, uint16_t b) {
  for (int j = y; j < y + h; j++)
    for (int i = x; i < x + w; i++) P(i, j, ((i + j) & 1) ? b : a);
}

/* Horizontal bands: colours[i] from rows[i] down; a dithered row pair
 * between neighbouring bands. */
static void Bands(const uint16_t *colours, const uint8_t *rows, int n, int bottom) {
  for (int i = 0; i < n; i++) {
    const int y0 = rows[i], y1 = i + 1 < n ? rows[i + 1] : bottom;
    Rect(0, y0, kCW, y1 - y0, colours[i]);
    if (i > 0) Dither(0, y0, kCW, 2, colours[i - 1], colours[i]);
  }
}

static uint32_t Hash(int x, int y, uint32_t seed) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}

static bool InEllipse(int dx, int dy, int rx, int ry) {
  return dx * dx * ry * ry + dy * dy * rx * rx <= rx * rx * ry * ry;
}

/* A mound: the top half of an ellipse standing on row `base`, with an
 * outline and a lighter patch on its upper left. */
static void Mound(int cx, int base, int rx, int ry, uint16_t fill, uint16_t light, uint16_t outline) {
  for (int y = base - ry; y < base; y++) {
    for (int x = cx - rx; x <= cx + rx; x++) {
      const int dx = x - cx, dy = y - base;
      if (!InEllipse(dx, dy, rx, ry)) continue;
      const bool edge = !InEllipse(dx - 1, dy, rx, ry) || !InEllipse(dx + 1, dy, rx, ry) ||
                        !InEllipse(dx, dy - 1, rx, ry);
      uint16_t c = fill;
      if (edge) c = outline;
      else if (light != fill && InEllipse(dx + rx / 3, dy + ry / 2, rx / 3, ry / 3)) c = light;
      P(x, y, c);
    }
  }
}

/* A full ellipse with a ring of `shore` inside the edge and an outline. */
static void Island(int cx, int cy, int rx, int ry, uint16_t land, uint16_t shore, uint16_t outline) {
  for (int y = cy - ry - 1; y <= cy + ry + 1; y++) {
    for (int x = cx - rx - 1; x <= cx + rx + 1; x++) {
      const int dx = x - cx, dy = y - cy;
      if (InEllipse(dx, dy, rx - 1, ry - 1)) P(x, y, land);
      else if (InEllipse(dx, dy, rx, ry)) P(x, y, shore);
      else if (InEllipse(dx, dy, rx + 1, ry + 1)) P(x, y, outline);
    }
  }
}

/* Character-art sprite: '.' = transparent, '1'-'4' = pal[0]-pal[3]. */
static void Sprite(int x, int y, const char *const *rows, int h, const uint16_t *pal) {
  for (int j = 0; j < h; j++)
    for (int i = 0; rows[j][i]; i++)
      if (rows[j][i] >= '1' && rows[j][i] <= '4') P(x + i, y + j, pal[rows[j][i] - '1']);
}

static const char *const kCloud[] = {
    ".....1111...........",
    "...11222211..1111...",
    "..1222222221122221..",
    ".122222222222222221.",
    "12222222222222222221",
    "12223322222222333221",
    ".1133331111111333311",
    "...11111.....11111..",
};

static void Cloud(int x, int y, uint16_t outline) {
  const uint16_t pal[3] = {outline, C(31, 31, 31), C(24, 28, 31)};
  Sprite(x, y, kCloud, 8, pal);
}

static const char *const kBubble[] = {".11.", "1.21", "1..1", ".11."};

static const char *const kFlame[] = {
    "..1..", ".121.", ".121.", "12321", "12321", ".121.", "44444", ".444.",
};

/* Brick wall: bricks w x h (cells) with mortar lines and a light top edge. */
static void Bricks(int y0, int y1, int bw, int bh, uint16_t brick, uint16_t light, uint16_t mortar) {
  for (int y = y0; y < y1; y++) {
    const int row = (y - y0) / bh, ry = (y - y0) % bh;
    for (int x = 0; x < kCW; x++) {
      const int rx = (x + (row & 1) * (bw / 2)) % bw;
      P(x, y, ry == bh - 1 || rx == bw - 1 ? mortar : ry == 0 ? light : brick);
    }
  }
}

/* Ground with a top outline, a surface band and a striped lower band. */
static void Ground(uint16_t outline, uint16_t top, uint16_t top_dark, uint16_t dirt, uint16_t stripe) {
  Rect(0, kGround, kCW, 1, outline);
  Rect(0, kGround + 1, kCW, 2, top);
  Rect(0, kGround + 3, kCW, 1, top_dark);
  for (int y = kGround + 4; y < kCH; y++)
    for (int x = 0; x < kCW; x++) P(x, y, (x + y) % 6 == 0 ? stripe : dirt);
}

/* ---- scenes ------------------------------------------------------------ */

static void Grass(void) {
  static const uint16_t sky[] = {C(13, 22, 31), C(17, 25, 31), C(21, 28, 31)};
  static const uint8_t rows[] = {0, 30, 62};
  Bands(sky, rows, 3, kGround);
  Cloud(6, 69, C(18, 23, 31));
  Cloud(98, 72, C(18, 23, 31));
  Cloud(52, 4, C(14, 22, 31));
  const uint16_t out = C(1, 8, 2);
  Mound(20, kGround, 15, 17, C(9, 24, 8), C(16, 29, 12), out);
  Mound(110, kGround, 12, 13, C(9, 24, 8), C(16, 29, 12), out);
  Mound(64, kGround, 9, 8, C(4, 18, 5), C(9, 23, 8), out);
  /* Bushes. */
  Mound(40, kGround, 5, 4, C(6, 21, 6), C(12, 26, 9), out);
  Mound(46, kGround, 4, 3, C(6, 21, 6), C(12, 26, 9), out);
  Mound(88, kGround, 4, 3, C(6, 21, 6), C(12, 26, 9), out);
  Ground(C(2, 9, 2), C(8, 25, 6), C(4, 16, 3), C(28, 19, 9), C(22, 13, 5));
}

static void Sky(void) {
  static const uint16_t sky[] = {C(10, 19, 31), C(14, 23, 31), C(19, 27, 31)};
  static const uint8_t rows[] = {0, 34, 66};
  Bands(sky, rows, 3, kCH);
  Cloud(2, 1, C(16, 22, 31));
  Cloud(96, 3, C(16, 22, 31));
  Cloud(10, 70, C(17, 24, 31));
  Cloud(60, 74, C(17, 24, 31));
  Cloud(100, 68, C(17, 24, 31));
  /* A floor of cloud tops. */
  for (int x = 0; x < kCW; x++) {
    const int k = x % 10;
    const int top = kGround - (k >= 2 && k <= 7) - (k >= 4 && k <= 5);
    P(x, top - 1, C(18, 24, 31));
    for (int y = top; y < kCH; y++)
      P(x, y, y >= kGround + 5 ? (((x + y) & 1) ? C(24, 28, 31) : C(31, 31, 31)) : C(31, 31, 31));
  }
  for (int x = 0; x < kCW; x++) P(x, kCH - 1, C(24, 28, 31));
}

static void Cave(void) {
  Rect(0, 0, kCW, kCH, C(4, 3, 6));
  for (int y = 0; y < kGround; y++)
    for (int x = 0; x < kCW; x++)
      if (Hash(x, y, 1) % 37 == 0) P(x, y, C(8, 6, 9));
  /* Back rocks and stalagmites. */
  Mound(14, kGround, 16, 12, C(7, 5, 7), C(7, 5, 7), C(9, 7, 9));
  Mound(100, kGround, 20, 10, C(7, 5, 7), C(7, 5, 7), C(9, 7, 9));
  static const int kMites[][2] = {{34, 9}, {44, 5}, {78, 11}, {90, 6}, {120, 8}};
  for (unsigned m = 0; m < sizeof(kMites) / sizeof(kMites[0]); m++) {
    const int cx = kMites[m][0], h = kMites[m][1];
    for (int j = 0; j < h; j++) {
      const int half = (j + 1) / 3;
      for (int i = -half; i <= half; i++)
        P(cx + i, kGround - 1 - (h - 1 - j), i == -half ? C(20, 13, 7) : i == half ? C(9, 5, 3) : C(14, 9, 5));
    }
  }
  /* Stalactites along the top edge. */
  for (int x = 0; x < kCW; x++) {
    const int h = 1 + (int)(Hash(x / 3, 0, 2) % 3);
    for (int y = 0; y < h; y++) P(x, y, C(12, 8, 5));
  }
  /* Rock floor: blocks with a light top edge. */
  for (int y = kGround; y < kCH; y++)
    for (int x = 0; x < kCW; x++) {
      const int ry = (y - kGround) % 4, rx = (x + ((y - kGround) / 4) * 4) % 8;
      P(x, y, ry == 3 || rx == 7 ? C(9, 5, 3) : ry == 0 ? C(25, 17, 9) : C(18, 11, 6));
    }
}

static void Water(void) {
  static const uint16_t sea[] = {C(9, 17, 30), C(6, 13, 26), C(4, 9, 21), C(3, 7, 17)};
  static const uint8_t rows[] = {0, 24, 52, 76};
  Bands(sea, rows, 4, kGround);
  /* Surface waves on the top edge. */
  for (int x = 0; x < kCW; x++) {
    P(x, 0, C(22, 28, 31));
    if ((x & 3) != 3) P(x, 1, C(22, 28, 31));
  }
  const uint16_t bpal[2] = {C(20, 26, 31), C(31, 31, 31)};
  static const int kBubbles[][2] = {{8, 72}, {30, 80}, {58, 70}, {72, 78}, {100, 74}, {118, 81}, {12, 4}, {116, 6}};
  for (unsigned b = 0; b < sizeof(kBubbles) / sizeof(kBubbles[0]); b++)
    Sprite(kBubbles[b][0], kBubbles[b][1], kBubble, 4, bpal);
  /* Seaweed. */
  static const int kWeed[][2] = {{20, 14}, {24, 9}, {80, 16}, {86, 10}, {110, 12}};
  for (unsigned w = 0; w < sizeof(kWeed) / sizeof(kWeed[0]); w++)
    for (int j = 0; j < kWeed[w][1]; j++) {
      const int x = kWeed[w][0] + ((j / 3) & 1), y = kGround - 1 - j;
      P(x, y, C(5, 20, 8));
      P(x + 1, y, C(11, 27, 12));
    }
  /* Sand. */
  Rect(0, kGround, kCW, 1, C(20, 16, 8));
  for (int y = kGround + 1; y < kCH; y++)
    for (int x = 0; x < kCW; x++) P(x, y, Hash(x, y, 3) % 9 == 0 ? C(23, 19, 10) : C(29, 25, 15));
}

static void Castle(void) {
  Bricks(0, kGround, 8, 4, C(9, 9, 12), C(12, 12, 15), C(5, 5, 8));
  /* Pillars with torches. */
  static const int kPillars[] = {28, 96};
  for (int p = 0; p < 2; p++) {
    const int px = kPillars[p];
    for (int y = 0; y < kGround; y++) {
      P(px, y, C(5, 5, 8));
      P(px + 1, y, C(18, 18, 21));
      for (int x = px + 2; x < px + 6; x++) P(x, y, C(14, 14, 17));
      P(px + 6, y, C(9, 9, 12));
      P(px + 7, y, C(5, 5, 8));
    }
    static const uint16_t fpal[4] = {C(31, 12, 2), C(31, 25, 4), C(31, 31, 20), C(10, 7, 4)};
    Sprite(px + 1, 70, kFlame, 8, fpal);
  }
  /* Stone floor over lava. */
  Rect(0, kGround, kCW, 1, C(3, 3, 5));
  for (int y = kGround + 1; y < kGround + 4; y++)
    for (int x = 0; x < kCW; x++)
      P(x, y, (x % 8) == 7 ? C(6, 6, 9) : y == kGround + 1 ? C(22, 22, 25) : C(15, 15, 18));
  for (int y = kGround + 4; y < kCH; y++)
    for (int x = 0; x < kCW; x++) {
      const int k = (x + (y - kGround) * 3) % 12;
      uint16_t c = C(28, 7, 0);
      if (y == kGround + 4) c = (x & 3) < 2 ? C(31, 24, 4) : C(31, 15, 1);
      else if (k < 2) c = C(31, 15, 1);
      P(x, y, c);
    }
}

static void Ghost(void) {
  /* Wooden planks. */
  for (int y = 0; y < kGround; y++)
    for (int x = 0; x < kCW; x++) {
      const int px = x % 6;
      uint16_t c = px == 5 ? C(3, 2, 6) : px == 0 ? C(9, 7, 13) : C(6, 5, 10);
      if (px == 2 && (y % 24) == 3) c = C(13, 11, 16);  /* nails */
      P(x, y, c);
    }
  /* A window with the moon. */
  const int wx = 102, wy = 69, ww = 22, wh = 15;
  Rect(wx - 1, wy - 1, ww + 2, wh + 2, C(8, 5, 3));
  Rect(wx, wy, ww, wh, C(14, 9, 5));
  Rect(wx + 1, wy + 1, ww - 2, wh - 2, C(2, 2, 8));
  for (int y = wy + 1; y < wy + wh - 1; y++)
    for (int x = wx + 1; x < wx + ww - 1; x++)
      if (InEllipse(x - (wx + 14), y - (wy + 6), 3, 3)) P(x, y, C(30, 29, 18));
  Rect(wx + ww / 2, wy, 1, wh, C(14, 9, 5));
  Rect(wx, wy + wh / 2, ww, 1, C(14, 9, 5));
  /* Cobweb in the lower-left corner above the floor. */
  for (int i = 0; i < 12; i++) {
    P(i, kGround - 12 + i, C(18, 18, 22));
    if (i < 8) P(i, kGround - 12 + i / 2 + 6, C(14, 14, 18));
    P(i / 2 + 6, kGround - 12 + i, C(14, 14, 18));
  }
  /* Floorboards. */
  for (int y = kGround; y < kCH; y++)
    for (int x = 0; x < kCW; x++) {
      const int band = (y - kGround) / 4, ry = (y - kGround) % 4;
      const bool seam = (x + band * 7) % 16 == 0;
      P(x, y, ry == 0 ? C(19, 12, 6) : ry == 3 || seam ? C(7, 4, 2) : C(13, 8, 4));
    }
}

static void Forest(void) {
  static const uint16_t bg[] = {C(2, 9, 5), C(3, 12, 6), C(4, 15, 7)};
  static const uint8_t rows[] = {0, 40, 70};
  Bands(bg, rows, 3, kGround);
  static const int kTrunks[] = {6, 38, 74, 110};
  for (int t = 0; t < 4; t++)
    for (int y = 0; y < kGround; y++)
      for (int x = 0; x < 7; x++) {
        const int ax = kTrunks[t] + x;
        P(ax, y, x == 0 || x == 6 ? C(6, 3, 1) : (x == 2 && y % 5 < 3) ? C(9, 5, 2) : x == 1 ? C(17, 10, 4) : C(12, 7, 3));
      }
  /* Canopy along the top, leaf clumps lower down. */
  for (int x = 0; x < kCW; x++) {
    const int h = 2 + (int)(Hash(x / 4, 0, 4) % 2);
    for (int y = 0; y < h; y++) P(x, y, y == h - 1 ? C(1, 6, 2) : C(6, 19, 6));
  }
  Mound(24, kGround, 10, 8, C(5, 18, 5), C(9, 23, 8), C(1, 6, 2));
  Mound(60, kGround, 8, 6, C(5, 18, 5), C(9, 23, 8), C(1, 6, 2));
  Mound(96, kGround, 11, 9, C(5, 18, 5), C(9, 23, 8), C(1, 6, 2));
  Ground(C(1, 6, 1), C(5, 18, 4), C(3, 11, 2), C(18, 11, 5), C(13, 7, 3));
}

static void Snow(void) {
  static const uint16_t sky[] = {C(16, 22, 31), C(20, 25, 31), C(24, 28, 31)};
  static const uint8_t rows[] = {0, 36, 66};
  Bands(sky, rows, 3, kGround);
  /* Mountains with snow caps. */
  static const int kPeaks[][3] = {{26, 62, 30}, {92, 58, 34}, {60, 72, 20}};
  for (int m = 0; m < 3; m++) {
    const int px = kPeaks[m][0], py = kPeaks[m][1], half = kPeaks[m][2];
    for (int y = py; y < kGround; y++) {
      const int w = (y - py) * half / (kGround - py);
      for (int x = px - w; x <= px + w; x++) {
        uint16_t c = x < px ? C(15, 18, 25) : C(11, 14, 21);
        if (y - py < 5 + ((x * 7) % 3)) c = C(31, 31, 31);
        if (x == px - w || x == px + w) c = C(8, 10, 17);
        P(x, y, c);
      }
    }
  }
  /* Snowflakes. */
  for (int y = 0; y < kGround; y++)
    for (int x = 0; x < kCW; x++)
      if (Hash(x, y, 5) % 61 == 0) P(x, y, C(31, 31, 31));
  /* Snow over ice. */
  Rect(0, kGround, kCW, 1, C(18, 22, 30));
  Rect(0, kGround + 1, kCW, 3, C(31, 31, 31));
  for (int y = kGround + 4; y < kCH; y++)
    for (int x = 0; x < kCW; x++) P(x, y, (x + y) % 9 == 0 ? C(27, 31, 31) : C(17, 25, 31));
}

static void Night(int variant) {
  const bool special = variant == 1;
  const uint16_t top = special ? C(9, 3, 14) : C(2, 2, 9);
  const uint16_t mid = special ? C(14, 5, 18) : C(4, 3, 13);
  const uint16_t low = special ? C(20, 8, 20) : C(7, 4, 16);
  const uint16_t sky[] = {top, mid, low};
  static const uint8_t rows[] = {0, 44, 74};
  Bands(sky, rows, 3, kGround);
  for (int y = 0; y < kGround; y++)
    for (int x = 0; x < kCW; x++) {
      const uint32_t h = Hash(x, y, 6) % 97;
      if (h == 0) P(x, y, C(31, 31, 31));
      else if (h == 1) P(x, y, C(31, 29, 16));
    }
  /* A few bigger twinkling stars. */
  static const int kStars[][2] = {{12, 72}, {50, 78}, {84, 70}, {118, 76}, {30, 0}, {104, 1}};
  for (int s = 0; s < 6; s++) {
    const int x = kStars[s][0], y = kStars[s][1];
    P(x, y, C(31, 31, 31));
    P(x - 1, y, C(31, 29, 16));
    P(x + 1, y, C(31, 29, 16));
    P(x, y - 1, C(31, 29, 16));
    P(x, y + 1, C(31, 29, 16));
  }
  const uint16_t hill = special ? C(8, 3, 10) : C(3, 4, 12), edge = special ? C(18, 8, 20) : C(8, 9, 22);
  Mound(28, kGround, 18, 9, hill, hill, edge);
  Mound(100, kGround, 22, 11, hill, hill, edge);
  Rect(0, kGround, kCW, 1, edge);
  for (int y = kGround + 1; y < kCH; y++)
    for (int x = 0; x < kCW; x++)
      P(x, y, Hash(x, y, 7) % 11 == 0 ? edge : (special ? C(5, 2, 7) : C(2, 2, 8)));
}

static void Palace(int variant) {
  static const uint16_t kTone[4][3] = {
      {C(17, 14, 2), C(13, 10, 1), C(31, 27, 6)},   /* yellow */
      {C(3, 14, 4), C(2, 10, 3), C(8, 27, 9)},      /* green */
      {C(17, 4, 3), C(13, 2, 2), C(31, 9, 7)},      /* red */
      {C(4, 7, 19), C(2, 5, 14), C(9, 15, 31)},     /* blue */
  };
  const uint16_t *t = kTone[variant & 3];
  /* Diamond tiles. */
  for (int y = 0; y < kGround; y++)
    for (int x = 0; x < kCW; x++) {
      const int dx = x % 8 - 4, dy = y % 8 - 4;
      const int d = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
      P(x, y, d == 3 ? t[1] : d < 3 ? t[0] : C(3, 3, 5));
    }
  /* Big blocks on the floor. */
  for (int y = kGround; y < kCH; y++)
    for (int x = 0; x < kCW; x++) {
      const int rx = x % 8, ry = y - kGround;
      uint16_t c = t[2];
      if (rx == 7 || ry == 7) c = C(2, 2, 3);
      else if (rx == 0 || ry == 0) c = C(31, 31, 31);
      else if (rx == 6 || ry == 6) c = t[0];
      P(x, y, c);
    }
}

static void Chocolate(void) {
  static const uint16_t sky[] = {C(31, 19, 10), C(31, 23, 13), C(31, 27, 18)};
  static const uint8_t rows[] = {0, 34, 64};
  Bands(sky, rows, 3, kGround);
  Cloud(14, 70, C(31, 20, 12));
  Cloud(94, 2, C(31, 19, 10));
  const uint16_t out = C(7, 3, 1);
  Mound(36, kGround, 16, 15, C(15, 8, 4), C(20, 12, 6), out);
  Mound(104, kGround, 14, 18, C(15, 8, 4), C(20, 12, 6), out);
  Mound(72, kGround, 9, 7, C(11, 6, 3), C(15, 8, 4), out);
  Ground(out, C(21, 12, 5), C(14, 7, 3), C(13, 7, 3), C(17, 10, 4));
}

/* The overworld: sea (or cave rock, forest, night sky) with islands, a
 * dotted path and level dots, coloured by submap. */
static void Map(int submap) {
  static const uint16_t kPal[7][6] = {
      /* sea, sea mark, land, land light, shore, outline */
      {C(5, 13, 27), C(14, 22, 31), C(8, 22, 6), C(14, 27, 9), C(28, 25, 15), C(2, 7, 16)},   /* main */
      {C(5, 13, 27), C(14, 22, 31), C(8, 22, 6), C(14, 27, 9), C(28, 25, 15), C(2, 7, 16)},   /* Yoshi's Island */
      {C(4, 3, 6), C(9, 7, 10), C(15, 10, 5), C(21, 14, 7), C(11, 8, 5), C(1, 1, 2)},         /* Vanilla Dome */
      {C(3, 10, 5), C(7, 17, 8), C(11, 21, 6), C(16, 26, 9), C(22, 18, 9), C(1, 5, 2)},       /* Forest */
      {C(9, 2, 2), C(24, 8, 1), C(10, 7, 6), C(14, 10, 8), C(6, 4, 4), C(3, 1, 1)},           /* Valley */
      {C(12, 6, 18), C(22, 14, 28), C(26, 21, 8), C(31, 27, 12), C(30, 29, 20), C(5, 2, 9)},  /* Special */
      {C(2, 2, 9), C(31, 31, 31), C(6, 5, 16), C(10, 9, 22), C(14, 14, 28), C(1, 1, 4)},      /* Star */
  };
  const uint16_t *c = kPal[(unsigned)submap < 7 ? submap : 0];
  Rect(0, 0, kCW, kCH, c[0]);
  /* Wave marks (or stars, rocks, treetops) on a loose grid. */
  for (int gy = 0; gy < kCH; gy += 6)
    for (int gx = 0; gx < kCW; gx += 10) {
      const uint32_t h = Hash(gx, gy, 8);
      const int x = gx + (int)(h % 6), y = gy + (int)((h >> 8) % 3);
      if (submap == 6) {
        P(x, y, c[1]);
      } else {
        P(x, y, c[1]);
        P(x + 1, y + 1, c[1]);
        P(x + 2, y + 1, c[1]);
        P(x + 3, y, c[1]);
      }
    }
  Island(34, 88, 38, 13, c[2], c[4], c[5]);
  Island(108, 82, 15, 6, c[2], c[4], c[5]);
  Island(120, 2, 16, 5, c[2], c[4], c[5]);
  Island(4, 0, 12, 4, c[2], c[4], c[5]);
  /* Lighter patches on the big island. */
  Mound(20, 86, 8, 4, c[3], c[3], c[3]);
  Mound(52, 90, 7, 3, c[3], c[3], c[3]);
  /* Path and level dots. */
  const uint16_t path = C(31, 28, 19), dot_out = C(3, 3, 3);
  for (int x = 12; x <= 62; x += 2) P(x, 84 - (x > 36 ? (x - 36) / 6 : 0), path);
  static const int kDots[][3] = {{12, 84, 0}, {36, 84, 1}, {62, 80, 0}, {108, 81, 1}};
  for (int d = 0; d < 4; d++) {
    const int x = kDots[d][0], y = kDots[d][1];
    const uint16_t fill = kDots[d][2] ? C(31, 8, 6) : C(31, 27, 4);
    Rect(x - 1, y - 1, 3, 3, dot_out);
    P(x - 2, y, dot_out);
    P(x + 2, y, dot_out);
    P(x, y - 2, dot_out);
    P(x, y + 2, dot_out);
    Rect(x - 1, y, 3, 1, fill);
    P(x, y - 1, fill);
    P(x, y + 1, fill);
    P(x - 1, y - 1, C(31, 31, 31));
  }
}

void HudArtDrawScene(uint16_t *fb, int theme, int variant) {
  s_fb = fb;
  switch (theme) {
    default:
    case kThemeGrass: Grass(); break;
    case kThemeSky: Sky(); break;
    case kThemeCave: Cave(); break;
    case kThemeWater: Water(); break;
    case kThemeCastle: Castle(); break;
    case kThemeGhost: Ghost(); break;
    case kThemeForest: Forest(); break;
    case kThemeSnow: Snow(); break;
    case kThemeNight: Night(variant); break;
    case kThemePalace: Palace(variant); break;
    case kThemeChocolate: Chocolate(); break;
    case kThemeMap: Map(variant); break;
  }
}

/* Panel fill per theme: a deep shade that suits the scene. */
static const uint16_t kPanelFill[kThemeCount] = {
    [kThemeGrass] = C(2, 5, 13),  [kThemeSky] = C(3, 7, 17),     [kThemeCave] = C(6, 3, 2),
    [kThemeWater] = C(1, 4, 12),  [kThemeCastle] = C(5, 2, 3),   [kThemeGhost] = C(4, 2, 9),
    [kThemeForest] = C(1, 6, 3),  [kThemeSnow] = C(4, 7, 15),    [kThemeNight] = C(3, 2, 10),
    [kThemePalace] = C(3, 3, 6),  [kThemeChocolate] = C(7, 3, 1), [kThemeMap] = C(2, 4, 11),
};

void HudArtPanel(uint16_t *fb, int theme, int x, int y, int w, int h) {
  s_fb = fb;
  const int cx = x / 2, cy = y / 2, cw = w / 2, ch = h / 2;
  const uint16_t fill = kPanelFill[(unsigned)theme < kThemeCount ? theme : 0];
  const uint16_t border = C(31, 31, 31), outline = C(0, 0, 2);
  for (int j = 0; j < ch; j++)
    for (int i = 0; i < cw; i++) {
      const bool ex = i == 0 || i == cw - 1, ey = j == 0 || j == ch - 1;
      const bool bx = i == 1 || i == cw - 2, by = j == 1 || j == ch - 2;
      if (ex && ey) continue;                      /* rounded corner */
      uint16_t c = fill;
      if (ex || ey || (bx && by)) c = outline;
      else if (bx || by) c = border;
      P(cx + i, cy + j, c);
    }
}

void HudArtGoal(uint16_t *fb, int x) {
  s_fb = fb;
  const int cx = x / 2;
  const uint16_t gold = C(31, 26, 4), dark = C(18, 12, 0), tape = C(31, 8, 8);
  for (int y = kGround - 13; y < kGround; y++) {
    P(cx, y, y == kGround - 13 ? dark : gold);
    P(cx + 1, y, dark);
    P(cx + 6, y, y == kGround - 13 ? dark : gold);
    P(cx + 7, y, dark);
  }
  for (int i = 2; i < 6; i++) {
    P(cx + i, kGround - 9, tape);
    P(cx + i, kGround - 8, C(16, 2, 2));
  }
}
