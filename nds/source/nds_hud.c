/* Bottom screen: a game-themed status screen.
 *
 * A 16-bit bitmap (sub engine BG3) holds a hand-drawn SMW-style scene (sky,
 * clouds, hills, grass) with semi-transparent panels on top:
 *   - the level name, in the game's own message font;
 *   - lives, coins and time, using the status bar's digits and icons at 2x;
 *   - the score;
 *   - a large reserve-item box (tap it to use the item = Select);
 *   - a level progress track with a live Mario marker (a sprite using the
 *     player's current graphics);
 *   - a small FPS counter in the corner (tap it to hide/show).
 *
 * Glyphs are copied from the converted BG3 tiles once the game has loaded
 * them (in a level or on the overworld). Only values that change are
 * redrawn, from a cached copy of the static layout.
 */
#include "nds_hud.h"

#include <nds.h>
#include <string.h>

#include "smw_port.h"
#include "snes/ppu.h"

#define SUB_FB    ((uint16_t *)0x06200000)
#define SUB_OBJ   ((uint32_t *)0x06600000)
#define SUB_OAM   ((uint16_t *)0x07000400)
#define BG3_TILES ((const uint32_t *)0x06020000)  /* top-screen BG3 tiles */
#define OBJ_TILES ((const uint32_t *)0x06400000)  /* top-screen sprite tiles */

enum { kW = 256, kH = 192 };

#define C(r, g, b) ((uint16_t)(BIT(15) | (r) | ((g) << 5) | ((b) << 10)))

/* ---- layout ------------------------------------------------------------ */

enum { kLayoutNone, kLayoutLevel, kLayoutOverworld };

/* Panels: name across the top, counters left, item + score right, the
 * progress track above the ground. */
enum {
  kNameX = 8, kNameY = 6, kNameW = 240, kNameH = 20,
  kLeftX = 8, kLeftY = 32, kLeftW = 120, kLeftH = 114,
  kRightX = 132, kRightY = 32, kRightW = 116, kRightH = 114,
  kTrackX = 8, kTrackY = 151, kTrackW = 240, kTrackH = 18,
  kBoxX = 158, kBoxY = 38,   /* 64x64: the status-bar item box at 2x */
};

/* ---- state ------------------------------------------------------------- */

static uint16_t s_scene[kW * kH];   /* background art */
static uint16_t s_base[kW * kH];    /* scene + panels + static labels */
static int s_layout = -1;
static bool s_show_fps = true;
static unsigned s_fps;
static int s_select_frames;
static Ppu *s_ppu;

/* Last drawn values (-1 = redraw). */
static int s_drawn_lives, s_drawn_coins, s_drawn_time, s_drawn_fps, s_drawn_name;
static int32_t s_drawn_score;

/* ---- glyphs from the game's BG3 graphics ------------------------------ */

typedef struct Glyph {
  uint8_t px[64];       /* colour index 0-3 (0 = transparent) */
  uint16_t pal[4];
} Glyph;

enum {
  kGlyphDigit0 = 0,            /* 10 status-bar digits */
  kGlyphX = 10,                /* the little "x" */
  kGlyphCoin = 11,
  kGlyphMario = 12,            /* 5 tiles: the MARIO logo */
  kGlyphTime = 17,             /* 3 tiles: TIME */
  kGlyphBoxCorner = 20, kGlyphBoxEdgeH = 21, kGlyphBoxEdgeV = 22,
  kGlyphCount = 23,
};
static Glyph s_glyph[kGlyphCount];

/* The status bar's BG3 palettes as set in levels (SNES BGR555 = DS RGB15),
 * fixed so the screen looks the same on the overworld. */
static const uint16_t kHudPalettes[8][4] = {
  [2] = {0x7393, 0x0000, 0x0cfb, 0x2feb},  /* MARIO logo: red */
  [6] = {0x7393, 0x0000, 0x7aab, 0x7fff},  /* digits, font, item box */
  [7] = {0x7393, 0x0000, 0x1e9b, 0x3b7f},  /* coin, TIME: gold */
};

static void CaptureTile(Glyph *g, unsigned tile, unsigned snes_pal) {
  const uint32_t *src = &BG3_TILES[tile * 8];
  for (int y = 0; y < 8; y++) {
    const uint32_t row = src[y];
    for (int x = 0; x < 8; x++) g->px[y * 8 + x] = (row >> (4 * x)) & 3;
  }
  for (int c = 0; c < 4; c++) g->pal[c] = kHudPalettes[snes_pal & 7][c] | BIT(15);
}

static void CaptureGlyphs(void) {
  for (int i = 0; i < 10; i++) CaptureTile(&s_glyph[kGlyphDigit0 + i], i, 6);
  CaptureTile(&s_glyph[kGlyphX], 0x26, 6);
  CaptureTile(&s_glyph[kGlyphCoin], 0x2e, 7);
  for (int i = 0; i < 5; i++) CaptureTile(&s_glyph[kGlyphMario + i], 0x30 + i, 2);
  for (int i = 0; i < 3; i++) CaptureTile(&s_glyph[kGlyphTime + i], 0x3d + i, 7);
  CaptureTile(&s_glyph[kGlyphBoxCorner], 0x3a, 6);
  CaptureTile(&s_glyph[kGlyphBoxEdgeH], 0x3b, 6);
  CaptureTile(&s_glyph[kGlyphBoxEdgeV], 0x4a, 6);
}

/* ---- drawing primitives (into a 256x192 16-bit buffer) ----------------- */

static inline void Put(uint16_t *fb, int x, int y, uint16_t c) {
  if ((unsigned)x < kW && (unsigned)y < kH) fb[y * kW + x] = c;
}

static uint16_t Blend(uint16_t a, uint16_t b, int t /* 0..16: amount of b */) {
  const int r = ((a & 31) * (16 - t) + (b & 31) * t) >> 4;
  const int g = (((a >> 5) & 31) * (16 - t) + ((b >> 5) & 31) * t) >> 4;
  const int bl = (((a >> 10) & 31) * (16 - t) + ((b >> 10) & 31) * t) >> 4;
  return C(r, g, bl);
}

/* Glyph at integer scale, with a 1-scaled-pixel dark drop shadow. */
static void DrawGlyph(uint16_t *fb, const Glyph *g, int x, int y, int s, bool shadow,
                      bool hflip, bool vflip) {
  for (int pass = shadow ? 0 : 1; pass < 2; pass++) {
    const int ox = pass ? 0 : s, oy = pass ? 0 : s;
    for (int ty = 0; ty < 8; ty++) {
      for (int tx = 0; tx < 8; tx++) {
        const int sx = hflip ? 7 - tx : tx, sy = vflip ? 7 - ty : ty;
        const uint8_t p = g->px[sy * 8 + sx];
        if (!p) continue;
        const uint16_t c = pass ? g->pal[p] : C(1, 2, 5);
        for (int dy = 0; dy < s; dy++)
          for (int dx = 0; dx < s; dx++) Put(fb, x + tx * s + dx + ox, y + ty * s + dy + oy, c);
      }
    }
  }
}

/* A tile of the game's message font (A-Z, a-z, digits, punctuation). */
static void DrawFontTile(uint16_t *fb, unsigned code, int x, int y) {
  Glyph g;
  CaptureTile(&g, 0x100 + code, 6);
  DrawGlyph(fb, &g, x, y, 1, true, false, false);
}

static void DrawFontText(uint16_t *fb, const char *s, int x, int y) {
  for (; *s; s++, x += 8) {
    const char c = *s;
    if (c >= 'A' && c <= 'Z') DrawFontTile(fb, (unsigned)(c - 'A'), x, y);
    else if (c >= 'a' && c <= 'z') DrawFontTile(fb, 0x40 + (unsigned)(c - 'a'), x, y);
  }
}

static void FillCircle(uint16_t *fb, int cx, int cy, int r, uint16_t c) {
  for (int y = -r; y <= r; y++)
    for (int x = -r; x <= r; x++)
      if (x * x + y * y <= r * r) Put(fb, cx + x, cy + y, c);
}

/* ---- the scene --------------------------------------------------------- */

static void Cloud(uint16_t *fb, int x, int y) {
  const uint16_t shade = C(24, 28, 31), white = C(31, 31, 31);
  FillCircle(fb, x, y + 2, 9, shade);
  FillCircle(fb, x + 12, y - 3, 11, shade);
  FillCircle(fb, x + 26, y + 2, 9, shade);
  FillCircle(fb, x, y, 8, white);
  FillCircle(fb, x + 12, y - 5, 10, white);
  FillCircle(fb, x + 26, y, 8, white);
}

/* A rounded SMW-style hill: a tall half-ellipse with a dark outline. */
static void Hill(uint16_t *fb, int cx, int base, int rx, int ry, uint16_t fill, uint16_t light) {
  const uint16_t outline = C(1, 7, 3);
  for (int y = base - ry - 1; y <= base; y++) {
    for (int x = cx - rx - 1; x <= cx + rx + 1; x++) {
      const int dx = x - cx, dy = y - base;
      const int64_t in = (int64_t)dx * dx * ry * ry + (int64_t)dy * dy * rx * rx;
      const int64_t r2 = (int64_t)rx * rx * ry * ry;
      if (in > r2) continue;
      const int64_t in2 = (int64_t)(dx < 0 ? dx - 2 : dx + 2) * (dx < 0 ? dx - 2 : dx + 2) * ry * ry +
                          (int64_t)(dy - 2) * (dy - 2) * rx * rx;
      uint16_t c = fill;
      if (in2 > r2) c = outline;
      else if (dx < -rx / 3 && dy < -ry / 3) c = light;
      Put(fb, x, y, c);
    }
  }
}

static void DrawScene(void) {
  uint16_t *fb = s_scene;
  for (int y = 0; y < kH; y++) {
    const int t = y * 16 / kH;
    const uint16_t c = Blend(C(10, 19, 31), C(22, 28, 31), t);
    for (int x = 0; x < kW; x++) fb[y * kW + x] = c;
  }
  Cloud(fb, 30, 40);
  Cloud(fb, 190, 70);
  Cloud(fb, 120, 120);
  Hill(fb, 40, 178, 34, 70, C(9, 24, 10), C(16, 29, 14));
  Hill(fb, 214, 178, 30, 56, C(9, 24, 10), C(16, 29, 14));
  Hill(fb, 128, 178, 26, 34, C(4, 18, 6), C(10, 24, 9));
  /* Grass edge and dirt with the game's diagonal stripes. */
  for (int y = 172; y < kH; y++) {
    for (int x = 0; x < kW; x++) {
      uint16_t c;
      if (y == 172) c = C(1, 7, 2);
      else if (y < 178) c = C(5, 23, 4);
      else if (y == 178) c = C(3, 13, 3);
      else c = ((x + y) % 12 < 2) ? C(23, 14, 6) : C(29, 20, 9);
      fb[y * kW + x] = c;
    }
  }
}

/* Semi-transparent rounded panel with a light border. */
static void Panel(uint16_t *fb, int x0, int y0, int w, int h) {
  const int r = 5;
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      const int cx = x < r ? r - x : x >= w - r ? x - (w - r - 1) : 0;
      const int cy = y < r ? r - y : y >= h - r ? y - (h - r - 1) : 0;
      const int d = cx * cx + cy * cy;
      if (d > r * r) continue;
      const bool edge = x == 0 || y == 0 || x == w - 1 || y == h - 1 || d > (r - 1) * (r - 1);
      uint16_t *p = &fb[(y0 + y) * kW + x0 + x];
      *p = edge ? C(29, 30, 31) : Blend(*p, C(2, 3, 10), 11);
    }
  }
}

/* The status bar's item box (corner/edge tiles) at 2x: 64x64. */
static void DrawItemBox(uint16_t *fb, int x, int y) {
  for (int ty = 0; ty < 4; ty++) {
    for (int tx = 0; tx < 4; tx++) {
      const bool top = ty == 0, bottom = ty == 3, left = tx == 0, right = tx == 3;
      const Glyph *g = NULL;
      if ((top || bottom) && (left || right)) g = &s_glyph[kGlyphBoxCorner];
      else if (top || bottom) g = &s_glyph[kGlyphBoxEdgeH];
      else if (left || right) g = &s_glyph[kGlyphBoxEdgeV];
      if (g) DrawGlyph(fb, g, x + tx * 16, y + ty * 16, 2, false, right, bottom);
    }
  }
}

static void BuildLayout(int layout) {
  memcpy(s_base, s_scene, sizeof(s_base));
  if (layout != kLayoutNone) {
    Panel(s_base, kNameX, kNameY, kNameW, kNameH);
    Panel(s_base, kLeftX, kLeftY, kLeftW, kLeftH);
    Panel(s_base, kRightX, kRightY, kRightW, kRightH);
    for (int i = 0; i < 5; i++)
      DrawGlyph(s_base, &s_glyph[kGlyphMario + i], kLeftX + 20 + i * 16, kLeftY + 8, 2, true, false, false);
    DrawGlyph(s_base, &s_glyph[kGlyphX], kLeftX + 40, kLeftY + 30, 2, true, false, false);
    DrawGlyph(s_base, &s_glyph[kGlyphCoin], kLeftX + 12, kLeftY + 56, 2, true, false, false);
    DrawGlyph(s_base, &s_glyph[kGlyphX], kLeftX + 40, kLeftY + 56, 2, true, false, false);
    DrawItemBox(s_base, kBoxX, kBoxY);
  }
  if (layout == kLayoutLevel) {
    DrawFontText(s_base, "Touch to use", kRightX + 10, kBoxY + 68);
    for (int i = 0; i < 3; i++)
      DrawGlyph(s_base, &s_glyph[kGlyphTime + i], kLeftX + 8 + i * 16, kLeftY + 86, 2, true, false, false);
    Panel(s_base, kTrackX, kTrackY, kTrackW, kTrackH);
    /* Track line from the start post to the goal posts. */
    const int ty = kTrackY + 11;
    for (int x = kTrackX + 14; x < kTrackX + kTrackW - 16; x++) {
      Put(s_base, x, ty, C(29, 30, 31));
      Put(s_base, x, ty + 1, C(12, 14, 20));
    }
    for (int y = kTrackY + 3; y < kTrackY + 15; y++) {
      Put(s_base, kTrackX + 12, y, C(29, 30, 31));
      Put(s_base, kTrackX + kTrackW - 16, y, C(31, 26, 4));
      Put(s_base, kTrackX + kTrackW - 9, y, C(31, 26, 4));
    }
    for (int x = kTrackX + kTrackW - 15; x < kTrackX + kTrackW - 9; x++)
      Put(s_base, x, kTrackY + 6, C(31, 8, 8));  /* goal tape */
  }
  DC_FlushRange(s_base, sizeof(s_base));
  dmaCopyWords(3, s_base, SUB_FB, sizeof(s_base));
  s_drawn_lives = s_drawn_coins = s_drawn_time = s_drawn_fps = s_drawn_name = -1;
  s_drawn_score = -1;
}

/* Restore a rectangle of the static layout (in video memory). */
static void Restore(int x0, int y0, int w, int h) {
  for (int y = y0; y < y0 + h; y++)
    memcpy(&SUB_FB[y * kW + x0], &s_base[y * kW + x0], (size_t)w * 2);
}

/* Draw a number with the status-bar digits. Draws into video memory via a
 * small scratch area: render into s_scratch, then copy the rectangle. */
static uint16_t s_scratch[kW * 40];

static void DrawNumber(int value, int digits, int x, int y, int scale, bool zero_pad, int right_x) {
  char buf[12];
  int n = 0;
  do {
    buf[n++] = (char)(value % 10);
    value /= 10;
  } while (value && n < 11);
  while (zero_pad && n < digits) buf[n++] = 0;
  const int w = 8 * scale * n;
  const int start = right_x >= 0 ? right_x - w : x;
  const int h = 8 * scale + scale;
  const int clear_w = 8 * scale * digits + scale;
  const int clear_x = right_x >= 0 ? right_x - 8 * scale * digits : x;
  /* Compose in scratch (rows y..y+h of s_base), then copy. */
  for (int r = 0; r < h; r++)
    memcpy(&s_scratch[r * kW], &s_base[(y + r) * kW], kW * 2);
  for (int i = 0; i < n; i++) {
    const Glyph *g = &s_glyph[kGlyphDigit0 + buf[n - 1 - i]];
    DrawGlyph(s_scratch - y * kW, g, start + i * 8 * scale, y, scale, true, false, false);
  }
  for (int r = 0; r < h; r++)
    memcpy(&SUB_FB[(y + r) * kW + clear_x], &s_scratch[r * kW + clear_x], (size_t)clear_w * 2);
}

/* DrawGlyph with a buffer pointer offset so (x, y) are screen coordinates
 * while writing into s_scratch rows; Put() bounds checks use kH, so keep
 * y within the screen. */

/* Codes (message font tile - 0x100) of the name to show, and a hash. In a
 * level: from the level number. On the overworld: read back from the
 * overworld's own status bar (BG3 map row 4, 19 tiles from column 11),
 * which names the spot Mario is standing on. */
static int NameCodes(int layout, uint8_t *codes, int max) {
  if (layout == kLayoutLevel) return PortLevelNameCodes(PortGameRam()[0x13bf], codes, max);
  const uint16_t *vram = s_ppu->vram;
  const uint32_t map = PPU_bgTilemapAdr(s_ppu, 2);
  int n = 0;
  for (int i = 0; i < 19 && n < max; i++) {
    const uint16_t e = vram[(map + 4 * 32 + 11 + i) & 0x7fff];
    codes[n++] = (e & 0x300) == 0x100 ? (uint8_t)(e & 0x7f) : 0x1f;
  }
  while (n > 0 && codes[n - 1] == 0x1f) n--;
  return n;
}

static void DrawName(const uint8_t *codes, int n) {
  const int x0 = kNameX + (kNameW - n * 8) / 2, y0 = kNameY + 6;
  Restore(kNameX + 2, kNameY + 2, kNameW - 4, kNameH - 4);
  for (int r = 0; r < 10; r++)
    memcpy(&s_scratch[r * kW], &s_base[(y0 + r) * kW], kW * 2);
  for (int i = 0; i < n; i++) {
    Glyph g;
    CaptureTile(&g, 0x100 + codes[i], 6);
    DrawGlyph(s_scratch - y0 * kW, &g, x0 + i * 8, y0, 1, true, false, false);
  }
  for (int r = 0; r < 10; r++)
    memcpy(&SUB_FB[(y0 + r) * kW + kNameX + 2], &s_scratch[r * kW + kNameX + 2], (kNameW - 4) * 2);
}

static void DrawFps(void) {
  const int y = 182;
  for (int r = 0; r < 9; r++) memcpy(&s_scratch[r * kW], &s_base[(y + r) * kW], kW * 2);
  if (s_show_fps) {
    char t[8] = {(char)('0' + (s_fps / 10) % 10), (char)('0' + s_fps % 10), ' ', 'F', 'P', 'S', 0};
    for (int i = 0; t[i]; i++) {
      Glyph g;
      const char c = t[i];
      if (c >= '0' && c <= '9') g = s_glyph[kGlyphDigit0 + (c - '0')];
      else if (c == ' ') continue;
      else CaptureTile(&g, 0x100 + (unsigned)(c - 'A'), 6);
      DrawGlyph(s_scratch - y * kW, &g, 200 + i * 8, y, 1, true, false, false);
    }
  }
  for (int r = 0; r < 9; r++) memcpy(&SUB_FB[(y + r) * kW + 196], &s_scratch[r * kW + 196], 60 * 2);
}

/* "Game saved!" / "Save failed!" in the bottom-left corner. The font tiles
 * may not be loaded yet (title screen), so only shown with a layout. */
static int s_seen_saves, s_message_frames;

static void DrawSaveMessage(const char *text) {
  const int y = 180, x = 8, w = 120;
  for (int r = 0; r < 10; r++) memcpy(&s_scratch[r * kW], &s_base[(y + r) * kW], kW * 2);
  for (int i = 0; text && text[i]; i++) {
    const char c = text[i];
    unsigned code;
    if (c >= 'A' && c <= 'Z') code = (unsigned)(c - 'A');
    else if (c >= 'a' && c <= 'z') code = 0x40 + (unsigned)(c - 'a');
    else if (c == '!') code = 0x1a;
    else continue;
    Glyph g;
    CaptureTile(&g, 0x100 + code, 6);
    DrawGlyph(s_scratch - y * kW, &g, x + i * 8, y, 1, true, false, false);
  }
  for (int r = 0; r < 10; r++) memcpy(&SUB_FB[(y + r) * kW + x], &s_scratch[r * kW + x], w * 2);
}

static void UpdateSaveMessage(void) {
  bool ok;
  const int saves = PortSaveStatus(&ok);
  if (s_layout == kLayoutNone) {
    s_seen_saves = saves;  /* e.g. the copy the game makes when a file is picked */
  } else if (saves != s_seen_saves) {
    s_seen_saves = saves;
    DrawSaveMessage(ok ? "Game saved!" : "Save failed!");
    s_message_frames = 180;
  } else if (s_message_frames > 0 && --s_message_frames == 0) {
    DrawSaveMessage(NULL);
  }
}

/* ---- public ------------------------------------------------------------ */

void NdsHudInit(void) {
  s_ppu = PortPpu();
  videoSetModeSub(MODE_5_2D | DISPLAY_BG3_ACTIVE | DISPLAY_SPR_ACTIVE | DISPLAY_SPR_1D |
                  DISPLAY_SPR_1D_SIZE_32);
  vramSetBankC(VRAM_C_SUB_BG_0x06200000);
  vramSetBankI(VRAM_I_SUB_SPRITE);
  REG_BG3CNT_SUB = BG_BMP16_256x256 | BG_BMP_BASE(0) | BG_PRIORITY(3);
  REG_BG3PA_SUB = 256;
  REG_BG3PB_SUB = 0;
  REG_BG3PC_SUB = 0;
  REG_BG3PD_SUB = 256;
  REG_BG3X_SUB = 0;
  REG_BG3Y_SUB = 0;
  memset((void *)0x06600000, 0, 16 * 1024);
  for (int i = 0; i < 128; i++) SUB_OAM[i * 4] = ATTR0_DISABLED;
  /* Affine group 0 at 2x (8.8 fixed point, 0x80 = 0.5) for the item. */
  SUB_OAM[3] = 0x80;
  SUB_OAM[7] = 0;
  SUB_OAM[11] = 0;
  SUB_OAM[15] = 0x80;
  DrawScene();
  s_layout = -1;
}

void NdsHudOnBg3Tile(unsigned index, const uint32_t *tile) {
  (void)index;
  (void)tile;
}

void NdsHudSetStats(unsigned fps, unsigned cpu_percent) {
  (void)cpu_percent;
  s_fps = fps;
}

static bool InRect(int px, int py, int x, int y, int w, int h) {
  return px >= x && px < x + w && py >= y && py < y + h;
}

uint32_t NdsHudInput(uint32_t pad) {
  if (keysDown() & KEY_TOUCH) {
    touchPosition tp;
    touchRead(&tp);
    if (s_layout == kLayoutLevel && InRect(tp.px, tp.py, kRightX, kRightY, kRightW, 80))
      s_select_frames = 2;  /* Select drops the reserve item */
    else if (tp.py >= 176 && tp.px >= 188)
      s_show_fps = !s_show_fps, s_drawn_fps = -1;
  }
  if (s_select_frames > 0) {
    s_select_frames--;
    pad |= kPortPad_Select;
  }
  return pad;
}

/* Copy a 16x16 sprite (SNES tile t, 2D layout on the top screen) into sub
 * sprite VRAM slot (1D, 4 tiles). */
static void CopySprite16(unsigned snes_tile, unsigned slot) {
  const unsigned ds = ((snes_tile >> 4) << 5) | (snes_tile & 15);
  static const unsigned kOff[4] = {0, 1, 32, 33};
  for (int i = 0; i < 4; i++)
    memcpy(&SUB_OBJ[(slot + i) * 8], &OBJ_TILES[(ds + kOff[i]) * 8], 32);
}

void NdsHudPresent(void) {
  const Ppu *p = s_ppu;
  const uint8_t *ram = PortGameRam();
  const uint8_t mode = ram[0x100];
  const int layout = (mode >= 0x13 && mode <= 0x14) ? kLayoutLevel : mode == 0x0e ? kLayoutOverworld : kLayoutNone;

  if (layout != s_layout) {
    if (layout != kLayoutNone) CaptureGlyphs();
    BuildLayout(layout);
    s_layout = layout;
  }

  for (int i = 0; i < 128; i++) SPRITE_PALETTE_SUB[i] = p->cgram[128 + i];

  if (layout != kLayoutNone) {
    const int lives = ram[0xdbe] + 1, coins = ram[0xdbf];
    if (lives != s_drawn_lives) {
      DrawNumber(lives > 99 ? 99 : lives, 2, 0, kLeftY + 30, 2, false, kLeftX + 104);
      s_drawn_lives = lives;
    }
    if (coins != s_drawn_coins) {
      DrawNumber(coins, 2, 0, kLeftY + 56, 2, false, kLeftX + 104);
      s_drawn_coins = coins;
    }
    const int32_t score = (int32_t)(ram[0xf34] | ram[0xf35] << 8 | ram[0xf36] << 16) * 10;
    if (score != s_drawn_score) {
      DrawNumber(score, 7, 0, kBoxY + 84, 2, false, kRightX + kRightW - 6);
      s_drawn_score = score;
    }
    uint8_t codes[32];
    const int n = NameCodes(layout, codes, 28);
    int hash = n;
    for (int i = 0; i < n; i++) hash = hash * 31 + codes[i];
    hash &= 0x7fffffff;
    if (hash != s_drawn_name) {
      DrawName(codes, n);
      s_drawn_name = hash;
    }
  }
  if (layout == kLayoutLevel) {
    const int time = ram[0xf31] * 100 + ram[0xf32] * 10 + ram[0xf33];
    if (time != s_drawn_time) {
      DrawNumber(time, 3, 0, kLeftY + 86, 2, true, kLeftX + 112);
      s_drawn_time = time;
    }
  }
  UpdateSaveMessage();
  const int fps_key = s_show_fps ? (int)s_fps : 1000;
  if (fps_key != s_drawn_fps) {
    DrawFps();
    s_drawn_fps = fps_key;
  }

  /* Sprites: 0 = reserve item at 2x, 1-2 = Mario on the progress track. */
  SUB_OAM[0] = ATTR0_DISABLED;
  SUB_OAM[4] = ATTR0_DISABLED;
  SUB_OAM[8] = ATTR0_DISABLED;
  if (layout == kLayoutNone) return;
  static const uint8_t kSizes[8][2] = {
    {8, 16}, {8, 32}, {8, 64}, {16, 32}, {16, 64}, {32, 64}, {16, 32}, {16, 32},
  };
  const uint8_t *sizes = kSizes[PPU_objSize(p)];
  for (int i = 0; i < 128; i++) {
    const uint16_t lo = p->oam[i * 2], hi = p->oam[i * 2 + 1];
    const uint8_t ext = p->highOam[i >> 2] >> ((i & 3) * 2);
    /* The game always draws the reserve item at x=120, y=15. */
    if ((lo & 0xff) != 120 || (lo >> 8) != 15 || (ext & 1) || sizes[(ext >> 1) & 1] != 16) continue;
    CopySprite16((hi & 0xff) | (hi & 0x100), 0);
    /* Double-size area is 32x32; centre it in the 64x64 box. */
    const int bx = kBoxX + 16, by = kBoxY + 16;
    SUB_OAM[0] = (uint16_t)(by | ATTR0_ROTSCALE_DOUBLE | ATTR0_SQUARE | ATTR0_COLOR_16);
    SUB_OAM[1] = (uint16_t)(bx | ATTR1_ROTDATA(0) | ATTR1_SIZE_16);
    SUB_OAM[2] = (uint16_t)(0 | ATTR2_PALETTE(((hi >> 8) >> 1) & 7));
    break;
  }
  if (layout == kLayoutLevel) {
    /* Progress: Mario's x (or y in vertical levels) over the level size. */
    const bool vertical = ram[0x5b] & 1;
    const int screens = ram[0x5d] ? ram[0x5d] : 1;
    const int pos = vertical ? (int)(ram[0x96] | ram[0x97] << 8) : (int)(ram[0x94] | ram[0x95] << 8);
    int frac = pos * 256 / (screens * 256);
    if (frac < 0) frac = 0;
    if (frac > 256) frac = 256;
    const int x0 = kTrackX + 12, x1 = kTrackX + kTrackW - 16;
    const int mx = x0 + (x1 - x0) * frac / 256 - 8;
    /* The player's two 16x16 sprites (tiles 0x00 and 0x02 are re-uploaded
     * every frame with the current pose); face the same way as on top. */
    bool flip = false;
    for (int i = 0; i < 128; i++)
      if ((p->oam[i * 2 + 1] & 0x1ff) == 0x000) { flip = (p->oam[i * 2 + 1] >> 8) & 0x40; break; }
    CopySprite16(0x00, 4);
    CopySprite16(0x02, 8);
    const int feet = kTrackY + 11;
    for (int part = 0; part < 2; part++) {
      SUB_OAM[(1 + part) * 4 + 0] = (uint16_t)(((feet - 32 + part * 16) & 0xff) | ATTR0_SQUARE | ATTR0_COLOR_16);
      SUB_OAM[(1 + part) * 4 + 1] = (uint16_t)((mx & 0x1ff) | ATTR1_SIZE_16 | (flip ? ATTR1_FLIP_X : 0));
      SUB_OAM[(1 + part) * 4 + 2] = (uint16_t)((4 + part * 4) | ATTR2_PALETTE(0));
    }
  }
}
