/* Bottom screen: status bar, big reserve-item box, FPS counter.
 *
 * Sub engine layout (VRAM C at 0x06200000, VRAM I for sprites):
 *   0x06200000+58K  BG0 map: the SNES status bar (BG3 map rows 0-4)
 *   0x06200000+60K  BG1 map: our own layer (item box frame, text)
 *   0x06210000      BG3 tiles as 4bpp (same conversion as the top screen)
 *   sprite VRAM     tiles copied per frame for the status-bar sprites
 */
#include "nds_hud.h"

#include <nds.h>
#include <string.h>

#include "smw_port.h"
#include "snes/ppu.h"

#define SUB_TILES   ((uint32_t *)0x06210000)
#define SUB_MAP0    ((uint16_t *)(0x06200000 + 58 * 1024))
#define SUB_MAP1    ((uint16_t *)(0x06200000 + 60 * 1024))
#define SUB_OBJ     ((uint32_t *)0x06600000)
#define SUB_OAM     ((uint16_t *)0x07000400)

enum {
  kStatusTop = 8,          /* bottom-screen y of BG3 map row 0 */
  kBoxX = 13, kBoxY = 9,   /* big item box, in 8px tiles: 6x6 tiles */
  kBoxW = 6, kBoxH = 6,
  kFontPal = 8 + 6,        /* white text (game's message palette) */
  kTileBlank = 0xfc,
  kTileCorner = 0x3a, kTileEdgeH = 0x3b, kTileEdgeV = 0x4a,
};

static bool s_show_fps = true;
static unsigned s_fps;
static int s_select_frames;
static Ppu *s_ppu;

static inline uint16_t Entry(unsigned tile, unsigned pal, bool hflip, bool vflip) {
  return (uint16_t)(tile | (hflip ? BIT(10) : 0) | (vflip ? BIT(11) : 0) | (pal << 12));
}

/* Text in the game's message font (A-Z at 0x100, a-z at 0x140) and its
 * status-bar digits (0x00-0x09). */
static void Text(int x, int y, const char *s) {
  for (; *s; s++, x++) {
    unsigned t = kTileBlank;
    const char c = *s;
    if (c >= 'A' && c <= 'Z') t = 0x100 + (c - 'A');
    else if (c >= 'a' && c <= 'z') t = 0x140 + (c - 'a');
    else if (c >= '0' && c <= '9') t = (unsigned)(c - '0');
    else if (c == '!') t = 0x11a;
    else if (c == '.') t = 0x11b;
    SUB_MAP1[y * 32 + x] = Entry(t, kFontPal, false, false);
  }
}

static void DrawBox(void) {
  /* The same corner/edge tiles the status bar uses for its item box. */
  const unsigned pal = 8 + 6;
  for (int ty = 0; ty < kBoxH; ty++) {
    for (int tx = 0; tx < kBoxW; tx++) {
      const bool top = ty == 0, bottom = ty == kBoxH - 1, left = tx == 0, right = tx == kBoxW - 1;
      uint16_t e = Entry(kTileBlank, pal, false, false);
      if ((top || bottom) && (left || right)) e = Entry(kTileCorner, pal, right, bottom);
      else if (top || bottom) e = Entry(kTileEdgeH, pal, false, bottom);
      else if (left || right) e = Entry(kTileEdgeV, pal, right, false);
      SUB_MAP1[(kBoxY + ty) * 32 + kBoxX + tx] = e;
    }
  }
}

static void DrawStaticText(void) {
  Text(kBoxX - 3, kBoxY + kBoxH + 1, "Tap to use item");
}

/* The item box and its hint only make sense inside a level. */
static void ShowLevelLayer(bool show) {
  static int shown = -1;
  if (shown == (int)show) return;
  shown = show;
  for (int y = kBoxY; y <= kBoxY + kBoxH + 1; y++)
    for (int x = 0; x < 32; x++)
      SUB_MAP1[y * 32 + x] = Entry(kTileBlank, 8, false, false);
  if (show) {
    DrawBox();
    DrawStaticText();
  }
}

void NdsHudInit(void) {
  s_ppu = PortPpu();
  videoSetModeSub(MODE_0_2D | DISPLAY_BG0_ACTIVE | DISPLAY_BG1_ACTIVE | DISPLAY_SPR_ACTIVE |
                  DISPLAY_SPR_1D | DISPLAY_SPR_1D_SIZE_32);
  vramSetBankC(VRAM_C_SUB_BG_0x06200000);
  vramSetBankI(VRAM_I_SUB_SPRITE);
  memset((void *)0x06200000, 0, 128 * 1024);
  memset((void *)0x06600000, 0, 16 * 1024);
  REG_BG0CNT_SUB = BG_PRIORITY(1) | BG_TILE_BASE(4) | BG_MAP_BASE(29) | BG_COLOR_16 | BG_32x32;
  REG_BG1CNT_SUB = BG_PRIORITY(0) | BG_TILE_BASE(4) | BG_MAP_BASE(30) | BG_COLOR_16 | BG_32x32;
  REG_BG0HOFS_SUB = REG_BG0VOFS_SUB = REG_BG1HOFS_SUB = REG_BG1VOFS_SUB = 0;
  for (int i = 0; i < 32 * 32; i++) {
    SUB_MAP0[i] = Entry(kTileBlank, 8, false, false);
    SUB_MAP1[i] = Entry(kTileBlank, 8, false, false);
  }
  for (int i = 0; i < 128; i++) SUB_OAM[i * 4] = ATTR0_DISABLED;
  /* Big item: affine group 0 at 2x (8.8 fixed point, 0x80 = 0.5). */
  SUB_OAM[3] = 0x80;
  SUB_OAM[7] = 0;
  SUB_OAM[11] = 0;
  SUB_OAM[15] = 0x80;
  /* BG3 tiles converted so far by the top-screen renderer. */
  memcpy(SUB_TILES, (const void *)0x06020000, 32 * 1024);
  BG_PALETTE_SUB[0] = RGB15(2, 3, 8);
}

void NdsHudOnBg3Tile(unsigned index, const uint32_t *tile) {
  memcpy(&SUB_TILES[index * 8], tile, 32);
}

void NdsHudSetFps(unsigned fps) {
  s_fps = fps;
}

static bool InTiles(int px, int py, int tx, int ty, int tw, int th) {
  return px >= tx * 8 && px < (tx + tw) * 8 && py >= ty * 8 && py < (ty + th) * 8;
}

uint32_t NdsHudInput(uint32_t pad) {
  const uint32_t down = keysDown();
  if (down & KEY_TOUCH) {
    touchPosition tp;
    touchRead(&tp);
    const bool in_big_box = InTiles(tp.px, tp.py, kBoxX - 1, kBoxY - 1, kBoxW + 2, kBoxH + 2);
    const bool in_small_box = tp.px >= 108 && tp.px < 148 && tp.py < kStatusTop + 40;
    if ((in_big_box || in_small_box) && PortGameRam()[0x100] == 0x14)
      s_select_frames = 2;  /* Select drops the reserve item */
    else if (tp.py >= 176 && tp.px >= 176)
      s_show_fps = !s_show_fps;
  }
  if (s_select_frames > 0) {
    s_select_frames--;
    pad |= kPortPad_Select;
  }
  return pad;
}

/* Copy a status-bar sprite's tiles (top-screen sprite VRAM, 2D layout) to
 * sub sprite VRAM (1D layout); returns the sub tile index. */
static unsigned CopySpriteTiles(unsigned snes_tile, int size, unsigned *next_slot) {
  const uint32_t *src = (const uint32_t *)0x06400000;
  const unsigned ds = ((snes_tile >> 4) << 5) | (snes_tile & 15);
  const unsigned n = size / 8;
  const unsigned slot = *next_slot;
  for (unsigned ty = 0; ty < n; ty++)
    for (unsigned tx = 0; tx < n; tx++)
      memcpy(&SUB_OBJ[(slot + ty * n + tx) * 8], &src[(ds + ty * 32 + tx) * 8], 32);
  *next_slot += n * n;
  return slot;
}

void NdsHudPresent(void) {
  const Ppu *p = s_ppu;
  const uint8_t *ram = PortGameRam();
  const bool in_level = ram[0x100] == 0x14;
  ShowLevelLayer(in_level);

  /* Palettes: BG3 colours for the status bar, sprite colours. */
  for (int pal = 0; pal < 8; pal++)
    for (int c = 1; c < 4; c++)
      BG_PALETTE_SUB[(8 + pal) * 16 + c] = p->cgram[pal * 4 + c];
  for (int i = 0; i < 128; i++) SPRITE_PALETTE_SUB[i] = p->cgram[128 + i];

  /* Status bar: BG3 map rows 0-4 (left 32 columns), only in levels. */
  const uint32_t map = PPU_bgTilemapAdr(p, 2);
  for (int row = 0; row < 5; row++) {
    uint16_t *dst = &SUB_MAP0[(kStatusTop / 8 + row) * 32];
    for (int col = 0; col < 32; col++) {
      const uint16_t e = in_level ? p->vram[(map + row * 32 + col) & 0x7fff] : kTileBlank;
      dst[col] = (e & 0x3ff) | ((e >> 4) & 0xc00) | ((((e >> 10) & 7) + 8) << 12);
    }
  }

  /* The reserve item (the only status-bar sprite: the game always draws it
   * at x=120, y=15), plus a 2x copy in the big box. SNES screen row y shows
   * BG row y+1, so the sprite moves down by 1. */
  static const uint8_t kSizes[8][2] = {
    {8, 16}, {8, 32}, {8, 64}, {16, 32}, {16, 64}, {32, 64}, {16, 32}, {16, 32},
  };
  const uint8_t *sizes = kSizes[PPU_objSize(p)];
  unsigned slot = 0, used = 1;  /* OAM entry 0 = big item */
  SUB_OAM[0] = ATTR0_DISABLED;
  for (int i = 0; in_level && i < 128 && used < 32; i++) {
    const uint16_t lo = p->oam[i * 2], hi = p->oam[i * 2 + 1];
    const uint8_t ext = p->highOam[i >> 2] >> ((i & 3) * 2);
    const int y = lo >> 8, x = (lo & 0xff) | ((ext & 1) << 8);
    const int size = sizes[(ext >> 1) & 1];
    if (x != 120 || y != 15 || size != 16) continue;
    const uint16_t tile = CopySpriteTiles((hi & 0xff) | (hi & 0x100), size, &slot);
    const uint8_t attr = hi >> 8;
    const uint16_t a1 = (size == 16 ? ATTR1_SIZE_16 : ATTR1_SIZE_8) |
                        ((attr & 0x40) ? ATTR1_FLIP_X : 0) | ((attr & 0x80) ? ATTR1_FLIP_Y : 0);
    const uint16_t a2 = (uint16_t)(tile | ATTR2_PALETTE((attr >> 1) & 7));
    SUB_OAM[used * 4 + 0] = (uint16_t)(((y + kStatusTop + 1) & 0xff) | ATTR0_SQUARE | ATTR0_COLOR_16);
    SUB_OAM[used * 4 + 1] = (uint16_t)((x & 0x1ff) | a1);
    SUB_OAM[used * 4 + 2] = a2;
    used++;
    {
      const int bx = kBoxX * 8 + (kBoxW * 8 - 32) / 2, by = kBoxY * 8 + (kBoxH * 8 - 32) / 2;
      SUB_OAM[0] = (uint16_t)(by | ATTR0_ROTSCALE_DOUBLE | ATTR0_SQUARE | ATTR0_COLOR_16);
      SUB_OAM[1] = (uint16_t)(bx | ATTR1_ROTDATA(0) | ATTR1_SIZE_16);
      SUB_OAM[2] = a2;
    }
  }
  for (unsigned i = used; i < 32; i++) SUB_OAM[i * 4] = ATTR0_DISABLED;

  /* FPS counter, bottom right. */
  char text[8] = "      ";
  if (s_show_fps) {
    text[0] = (char)('0' + (s_fps / 10) % 10);
    text[1] = (char)('0' + s_fps % 10);
    memcpy(text + 2, " FPS", 4);
  } else {
    memcpy(text, "   FPS", 6);
  }
  text[6] = 0;
  Text(25, 23, text);
}
