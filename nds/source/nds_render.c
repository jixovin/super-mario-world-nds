/* Hardware renderer: SNES PPU state -> DS main 2D engine (top screen).
 *
 * SNES and DS 2D hardware are close relatives: both use 8x8 tiles, 32x32
 * tile maps, 16-colour palettes and the same 15-bit BGR colour format.
 *
 *   SNES                          DS (main engine, mode 0)
 *   BG1 high-priority tiles   ->  BG0   (SNES tiles carry a priority bit;
 *   BG1 low-priority tiles    ->  BG1    DS tiles don't, so BG1 is split)
 *   BG2                       ->  BG2
 *   BG3 (2bpp, converted)     ->  BG3
 *   sprites                   ->  sprites (2D tile mapping)
 *
 * DS video memory layout:
 *   0x06000000  SNES VRAM interpreted as 4bpp tiles, converted in place
 *   0x06010000  DS tile maps (8 KB per BG)
 *   0x06020000  BG3 2bpp tiles converted to 4bpp
 *   0x06400000  sprite tiles, re-laid from the SNES 16-wide grid to the
 *               DS 32-wide grid
 *
 * Supported so far: SNES mode 1 (nearly all of the game), scroll splits
 * and per-line scroll/backdrop changes, brightness fades, mosaic, and
 * windows (message box, keyhole, circle wipes) via DS windows 0/1 with
 * per-line positions; window colour effects (clip to black, no colour
 * math) via DS darkening.
 * Not yet: mode 7 and translucent colour math.
 */
#include "nds_render.h"

#include <nds.h>
#include <string.h>

#include "smw_port.h"
#include "snes/ppu.h"

enum {
  kDsRows = 192,
  kSnesRows = 224,
  kBlankTile = 1023,           /* index forced transparent for BG1 splits */
};

#define DS_BG_TILES   ((uint32_t *)0x06000000)
#define DS_BG_MAPS    ((uint16_t *)0x06010000)
#define DS_BG3_TILES  ((uint32_t *)0x06020000)
#define DS_OBJ_TILES  ((uint32_t *)0x06400000)
#define DS_OAM        ((uint16_t *)0x07000000)

/* Per-line PPU state captured while the game's frame runs. */
typedef struct LineRegs {
  uint16_t hs[4], vs[4];
  uint16_t fixed;
  uint8_t inidisp, bgmode, main, sub, cgwsel, cgadsub, mosaic;
  uint8_t main_win, sub_win, w1l, w1r, w2l, w2r;
  uint32_t windowsel;
} LineRegs;

static LineRegs s_lines[kSnesRows];
static Ppu *s_ppu;

/* Per-line tables read by HBlank DMA; one is displayed while the other is
 * filled. Entry 0 is written directly at VBlank, entries 1..192 by DMA. */
static uint16_t s_scroll[2][kDsRows + 1][8] __attribute__((aligned(32)));
static uint16_t s_backdrop[2][kDsRows + 1] __attribute__((aligned(32)));
static uint32_t s_winpos[2][kDsRows + 1] __attribute__((aligned(32)));
static int s_table;

static uint16_t s_vram_shadow[0x8000];   /* SNES VRAM as of the last capture */
static uint32_t s_dirty[2048 / 32];       /* 16-word blocks changed since then */
static bool s_full_refresh = true;
static bool s_maps_full_refresh = true;
static uint8_t s_last_bgxsc[3];

/* Built before VBlank, copied during it. */
static uint16_t s_oam_stage[128 * 4];
static unsigned s_crop;
static int s_ready_table = -1;
static uint8_t s_last_obsel = 0xff;
static uint16_t s_last_tile_adr = 0xffff;

/* s_spread[b]: bit (7-x) of b moved to bit 4x, i.e. one bitplane byte
 * spread over the 8 nibbles of a packed 4bpp DS row. */
static uint32_t s_spread[256];

void NdsRenderInit(void) {
  for (int b = 0; b < 256; b++) {
    uint32_t v = 0;
    for (int x = 0; x < 8; x++)
      if (b & (0x80 >> x)) v |= 1u << (4 * x);
    s_spread[b] = v;
  }
  videoSetMode(MODE_0_2D);
  vramSetBankA(VRAM_A_MAIN_BG_0x06000000);
  vramSetBankB(VRAM_B_MAIN_BG_0x06020000);
  vramSetBankE(VRAM_E_MAIN_SPRITE);
  memset((void *)0x06000000, 0, 256 * 1024);
  memset((void *)0x06400000, 0, 64 * 1024);
  for (int i = 0; i < 128; i++) {
    DS_OAM[i * 4 + 0] = ATTR0_DISABLED;
    DS_OAM[i * 4 + 1] = 0;
    DS_OAM[i * 4 + 2] = 0;
  }
  s_ppu = PortPpu();
}

static void CaptureLine(int line) {
  if (line < 1 || line > kSnesRows) return;
  const Ppu *p = s_ppu;
  LineRegs *l = &s_lines[line - 1];
  memcpy(l->hs, p->hScroll, sizeof(l->hs));
  memcpy(l->vs, p->vScroll, sizeof(l->vs));
  l->fixed = p->fixedColor;
  l->inidisp = p->inidisp;
  l->bgmode = p->bgmode;
  l->main = p->screenEnabled[0];
  l->sub = p->screenEnabled[1];
  l->cgwsel = p->cgwsel;
  l->cgadsub = p->cgadsub;
  l->mosaic = p->mosaic;
  l->main_win = p->screenWindowed[0];
  l->sub_win = p->screenWindowed[1];
  l->w1l = p->window1left;
  l->w1r = p->window1right;
  l->w2l = p->window2left;
  l->w2r = p->window2right;
  l->windowsel = p->windowsel;
}


/* ---- tiles ---------------------------------------------------------- */

static inline void Convert4bpp(const uint16_t *src, uint32_t *dst) {
  for (int r = 0; r < 8; r++) {
    const uint16_t a = src[r], b = src[8 + r];
    dst[r] = s_spread[a & 0xff] | s_spread[a >> 8] << 1 |
             s_spread[b & 0xff] << 2 | s_spread[b >> 8] << 3;
  }
}

static inline void Convert2bpp(const uint16_t *src, uint32_t *dst) {
  for (int r = 0; r < 8; r++) {
    const uint16_t a = src[r];
    dst[r] = s_spread[a & 0xff] | s_spread[a >> 8] << 1;
  }
}

/* Before VBlank: find the 16-word SNES VRAM blocks that changed. */
static void FindChanges(void) {
  const Ppu *p = s_ppu;
  if (p->obsel != s_last_obsel || p->bgTileAdr != s_last_tile_adr) {
    s_full_refresh = true;
    s_last_obsel = p->obsel;
    s_last_tile_adr = p->bgTileAdr;
  }
  for (int i = 0; i < 3; i++) {
    if (p->bgXsc[i] != s_last_bgxsc[i]) {
      s_maps_full_refresh = true;
      s_last_bgxsc[i] = p->bgXsc[i];
    }
  }
  const uint32_t *a = (const uint32_t *)p->vram;
  uint32_t *s = (uint32_t *)s_vram_shadow;
  for (uint32_t blk = 0; blk < 2048; blk++, a += 8, s += 8) {
    if (!s_full_refresh && a[0] == s[0] && a[1] == s[1] && a[2] == s[2] && a[3] == s[3] &&
        a[4] == s[4] && a[5] == s[5] && a[6] == s[6] && a[7] == s[7])
      continue;
    memcpy(s, a, 32);
    s_dirty[blk >> 5] |= 1u << (blk & 31);
  }
  s_full_refresh = false;
}

/* During VBlank: convert one changed block into each DS view of it. */
static void ConvertBlock(uint32_t blk) {
  const Ppu *p = s_ppu;
  const uint16_t *vram = s_vram_shadow;
  const uint32_t w = blk * 16;
  const uint32_t bg12_base = PPU_bgTileAdr(p, 0);       /* words */
  const uint32_t bg3_base = PPU_bgTileAdr(p, 2);
  const uint32_t obj1 = PPU_objTileAdr1(p) & 0x7fff;
  const uint32_t obj2 = PPU_objTileAdr2(p) & 0x7fff;

  /* 4bpp view, for BG1/BG2. */
  if (blk == ((bg12_base / 16 + kBlankTile) & 2047))
    memset(&DS_BG_TILES[blk * 8], 0, 32);
  else
    Convert4bpp(&vram[w], &DS_BG_TILES[blk * 8]);

  /* 2bpp view, for BG3: two 8-word tiles per block. */
  const uint32_t rel3 = (w - bg3_base) & 0x7fff;
  if (rel3 < 1024 * 8) {
    Convert2bpp(&vram[w], &DS_BG3_TILES[(rel3 / 8) * 8]);
    Convert2bpp(&vram[w + 8], &DS_BG3_TILES[(rel3 / 8 + 1) * 8]);
  }

  /* Sprite tiles: SNES grid is 16 tiles wide, DS 2D grid is 32 wide. */
  const uint32_t rel1 = (w - obj1) & 0x7fff, rel2 = (w - obj2) & 0x7fff;
  uint32_t t = 0xffff;
  if (rel1 < 256 * 16) t = rel1 / 16;
  else if (rel2 < 256 * 16) t = 256 + rel2 / 16;
  if (t != 0xffff)
    Convert4bpp(&vram[w], &DS_OBJ_TILES[(((t >> 4) << 5) | (t & 15)) * 8]);
}

/* ---- tile maps ------------------------------------------------------ */

static inline uint16_t MapEntry(uint16_t e, uint16_t pal_add) {
  return (e & 0x3ff) | ((e >> 4) & 0xc00) | ((((e >> 10) & 7) + pal_add) << 12);
}

/* SNES and DS order 32x32 map screens the same way for every map size. */
static void ConvertMap(int layer, uint16_t *dst_a, uint16_t *dst_b, uint16_t pal_add) {
  const Ppu *p = s_ppu;
  const uint32_t base = PPU_bgTilemapAdr(p, layer);
  const int screens = (PPU_bgTilemapWider(p, layer) ? 2 : 1) * (PPU_bgTilemapHigher(p, layer) ? 2 : 1);
  const uint16_t blank = kBlankTile;
  for (int i = 0; i < screens * 1024; i++) {
    const uint32_t blk = ((base + i) & 0x7fff) >> 4;
    if (!s_maps_full_refresh && !(s_dirty[blk >> 5] & (1u << (blk & 31)))) {
      i |= 15;  /* skip the rest of this unchanged 16-entry block */
      continue;
    }
    const uint16_t e = s_vram_shadow[(base + i) & 0x7fff];
    const uint16_t d = MapEntry(e, pal_add);
    if (dst_b) {  /* split by the SNES priority bit */
      const bool hi = e & 0x2000;
      dst_a[i] = hi ? d : blank;
      dst_b[i] = hi ? blank : d;
    } else {
      dst_a[i] = d;
    }
  }
}

static uint16_t MapSizeBits(int layer) {
  const Ppu *p = s_ppu;
  return (PPU_bgTilemapWider(p, layer) ? BIT(14) : 0) | (PPU_bgTilemapHigher(p, layer) ? BIT(15) : 0);
}

/* ---- sprites -------------------------------------------------------- */

static void UpdateSprites(unsigned crop_top, const uint8_t prio_map[4]) {
  static const uint8_t kSizes[8][2] = {
    {8, 16}, {8, 32}, {8, 64}, {16, 32}, {16, 64}, {32, 64}, {16, 32}, {16, 32},
  };
  const Ppu *p = s_ppu;
  const uint8_t *sizes = kSizes[PPU_objSize(p)];
  for (int i = 0; i < 128; i++) {
    const uint16_t lo = p->oam[i * 2], hi = p->oam[i * 2 + 1];
    const uint8_t ext = p->highOam[i >> 2] >> ((i & 3) * 2);
    const int size = sizes[(ext >> 1) & 1];
    const int x = (lo & 0xff) | ((ext & 1) << 8);
    const int y = ((lo >> 8) - (int)crop_top) & 0xff;
    const uint16_t t = (hi & 0xff) | ((hi & 0x100) ? 0x100 : 0);
    const uint8_t attr = hi >> 8;
    if (y >= kDsRows && y + size <= 256) {
      s_oam_stage[i * 4] = ATTR0_DISABLED;
      continue;
    }
    const uint16_t size_code = size == 8 ? 0 : size == 16 ? 1 : size == 32 ? 2 : 3;
    s_oam_stage[i * 4 + 0] = (uint16_t)(y | ATTR0_SQUARE | ATTR0_COLOR_16);
    s_oam_stage[i * 4 + 1] = (uint16_t)((x & 0x1ff) | ((attr & 0x40) ? ATTR1_FLIP_X : 0) |
                                   ((attr & 0x80) ? ATTR1_FLIP_Y : 0) | (size_code << 14));
    s_oam_stage[i * 4 + 2] = (uint16_t)((((t >> 4) << 5) | (t & 15)) |
                                   ATTR2_PRIORITY(prio_map[(attr >> 4) & 3]) |
                                   ATTR2_PALETTE((attr >> 1) & 7));
  }
}

/* ---- palettes and per-line tables ----------------------------------- */

static inline uint16_t AddColor(uint16_t a, uint16_t b) {
  uint16_t r = (a & 31) + (b & 31), g = ((a >> 5) & 31) + ((b >> 5) & 31),
           bl = ((a >> 10) & 31) + ((b >> 10) & 31);
  return (r > 31 ? 31 : r) | (g > 31 ? 31 : g) << 5 | (bl > 31 ? 31 : bl) << 10;
}

/* What the SNES shows where no layer is drawn: colour 0, plus the fixed
 * colour when colour math adds it there (the sky in most levels). */
static uint16_t Backdrop(const LineRegs *l) {
  const uint16_t c0 = s_ppu->cgram[0] & 0x7fff;
  if ((l->cgadsub & 0x20) && !(l->cgadsub & 0x80))
    return AddColor(c0, l->fixed);
  return c0;
}

static void UpdatePalettes(void) {
  const uint16_t *cg = s_ppu->cgram;
  for (int i = 1; i < 128; i++) BG_PALETTE[i] = cg[i];   /* BG1/BG2 palettes */
  for (int i = 0; i < 128; i++) SPRITE_PALETTE[i] = cg[128 + i];
  for (int pal = 0; pal < 8; pal++)                      /* BG3: 4 colours */
    for (int c = 0; c < 4; c++)
      BG_PALETTE[(8 + pal) * 16 + c] = cg[pal * 4 + c];
}

static void StartLineDma(int table) {
  DMA_CR(0) = 0;
  DMA_CR(1) = 0;
  /* Line 0 directly; DMA writes the following lines during each HBlank. */
  const uint32_t *s = (const uint32_t *)s_scroll[table][0];
  for (int i = 0; i < 4; i++) ((vu32 *)&REG_BG0HOFS)[i] = s[i];
  BG_PALETTE[0] = s_backdrop[table][0];
  DMA_SRC(0) = (uint32_t)s_scroll[table][1];
  DMA_DEST(0) = (uint32_t)&REG_BG0HOFS;
  DMA_CR(0) = DMA_ENABLE | DMA_REPEAT | DMA_START_HBL | DMA_DST_RESET | DMA_32_BIT | 4;
  DMA_SRC(1) = (uint32_t)&s_backdrop[table][1];
  DMA_DEST(1) = (uint32_t)BG_PALETTE;
  DMA_CR(1) = DMA_ENABLE | DMA_REPEAT | DMA_START_HBL | DMA_DST_RESET | DMA_16_BIT | 1;
}

static void StartWindowDma(int table, bool enable) {
  DMA_CR(2) = 0;
  if (!enable) return;
  *(vu32 *)0x04000040 = s_winpos[table][0];  /* WIN0H, WIN1H */
  DMA_SRC(2) = (uint32_t)&s_winpos[table][1];
  DMA_DEST(2) = 0x04000040;
  DMA_CR(2) = DMA_ENABLE | DMA_REPEAT | DMA_START_HBL | DMA_DST_RESET | DMA_32_BIT | 1;
}

/* SNES window span [left, right] (inclusive; empty if left > right) as a DS
 * WINxH value (x1 << 8 | x2, x2 exclusive; x2 = 0 means "to the edge"). */
static inline uint16_t DsWindowSpan(uint8_t left, uint8_t right) {
  if (left > right) return 0;
  if (right == 255) return left == 0 ? 255 : (uint16_t)(left << 8);
  return (uint16_t)(left << 8 | (right + 1));
}

/* SNES window logic for one layer (BG1-4 = 0-3, OBJ = 4, colour = 5) in one
 * DS window region. snesrev ORs the two windows, as SMW only needs that. */
static bool InSnesWindow(uint32_t windowsel, int layer, bool in_w1, bool in_w2) {
  const uint32_t f = windowsel >> (layer * 4);
  bool r = false;
  if (f & 2) r |= (f & 1) ? !in_w1 : in_w1;
  if (f & 8) r |= (f & 4) ? !in_w2 : in_w2;
  return r;
}

typedef struct DsWindowRegs {
  uint32_t dispcnt_bits;
  uint16_t winin, winout, bldcnt, bldy;
} DsWindowRegs;

/* Work out DS window/effect registers from the SNES ones. DS regions:
 * inside window 0 (= SNES W1), inside window 1 only (= W2), outside. */
static DsWindowRegs ComputeWindows(const LineRegs *l, bool bg2_sub_only) {
  DsWindowRegs r = {0, 0, 0, 0, 0};
  const uint32_t ws = l->windowsel;
  const bool uses_w1 = (ws & 0x222222) != 0, uses_w2 = (ws & 0x888888) != 0;
  if (!uses_w1 && !uses_w2) return r;

  /* Which SNES layers are window-masked on the screen they appear on. */
  uint8_t masked = l->main_win & l->main;
  if (bg2_sub_only) masked = (masked & ~2) | (l->sub_win & 2);
  static const uint8_t kDsBits[5] = {1 | 2, 4, 8, 0, 16};  /* BG1 -> BG0+BG1 */

  const unsigned cm = (l->cgwsel >> 6) & 3, pm = (l->cgwsel >> 4) & 3;
  const bool backdrop_math = (l->cgadsub & 0x20) && !(l->cgadsub & 0x80);
  bool any_clip = false, any_nomath = false;
  bool clip[3], nomath[3];
  uint8_t region_bits[3];
  static const bool kIn1[3] = {true, false, false}, kIn2[3] = {false, true, false};
  for (int rg = 0; rg < 3; rg++) {
    uint8_t bits = 0;
    for (int layer = 0; layer < 5; layer++) {
      const bool hidden = (masked & (1 << layer)) && InSnesWindow(ws, layer, kIn1[rg], kIn2[rg]);
      if (!hidden) bits |= kDsBits[layer];
    }
    region_bits[rg] = bits;
    const bool cw = InSnesWindow(ws, 5, kIn1[rg], kIn2[rg]);
    clip[rg] = cm == 3 || (cm == 1 && !cw) || (cm == 2 && cw);
    nomath[rg] = pm == 3 || (pm == 1 && !cw) || (pm == 2 && cw);
    any_clip |= clip[rg];
    any_nomath |= nomath[rg];
  }

  /* Colour effects: "clip to black" darkens everything; "no colour math"
   * where the sky colour comes from math leaves the plain backdrop, which
   * in SMW is black (the message box). */
  bool *effect = NULL;
  if (any_clip) {
    effect = clip;
    r.bldcnt = BLEND_FADE_BLACK | BLEND_SRC_BG0 | BLEND_SRC_BG1 | BLEND_SRC_BG2 |
               BLEND_SRC_BG3 | BLEND_SRC_SPRITE | BLEND_SRC_BACKDROP;
  } else if (any_nomath && backdrop_math && (s_ppu->cgram[0] & 0x7fff) == 0) {
    effect = nomath;
    r.bldcnt = BLEND_FADE_BLACK | BLEND_SRC_BACKDROP;
  }
  r.bldy = 16;
  const uint8_t e0 = effect && effect[0] ? 0x20 : 0, e1 = effect && effect[1] ? 0x20 : 0,
                e2 = effect && effect[2] ? 0x20 : 0;
  r.winin = (uint16_t)((region_bits[0] | e0) | (region_bits[1] | e1) << 8);
  r.winout = (uint16_t)(region_bits[2] | e2);
  r.dispcnt_bits = (uses_w1 ? DISPLAY_WIN0_ON : 0) | (uses_w2 ? DISPLAY_WIN1_ON : 0);
  return r;
}

/* ---- per frame ------------------------------------------------------ */

static const uint8_t kSpritePrio[4] = {2, 2, 1, 0};

/* Before VBlank: record the frame and prepare everything that does not
 * touch video memory, so the VBlank part stays short. */
void NdsRenderCapture(unsigned crop_top) {
  PortRunPpuLines(&CaptureLine);
  s_crop = crop_top;
  FindChanges();
  UpdateSprites(crop_top, kSpritePrio);

  /* Per-line scroll and backdrop for the 192 shown lines. */
  const int t = s_table ^ 1;
  const uint16_t v_add = (uint16_t)(crop_top + 1);
  for (int y = 0; y <= kDsRows; y++) {
    const LineRegs *l = &s_lines[crop_top + (y < kDsRows ? y : kDsRows - 1)];
    uint16_t *sc = s_scroll[t][y];
    sc[0] = sc[2] = l->hs[0];
    sc[1] = sc[3] = l->vs[0] + v_add;
    sc[4] = l->hs[1];
    sc[5] = l->vs[1] + v_add;
    sc[6] = l->hs[2];
    sc[7] = l->vs[2] + v_add;
    s_backdrop[t][y] = Backdrop(l);
    s_winpos[t][y] = DsWindowSpan(l->w1l, l->w1r) | (uint32_t)DsWindowSpan(l->w2l, l->w2r) << 16;
  }
  DC_FlushRange(s_winpos[t], sizeof(s_winpos[t]));
  DC_FlushRange(s_scroll[t], sizeof(s_scroll[t]));
  DC_FlushRange(s_backdrop[t], sizeof(s_backdrop[t]));
  s_ready_table = t;
}

/* During VBlank: registers and small tables first (they must not change
 * mid-picture), then the changed tiles and map entries. */
void NdsRenderPresent(void) {
  const Ppu *p = s_ppu;
  const unsigned crop_top = s_crop;
  const LineRegs *mid = &s_lines[crop_top + kDsRows / 2];
  if (s_ready_table < 0) return;
  s_table = s_ready_table;
  StartLineDma(s_table);

  /* Layer order (see header). BG2 drawn only on the SNES sub screen sits
   * under everything on the main screen. */
  const bool bg2_sub_only = !(mid->main & 2) && (mid->sub & 2);
  const bool bg3_top = (mid->bgmode & 8) != 0;
  const uint8_t prio_bg3 = bg3_top ? 0 : 2;
  const uint8_t prio_bg2 = bg2_sub_only ? 3 : 1;

  const uint16_t mosaic_on = BIT(6);
  REG_BG0CNT = BG_PRIORITY(0) | BG_TILE_BASE(0) | BG_MAP_BASE(0) | BG_COLOR_16 | MapSizeBits(0) |
               ((mid->mosaic & 1) ? mosaic_on : 0);
  REG_BG1CNT = BG_PRIORITY(1) | BG_TILE_BASE(0) | BG_MAP_BASE(4) | BG_COLOR_16 | MapSizeBits(0) |
               ((mid->mosaic & 1) ? mosaic_on : 0);
  REG_BG2CNT = BG_PRIORITY(prio_bg2) | BG_TILE_BASE(PPU_bgTileAdr(p, 1) * 2 / 0x4000) |
               BG_MAP_BASE(8) | BG_COLOR_16 | MapSizeBits(1) | ((mid->mosaic & 2) ? mosaic_on : 0);
  REG_BG3CNT = BG_PRIORITY(prio_bg3) | BG_TILE_BASE(8) | BG_MAP_BASE(12) | BG_COLOR_16 |
               MapSizeBits(2) | ((mid->mosaic & 4) ? mosaic_on : 0);
  const unsigned m = (mid->mosaic >> 4) & 15;
  REG_MOSAIC = m | (m << 4);

  const uint8_t shown = mid->main | mid->sub;
  uint32_t dispcnt = MODE_0_2D | DISPLAY_SCREEN_BASE(1) | DISPLAY_SPR_2D;
  if (shown & 1) dispcnt |= DISPLAY_BG0_ACTIVE | DISPLAY_BG1_ACTIVE;
  if (shown & 2) dispcnt |= DISPLAY_BG2_ACTIVE;
  if (shown & 4) dispcnt |= DISPLAY_BG3_ACTIVE;
  if (shown & 16) dispcnt |= DISPLAY_SPR_ACTIVE;
  if (PPU_mode(p) != 1)
    dispcnt &= ~(DISPLAY_BG0_ACTIVE | DISPLAY_BG1_ACTIVE | DISPLAY_BG2_ACTIVE | DISPLAY_BG3_ACTIVE);

  const DsWindowRegs win = ComputeWindows(mid, bg2_sub_only);
  dispcnt |= win.dispcnt_bits;
  *(vu16 *)0x04000044 = kDsRows;  /* WIN0V: lines 0..191 */
  *(vu16 *)0x04000046 = kDsRows;  /* WIN1V */
  WIN_IN = win.winin;
  WIN_OUT = win.winout;
  REG_BLDCNT = win.bldcnt;
  REG_BLDY = win.bldy;
  StartWindowDma(s_table, win.dispcnt_bits != 0);
  REG_DISPCNT = dispcnt;

  /* Brightness fades and forced blank. */
  const unsigned b = mid->inidisp & 15;
  if (mid->inidisp & 0x80 || b == 0)
    REG_MASTER_BRIGHT = (2 << 14) | 16;
  else
    REG_MASTER_BRIGHT = b == 15 ? 0 : (2 << 14) | (((15 - b) * 16 + 7) / 15);

  memcpy((void *)DS_OAM, s_oam_stage, sizeof(s_oam_stage));
  UpdatePalettes();

  /* Changed tiles, then the tile maps (only their changed blocks). */
  for (uint32_t word = 0; word < 2048 / 32; word++) {
    uint32_t bits = s_dirty[word];
    while (bits) {
      const int bit = __builtin_ctz(bits);
      bits &= bits - 1;
      ConvertBlock(word * 32 + bit);
    }
  }
  ConvertMap(0, DS_BG_MAPS + 0x0000, DS_BG_MAPS + 0x1000, 0);
  ConvertMap(1, DS_BG_MAPS + 0x2000, NULL, 0);
  ConvertMap(2, DS_BG_MAPS + 0x3000, NULL, 8);
  s_maps_full_refresh = false;
  memset(s_dirty, 0, sizeof(s_dirty));
}
