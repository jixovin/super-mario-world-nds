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

#include "nds_hud.h"
#include "smw_port.h"
#include "snes/ppu.h"

enum {
  kDsRows = 192,
  kSnesRows = 224,
  kBlankTile = 1023,           /* index forced transparent for BG1 splits */
  kMaxChunksPerVBlank = 2500,  /* 32-byte chunks safely copied in VBlank (~80 KB) */
};

/* Video memory regions, and copies of them in main RAM. Conversion writes
 * the RAM copies before VBlank (when there is CPU time); during VBlank only
 * the changed 32-byte chunks are copied to video memory. */
#define VRAM_BG_TILES   ((uint32_t *)0x06000000)  /* 64 KB */
#define VRAM_BG_MAPS    ((uint32_t *)0x06010000)  /* 32 KB */
#define VRAM_BG3_TILES  ((uint32_t *)0x06020000)  /* 32 KB */
#define VRAM_OBJ_TILES  ((uint32_t *)0x06400000)  /* 32 KB */
#define VRAM_M7_MAP     ((uint32_t *)0x06018000)  /* 16 KB, map base 16 */
#define VRAM_M7_TILES   ((uint32_t *)0x06028000)  /* 16 KB, tile base 10 */
enum { kRegBg, kRegMaps, kRegBg3, kRegObj, kRegM7Map, kRegM7Tiles, kRegions };
static const uint32_t kRegionChunks[kRegions] = {2048, 1024, 1024, 1024, 512, 512};
static uint32_t s_mir_bg[16384], s_mir_maps[8192], s_mir_bg3[8192], s_mir_obj[8192];
static uint32_t s_mir_m7map[4096], s_mir_m7tiles[4096];
static uint32_t s_upload[kRegions][2048 / 32];  /* chunks to copy at VBlank */

/* Mode 7 (some boss rooms): SNES VRAM words 0-0x3fff hold a 128x128 map
 * (low bytes) and 256 8bpp tiles (high bytes), the same format as a DS
 * rotation background, so they are just de-interleaved into
 * VRAM_M7_MAP/VRAM_M7_TILES. Only done while mode 7 is in use. */
static bool s_m7_active;
#define DS_BG_TILES   s_mir_bg
#define DS_BG_MAPS    ((uint16_t *)s_mir_maps)
#define DS_BG3_TILES  s_mir_bg3
#define DS_OBJ_TILES  s_mir_obj

static inline void MarkUpload(int region, uint32_t chunk) {
  s_upload[region][chunk >> 5] |= 1u << (chunk & 31);
}
#define DS_OAM        ((uint16_t *)0x07000000)

/* Per-line PPU state captured while the game's frame runs. */
typedef struct LineRegs {
  uint16_t hs[4], vs[4];
  uint16_t fixed;
  uint8_t inidisp, bgmode, main, sub, cgwsel, cgadsub, mosaic;
  uint8_t main_win, sub_win, w1l, w1r, w2l, w2r;
  uint32_t windowsel;
  int16_t m7[8];   /* mode 7: matrix a, b, c, d, centre x, y, scroll h, v */
  uint8_t m7sel;
} LineRegs;

static LineRegs s_lines[kSnesRows];
static Ppu *s_ppu;

/* Per-line tables read by HBlank DMA; one is displayed while the other is
 * filled. Entry 0 is written directly at VBlank, entries 1..192 by DMA. */
/* Per line: BG0-3 H/V offsets (8 halfwords, written to 0x04000010), and in
 * mode 7 frames also the BG2 and BG3 rotation registers (0x04000020-3F), so
 * every line gets exactly the SNES mode 7 start position. */
enum { kLineHalfwords = 24 };
static uint16_t s_scroll[2][(kDsRows + 1) * kLineHalfwords] __attribute__((aligned(32)));
static uint8_t s_line_words[2];  /* 32-bit words per line DMA'd: 4, or 12 for mode 7 */
/* Line y of table t (lines are packed: the DMA reads them back to back). */
static inline uint16_t *ScrollRow(int t, int y) { return &s_scroll[t][y * s_line_words[t] * 2]; }
static uint16_t s_backdrop[2][kDsRows + 1] __attribute__((aligned(32)));
static uint32_t s_winpos[2][kDsRows + 1] __attribute__((aligned(32)));
/* Mode 7 frames only: DISPCNT per line (DMA3), since SNES mode 7 rooms can
 * switch between mode 7 and mode 1 part-way down the picture (Reznor). */
static uint32_t s_dispcnt[2][kDsRows + 1] __attribute__((aligned(32)));
static uint8_t s_rowline[2][kDsRows + 1];  /* SNES line shown on each DS line */
static bool s_rowtext[2][kDsRows + 1];     /* mode 7 frames: a mode 1 line */
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

static void VBlankStartIrq(void);

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
  irqSet(IRQ_VCOUNT, VBlankStartIrq);
  SetYtrigger(kDsRows);
  REG_DISPSTAT |= DISP_YTRIGGER_IRQ;
  irqEnable(IRQ_VCOUNT);
}

#ifdef SMW_NDS_TEST
/* Test builds: tools/dstest can have snesrev's software renderer draw the
 * same frame into g_test_ref (XRGB, 256x240) to compare with the DS. */
volatile uint8_t g_test_ref_on, g_test_ref_crop;
uint32_t g_test_ref[256 * 240];
#endif

static void CaptureLine(int line) {
#ifdef SMW_NDS_TEST
  if (g_test_ref_on) ppu_runLine(s_ppu, line);
#endif
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
  if ((p->bgmode & 7) == 7) {
    memcpy(l->m7, p->m7matrix, sizeof(l->m7));
    l->m7sel = p->m7sel;
  }
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
    s_maps_full_refresh = true;
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
  if (bg12_base % 0x2000 == 0 && blk == ((bg12_base / 16 + kBlankTile) & 2047))
    memset(&DS_BG_TILES[blk * 8], 0, 32);
  else
    Convert4bpp(&vram[w], &DS_BG_TILES[blk * 8]);
  MarkUpload(kRegBg, blk);

  /* 2bpp view, for BG3: two 8-word tiles per block. */
  const uint32_t rel3 = (w - bg3_base) & 0x7fff;
  if (rel3 < 1024 * 8) {
    const uint32_t i = rel3 / 8;
    Convert2bpp(&vram[w], &DS_BG3_TILES[i * 8]);
    Convert2bpp(&vram[w + 8], &DS_BG3_TILES[(i + 1) * 8]);
    MarkUpload(kRegBg3, i);
    MarkUpload(kRegBg3, i + 1);
  }

  /* Mode 7 view: 16 map bytes and 16 tile bytes per block. */
  if (s_m7_active && blk < 1024) {
    uint8_t *map = (uint8_t *)s_mir_m7map + w, *tiles = (uint8_t *)s_mir_m7tiles + w;
    for (int i = 0; i < 16; i++) {
      map[i] = (uint8_t)vram[w + i];
      tiles[i] = (uint8_t)(vram[w + i] >> 8);
    }
    MarkUpload(kRegM7Map, blk >> 1);
    MarkUpload(kRegM7Tiles, blk >> 1);
  }

  /* Sprite tiles: SNES grid is 16 tiles wide, DS 2D grid is 32 wide. */
  const uint32_t rel1 = (w - obj1) & 0x7fff, rel2 = (w - obj2) & 0x7fff;
  uint32_t t = 0xffff;
  if (rel1 < 256 * 16) t = rel1 / 16;
  else if (rel2 < 256 * 16) t = 256 + rel2 / 16;
  if (t != 0xffff) {
    const uint32_t ds = ((t >> 4) << 5) | (t & 15);
    Convert4bpp(&vram[w], &DS_OBJ_TILES[ds * 8]);
    MarkUpload(kRegObj, ds);
  }
}

/* ---- tile maps ------------------------------------------------------ */

static inline uint16_t MapEntry(uint16_t e, uint16_t pal_add, uint16_t tile_add) {
  return (((e & 0x3ff) + tile_add) & 0x3ff) | ((e >> 4) & 0xc00) | ((((e >> 10) & 7) + pal_add) << 12);
}

/* SNES BG1 tiles normally start at a 16 KB boundary of video memory, like
 * DS tile bases. In the mode 7 rooms they start 8 KB past one (at 56 KB);
 * then DS BG0 uses the 48 KB base with tile numbers shifted by 256, and
 * BG1 is not split by priority (that needs a blank tile). */
static uint16_t s_bg1_tile_add;
static uint8_t s_bg1_tile_base;

/* SNES and DS order 32x32 map screens the same way for every map size. */
static void ConvertMap(int layer, uint16_t *dst_a, uint16_t *dst_b, uint16_t pal_add,
                       uint16_t tile_add) {
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
    const uint16_t d = MapEntry(e, pal_add, tile_add);
    if (dst_b) {  /* split by the SNES priority bit */
      const bool hi = e & 0x2000;
      dst_a[i] = hi ? d : blank;
      dst_b[i] = hi ? blank : d;
      MarkUpload(kRegMaps, (uint32_t)(&dst_b[i] - DS_BG_MAPS) / 16);
    } else {
      dst_a[i] = d;
    }
    MarkUpload(kRegMaps, (uint32_t)(&dst_a[i] - DS_BG_MAPS) / 16);
  }
}

static uint16_t MapSizeBits(int layer) {
  const Ppu *p = s_ppu;
  return (PPU_bgTilemapWider(p, layer) ? BIT(14) : 0) | (PPU_bgTilemapHigher(p, layer) ? BIT(15) : 0);
}

/* ---- sprites -------------------------------------------------------- */

static void UpdateSprites(unsigned crop_top, bool level, const uint8_t prio_map[4], bool obj_math) {
  static const uint8_t kSizes[8][2] = {
    {8, 16}, {8, 32}, {8, 64}, {16, 32}, {16, 64}, {32, 64}, {16, 32}, {16, 32},
  };
  const Ppu *p = s_ppu;
  const uint8_t *sizes = kSizes[PPU_objSize(p)];
  /* SNES OAM priority rotation (used on the map): sprite `first` has the
   * highest priority. DS priority is by OAM slot, so rotate the slots. */
  const int first = PPU_objPriority(p) ? (p->oamaddl & 0xfe) >> 1 : 0;
  for (int slot = 0; slot < 128; slot++) {
    const int i = (first + slot) & 127;
    const uint16_t lo = p->oam[i * 2], hi = p->oam[i * 2 + 1];
    const uint8_t ext = p->highOam[i >> 2] >> ((i & 3) * 2);
    const int size = sizes[(ext >> 1) & 1];
    const int x = (lo & 0xff) | ((ext & 1) << 8);
    const int y = ((lo >> 8) - (int)crop_top) & 0xff;
    const uint16_t t = (hi & 0xff) | ((hi & 0x100) ? 0x100 : 0);
    const uint8_t attr = hi >> 8;
    /* In levels the reserve item (x=120, y=15) belongs to the status bar,
     * which is on the bottom screen. */
    if ((y >= kDsRows && y + size <= 256) || (level && (lo & 0xff) == 120 && (lo >> 8) == 15)) {
      s_oam_stage[slot * 4] = ATTR0_DISABLED;
      continue;
    }
    const uint16_t size_code = size == 8 ? 0 : size == 16 ? 1 : size == 32 ? 2 : 3;
    /* Colour math on sprites only affects palettes 4-7 on the SNES. */
    const uint16_t blend = obj_math && (attr & 8) ? ATTR0_TYPE_BLENDED : 0;
    s_oam_stage[slot * 4 + 0] = (uint16_t)(y | ATTR0_SQUARE | ATTR0_COLOR_16 | blend);
    s_oam_stage[slot * 4 + 1] = (uint16_t)((x & 0x1ff) | ((attr & 0x40) ? ATTR1_FLIP_X : 0) |
                                   ((attr & 0x80) ? ATTR1_FLIP_Y : 0) | (size_code << 14));
    s_oam_stage[slot * 4 + 2] = (uint16_t)((((t >> 4) << 5) | (t & 15)) |
                                   ATTR2_PRIORITY(prio_map[(attr >> 4) & 3]) |
                                   ATTR2_PALETTE((attr >> 1) & 7));
  }
}

/* ---- palettes and per-line tables ----------------------------------- */

/* How the SNES main and sub screens and colour math map onto DS layer
 * priorities and blending, for the frame being prepared. */
typedef struct Layout {
  uint8_t prio[4];          /* DS BG0-BG3 priorities */
  const uint8_t *obj_prio;  /* SNES sprite priority 0-3 -> DS priority */
  bool obj_math;            /* sprites with palettes 4-7 blend with the sub screen */
  uint16_t bldcnt, bldalpha, bldy;
  uint8_t pal_sub;          /* subtract the fixed colour in the palettes: layer mask */
  uint16_t sub_fixed;
  bool sub_half;
} Layout;
static Layout s_layout;

/* 0: covers nothing, 1: covers the whole line, 2: part of it. */
static int WindowCover(uint8_t left, uint8_t right, bool invert) {
  const int c = left > right ? 0 : (left == 0 && right == 255) ? 1 : 2;
  return c == 2 ? 2 : (invert ? 1 - c : c);
}

/* True if colour math applies to the whole of every shown line (the colour
 * window doesn't cut out any part). */
static bool MathEverywhere(unsigned crop_top) {
  for (int y = 0; y < kDsRows; y++) {
    const LineRegs *l = &s_lines[crop_top + y];
    const unsigned pm = (l->cgwsel >> 4) & 3;
    if (pm == 0) continue;
    if (pm == 3) return false;
    const unsigned f = (l->windowsel >> 20) & 15;
    int cover = 0;  /* colour window: OR of the enabled windows */
    if (f & 2) cover = WindowCover(l->w1l, l->w1r, f & 1);
    if (f & 8) {
      const int c2 = WindowCover(l->w2l, l->w2r, f & 4);
      cover = (cover == 1 || c2 == 1) ? 1 : (cover == 2 || c2 == 2) ? 2 : 0;
    }
    /* pm 1: no math outside the window; pm 2: none inside it */
    if ((pm == 1 && cover != 1) || (pm == 2 && cover != 0)) return false;
  }
  return true;
}

static inline uint16_t SubColor(uint16_t c, uint16_t f, bool half) {
  int r = (int)(c & 31) - (f & 31), g = (int)((c >> 5) & 31) - ((f >> 5) & 31),
      b = (int)((c >> 10) & 31) - ((f >> 10) & 31);
  r = r < 0 ? 0 : r; g = g < 0 ? 0 : g; b = b < 0 ? 0 : b;
  if (half) { r >>= 1; g >>= 1; b >>= 1; }
  return (uint16_t)(r | g << 5 | b << 10);
}

static inline uint16_t AddColor(uint16_t a, uint16_t b) {
  uint16_t r = (a & 31) + (b & 31), g = ((a >> 5) & 31) + ((b >> 5) & 31),
           bl = ((a >> 10) & 31) + ((b >> 10) & 31);
  return (r > 31 ? 31 : r) | (g > 31 ? 31 : g) << 5 | (bl > 31 ? 31 : bl) << 10;
}

/* What the SNES shows where no layer is drawn: colour 0, plus the fixed
 * colour when colour math adds it there (the sky in most levels). */
static uint16_t Backdrop(const LineRegs *l) {
  const uint16_t c0 = s_ppu->cgram[0] & 0x7fff;
  if (s_layout.pal_sub & 0x20) return SubColor(c0, s_layout.sub_fixed, s_layout.sub_half);
  if ((l->cgadsub & 0x20) && !(l->cgadsub & 0x80))
    return AddColor(c0, l->fixed);
  return c0;
}

static void UpdatePalettes(bool m7) {
  const uint16_t *cg = s_ppu->cgram;
  if (m7) {  /* the mode 7 layer uses all 256 colours */
    for (int i = 1; i < 256; i++) BG_PALETTE[i] = cg[i];
    for (int i = 0; i < 128; i++) SPRITE_PALETTE[i] = cg[128 + i];
    return;
  }
  const Layout *lay = &s_layout;
  if (lay->pal_sub) {  /* dark rooms: fixed colour subtracted from the layers with math */
    const uint16_t f = lay->sub_fixed;
    const bool h = lay->sub_half;
    for (int i = 1; i < 128; i++)
      BG_PALETTE[i] = (lay->pal_sub & 3) ? SubColor(cg[i], f, h) : cg[i];
    for (int i = 0; i < 128; i++)
      SPRITE_PALETTE[i] = (i >= 64 && (lay->pal_sub & 16)) ? SubColor(cg[128 + i], f, h) : cg[128 + i];
    for (int pal = 0; pal < 8; pal++)
      for (int c = 0; c < 4; c++)
        BG_PALETTE[(8 + pal) * 16 + c] =
            (lay->pal_sub & 4) ? SubColor(cg[pal * 4 + c], f, h) : cg[pal * 4 + c];
    return;
  }
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
  const uint32_t *s = (const uint32_t *)ScrollRow(table, 0);
  const unsigned words = s_line_words[table];
  for (unsigned i = 0; i < words; i++) ((vu32 *)&REG_BG0HOFS)[i] = s[i];
  BG_PALETTE[0] = s_backdrop[table][0];
  DMA_SRC(0) = (uint32_t)ScrollRow(table, 1);
  DMA_DEST(0) = (uint32_t)&REG_BG0HOFS;
  DMA_CR(0) = DMA_ENABLE | DMA_REPEAT | DMA_START_HBL | DMA_DST_RESET | DMA_32_BIT | words;
  DMA_SRC(1) = (uint32_t)&s_backdrop[table][1];
  DMA_DEST(1) = (uint32_t)BG_PALETTE;
  DMA_CR(1) = DMA_ENABLE | DMA_REPEAT | DMA_START_HBL | DMA_DST_RESET | DMA_16_BIT | 1;
}

static void StartWindowDma(int table, bool enable);
static void StartDispcntDma(int table, bool enable);

/* The table shown at the next VBlank. The per-line DMA has to be restarted
 * at every VBlank, otherwise it runs past the end of its table and fills
 * the picture with garbage scroll values (grey "static" lines). The main
 * loop restarts it in NdsRenderPresent, but when a frame takes too long
 * that is late, so an interrupt at the start of VBlank (line 192) restarts
 * it from the last finished table: a slow frame just shows the previous
 * picture again. */
static volatile int s_shown_table = -1;
static volatile bool s_shown_windows, s_shown_dispcnt;

static void VBlankStartIrq(void) {
  const int t = s_shown_table;
  if (t < 0) return;
  StartLineDma(t);
  StartWindowDma(t, s_shown_windows);
  StartDispcntDma(t, s_shown_dispcnt);
}

static void StartDispcntDma(int table, bool enable) {
  DMA_CR(3) = 0;
  if (!enable) return;
  REG_DISPCNT = s_dispcnt[table][0];
  DMA_SRC(3) = (uint32_t)&s_dispcnt[table][1];
  DMA_DEST(3) = (uint32_t)&REG_DISPCNT;
  DMA_CR(3) = DMA_ENABLE | DMA_REPEAT | DMA_START_HBL | DMA_DST_RESET | DMA_32_BIT | 1;
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

/* Subtracting the fixed colour (dark rooms): approximated by fading the
 * affected layers toward black, matched at full brightness:
 * (31 - f) (/2 when halving) == 31 * (16 - bldy) / 16. Returns 0 when the
 * line doesn't subtract the fixed colour. */
static uint16_t SubtractFade(const LineRegs *l, uint16_t *bldy) {
  if (!(l->cgadsub & 0x80) || (l->cgwsel & 2) || !(l->cgadsub & 0x3f)) return 0;
  const unsigned f = ((l->fixed & 31) + ((l->fixed >> 5) & 31) + ((l->fixed >> 10) & 31)) / 3;
  unsigned left = 31 - (f > 31 ? 31 : f);
  if (l->cgadsub & 0x40) left /= 2;
  *bldy = (uint16_t)(16 - (left * 16 + 15) / 31);
  const uint8_t m = l->cgadsub;
  return (uint16_t)(BLEND_FADE_BLACK | ((m & 1) ? BLEND_SRC_BG0 | BLEND_SRC_BG1 : 0) |
                    ((m & 2) ? BLEND_SRC_BG2 : 0) | ((m & 4) ? BLEND_SRC_BG3 : 0) |
                    ((m & 16) ? BLEND_SRC_SPRITE : 0) | ((m & 32) ? BLEND_SRC_BACKDROP : 0));
}

typedef struct DsWindowRegs {
  uint32_t dispcnt_bits;
  uint16_t winin, winout, bldcnt, bldy;
} DsWindowRegs;

/* Work out DS window/effect registers from the SNES ones. DS regions:
 * inside window 0 (= SNES W1), inside window 1 only (= W2), outside. */
static DsWindowRegs ComputeWindows(const LineRegs *l, bool bg2_sub_only, bool m7) {
  DsWindowRegs r = {0, 0, 0, 0, 0};
  const uint32_t ws = l->windowsel;
  const bool uses_w1 = (ws & 0x222222) != 0, uses_w2 = (ws & 0x888888) != 0;
  if (!uses_w1 && !uses_w2) return r;

  /* Which SNES layers are window-masked on the screen they appear on. */
  uint8_t masked = l->main_win & l->main;
  if (bg2_sub_only) masked = (masked & ~2) | (l->sub_win & 2);
  static const uint8_t kDsBitsM1[5] = {1 | 2, 4, 8, 0, 16};  /* BG1 -> BG0+BG1 */
  static const uint8_t kDsBitsM7[5] = {8 | 1 | 2, 4, 0, 0, 16}; /* mode 7 BG1 -> BG3 (+ text) */
  const uint8_t *kDsBits = m7 ? kDsBitsM7 : kDsBitsM1;

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
  uint16_t sub_y;
  const uint16_t sub_fade = s_layout.pal_sub ? 0 : SubtractFade(l, &sub_y);
  bool math_on[3];
  for (int rg = 0; rg < 3; rg++) math_on[rg] = !nomath[rg];
  if (sub_fade) {  /* dark room: darken where colour math applies */
    effect = math_on;
    r.bldcnt = sub_fade;
  } else if (any_clip) {
    effect = clip;
    r.bldcnt = BLEND_FADE_BLACK | BLEND_SRC_BG0 | BLEND_SRC_BG1 | BLEND_SRC_BG2 |
               BLEND_SRC_BG3 | BLEND_SRC_SPRITE | BLEND_SRC_BACKDROP;
  } else if (any_nomath && backdrop_math && (s_ppu->cgram[0] & 0x7fff) == 0) {
    effect = nomath;
    r.bldcnt = BLEND_FADE_BLACK | BLEND_SRC_BACKDROP;
  }
  r.bldy = sub_fade ? sub_y : 16;
  const uint8_t e0 = effect && effect[0] ? 0x20 : 0, e1 = effect && effect[1] ? 0x20 : 0,
                e2 = effect && effect[2] ? 0x20 : 0;
  r.winin = (uint16_t)((region_bits[0] | e0) | (region_bits[1] | e1) << 8);
  r.winout = (uint16_t)(region_bits[2] | e2);
  r.dispcnt_bits = (uses_w1 ? DISPLAY_WIN0_ON : 0) | (uses_w2 ? DISPLAY_WIN1_ON : 0);
  return r;
}

/* ---- per frame ------------------------------------------------------ */


/* Before VBlank: record the frame and prepare everything that does not
 * touch video memory, so the VBlank part stays short. */
/* Mode 7 settings for the frame being prepared (see PrepareMode7). */
typedef struct Mode7Frame {
  bool on;
  bool wrap;
} Mode7Frame;
static Mode7Frame s_m7;
static const LineRegs *s_m7_line;  /* first mode 7 line of the frame */

/* The SNES mode 7 start position of one line (snesrev's renderer, exactly,
 * including its rounding), as DS BG3 rotation registers at sc[16..23]. */
static void Mode7Row(const LineRegs *l, int line1, uint16_t *sc) {
  const int a = l->m7[0], b = l->m7[1], c = l->m7[2], d = l->m7[3];
  const int cx = (int16_t)(l->m7[4] << 3) >> 3, cy = (int16_t)(l->m7[5] << 3) >> 3;
  const int h = (int16_t)(l->m7[6] << 3) >> 3, v = (int16_t)(l->m7[7] << 3) >> 3;
  int ch = h - cx, cv = v - cy;
  ch = (ch & 0x2000) ? (ch | ~1023) : (ch & 1023);
  cv = (cv & 0x2000) ? (cv | ~1023) : (cv & 1023);
  const bool xflip = l->m7sel & 1, yflip = l->m7sel & 2;
  const int ry = yflip ? 255 - line1 : line1, rx0 = xflip ? 255 : 0;
  const int32_t x = (a * ch & ~63) + (b * ry & ~63) + (b * cv & ~63) + (cx << 8) + a * rx0;
  const int32_t y = (c * ch & ~63) + (d * ry & ~63) + (d * cv & ~63) + (cy << 8) + c * rx0;
  memset(sc + 8, 0, 8 * sizeof(uint16_t));
  sc[16] = (uint16_t)(xflip ? -a : a);
  sc[17] = (uint16_t)b;
  sc[18] = (uint16_t)(xflip ? -c : c);
  sc[19] = (uint16_t)d;
  sc[20] = (uint16_t)x;
  sc[21] = (uint16_t)((uint32_t)x >> 16);
  sc[22] = (uint16_t)y;
  sc[23] = (uint16_t)((uint32_t)y >> 16);
}

/* If any line uses mode 7, the DS picture uses DS mode 1 with BG3 as the
 * mode 7 layer; its start position is set for every line (Mode7Row). Mode 1
 * lines in the same picture (e.g. under Reznor) use BG0-BG2 as usual. */
static void PrepareMode7(void) {
  static int idle_frames;
  const LineRegs *l = NULL;
  for (int r = 0; r < kSnesRows; r++) {
    if ((s_lines[r].bgmode & 7) == 7) { l = &s_lines[r]; break; }
  }
  s_m7.on = false;
  if (!l) {
    if (s_m7_active && ++idle_frames > 60) s_m7_active = false;
    return;
  }
  idle_frames = 0;
  if (!s_m7_active) {  /* convert all of the mode 7 area once */
    s_m7_active = true;
    for (int w = 0; w < 1024 / 32; w++) s_dirty[w] = ~0u;
  }
  s_m7.on = true;
  s_m7_line = l;
  s_m7.wrap = !(l->m7sel & 0x80);
}

static Layout ComputeLayout(const LineRegs *l, unsigned crop_top) {
  /* Normal order: BG1 high, BG1 low, BG2, BG3 (BG3 on top with the mode 1
   * priority bit, used for the status bar and some layer 3 levels). */
  static const uint8_t kObjNormal[4] = {2, 2, 1, 0};
  /* Mode 7: SNES sprite priority 0 is behind the mode 7 layer (BG3 at 2). */
  static const uint8_t kObjM7[4] = {3, 2, 1, 0};
  /* BG3 alone on the main screen above BG1/BG2/sprites on the sub screen
   * (layer 3 in front, e.g. ghost house fog): BG3 on top of everything. */
  static const uint8_t kObjUnderBg3[4] = {3, 2, 2, 1};
  Layout r;
  const uint8_t main = l->main, sub = l->sub;
  if (s_m7.on) {
    r = (Layout){{0, 1, 1, 2}, kObjM7, false, 0, 0};
  } else if ((main & 4) && !(main & 3) && (sub & 3)) {
    r = (Layout){{1, 2, 3, 0}, kObjUnderBg3, false, 0, 0};
  } else {
    const bool bg2_sub_only = !(main & 2) && (sub & 2);
    r = (Layout){{0, 1, (uint8_t)(bg2_sub_only ? 3 : 1), (uint8_t)((l->bgmode & 8) ? 0 : 2)},
                 kObjNormal, false, 0, 0};
  }
  /* Colour math (add, or add-and-halve; SMW never subtracts in play):
   * main-screen layers with math on blend with what is under them. */
  const uint8_t math = l->cgadsub;
  r.bldcnt = SubtractFade(l, &r.bldy);  /* windows may refine this (ComputeWindows) */
  if (r.bldcnt && !s_m7.on && MathEverywhere(crop_top)) {
    /* No window cuts it out: subtract exactly, in the palettes. */
    r.bldcnt = 0;
    r.bldy = 0;
    r.pal_sub = l->cgadsub & 0x37;
    r.sub_fixed = l->fixed;
    r.sub_half = (l->cgadsub & 0x40) != 0;
  }
  if (!(math & 0x80) && !s_m7.on) {
    uint16_t dst = BLEND_DST_BACKDROP;
    if (sub & 1) dst |= BLEND_DST_BG0 | BLEND_DST_BG1;
    if (sub & 2) dst |= BLEND_DST_BG2;
    if (sub & 4) dst |= BLEND_DST_BG3;
    if (sub & 16) dst |= BLEND_DST_SPRITE;
    uint16_t src = 0;
    if ((math & 1) && (main & 1)) src |= BLEND_SRC_BG0 | BLEND_SRC_BG1;
    if ((math & 2) && (main & 2)) src |= BLEND_SRC_BG2;
    if ((math & 4) && (main & 4)) src |= BLEND_SRC_BG3;
    r.obj_math = (math & 0x10) && (main & 0x10) && (sub & 0x17);
    if (r.obj_math) {
      /* The SNES mixes these sprites with the sub screen even under main
       * screen layers; the DS can only mix with the layer just below, so
       * let them mix with whatever is there. */
      dst |= BLEND_DST_BG0 | BLEND_DST_BG1 | BLEND_DST_BG2 | BLEND_DST_BG3;
    }
    if (src || r.obj_math) {
      r.bldcnt = (src ? (BLEND_ALPHA | src) : 0) | dst;
      r.bldalpha = (math & 0x40) ? (8 | 8 << 8) : (16 | 16 << 8);
    }
    /* Backdrop math with halving (inside ghost houses): where the main
     * screen is empty the SNES shows (colour 0 + sub screen) / 2, i.e. the
     * sub-screen-only layers at half brightness (colour 0 is black). */
    const uint8_t sub_only = sub & ~main & 7;
    if (!src && (math & 0x60) == 0x60 && sub_only && (s_ppu->cgram[0] & 0x7fff) == 0) {
      r.bldcnt = (uint16_t)(BLEND_FADE_BLACK | (dst & 0x3f00) |
                            ((sub_only & 1) ? BLEND_SRC_BG0 | BLEND_SRC_BG1 : 0) |
                            ((sub_only & 2) ? BLEND_SRC_BG2 : 0) | ((sub_only & 4) ? BLEND_SRC_BG3 : 0));
      r.bldalpha = 8 | 8 << 8;
      r.bldy = 8;
    }
  }
  return r;
}

void NdsRenderCapture(unsigned crop_top, bool level) {
#ifdef SMW_NDS_TEST
  if (g_test_ref_on) {
    PpuBeginDrawing(s_ppu, (uint8_t *)g_test_ref, 256 * 4, 0);
    g_test_ref_crop = (uint8_t)crop_top;
  }
#endif
  PortRunPpuLines(&CaptureLine);
  s_crop = crop_top;
  PrepareMode7();
  FindChanges();
  for (uint32_t word = 0; word < 2048 / 32; word++) {
    uint32_t bits = s_dirty[word];
    while (bits) {
      const int bit = __builtin_ctz(bits);
      bits &= bits - 1;
      ConvertBlock(word * 32 + bit);
    }
  }
  const uint32_t bg1_bytes = PPU_bgTileAdr(s_ppu, 0) * 2;
  s_bg1_tile_base = (uint8_t)(bg1_bytes / 0x4000);
  s_bg1_tile_add = (uint16_t)((bg1_bytes % 0x4000) / 32);
  ConvertMap(0, DS_BG_MAPS + 0x0000, s_bg1_tile_add ? NULL : DS_BG_MAPS + 0x1000, 0, s_bg1_tile_add);
  ConvertMap(1, DS_BG_MAPS + 0x2000, NULL, 0, 0);
  ConvertMap(2, DS_BG_MAPS + 0x3000, NULL, 8, 0);
  s_maps_full_refresh = false;
  memset(s_dirty, 0, sizeof(s_dirty));
  s_layout = ComputeLayout(&s_lines[crop_top + kDsRows / 2], crop_top);
  UpdateSprites(crop_top, level, s_layout.obj_prio, s_layout.obj_math);

  /* Per-line scroll and backdrop for the 192 shown lines. Above the
   * status-bar split the SNES shows the status bar on BG3 (it is on the
   * bottom screen here); there DS BG3 continues the level's BG3 instead. */
  const int t = s_table ^ 1;
  s_line_words[t] = s_m7.on ? 12 : 4;
  const uint16_t v_add = (uint16_t)(crop_top + 1);
  int split = 0;  /* first SNES line below the status bar */
  if (level) {    /* the status-bar IRQ changes BG3 scroll/mode there */
    for (int r = 1; r < 64; r++) {
      if (s_lines[r].bgmode != s_lines[0].bgmode || s_lines[r].vs[2] != s_lines[0].vs[2] ||
          s_lines[r].hs[2] != s_lines[0].hs[2] || s_lines[r].main != s_lines[0].main) {
        split = r;
        break;
      }
    }
  }
  for (int y = 0; y <= kDsRows; y++) {
    const int line = (int)crop_top + (y < kDsRows ? y : kDsRows - 1);
    const LineRegs *l = &s_lines[line];
    const LineRegs *l3 = line < split ? &s_lines[split] : l;  /* BG3: level, not status bar */
    s_rowline[t][y] = (uint8_t)line;
    uint16_t *sc = ScrollRow(t, y);
    sc[0] = sc[2] = l->hs[0];
    sc[1] = sc[3] = l->vs[0] + v_add;
    sc[4] = l->hs[1];
    sc[5] = l->vs[1] + v_add;
    sc[6] = l3->hs[2];
    sc[7] = l3->vs[2] + v_add;
    /* SNES vertical mosaic blocks start at SNES line 0, DS ones at the top
     * of the DS screen; do the vertical part with the line scroll instead
     * (each line shows the first line of its block). */
    if (l->mosaic & 0xf0) {
      const uint16_t back = (uint16_t)((line + 1) % ((l->mosaic >> 4) + 1));
      if (l->mosaic & 1) { sc[1] -= back; sc[3] -= back; }
      if (l->mosaic & 2) sc[5] -= back;
      if (l3->mosaic & 4) sc[7] -= back;
    }
    s_backdrop[t][y] = Backdrop(l);
    s_winpos[t][y] = DsWindowSpan(l->w1l, l->w1r) | (uint32_t)DsWindowSpan(l->w2l, l->w2r) << 16;
    if (s_m7.on) Mode7Row((l->bgmode & 7) == 7 ? l : s_m7_line, line + 1, sc);
  }
  if (s_m7.on) {
    for (int y = 0; y <= kDsRows; y++) s_rowtext[t][y] = (s_lines[s_rowline[t][y]].bgmode & 7) != 7;
    /* Lines just above a mode 1 part show its layers early (see Present):
     * give them its BG1/BG2 scroll so they show the map just above it. */
    for (int y = kDsRows - 1; y >= 0; y--) {
      if (s_rowtext[t][y]) continue;
      for (int k = y + 1; k <= y + 2 && k <= kDsRows; k++) {
        if (s_rowtext[t][k]) {
          memcpy(ScrollRow(t, y), ScrollRow(t, k), 6 * sizeof(uint16_t));
          break;
        }
      }
    }
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
  s_shown_table = -1;  /* no restart from the interrupt while changing */
  s_table = s_ready_table;
  StartLineDma(s_table);

  /* Layer order and colour math: see ComputeLayout. */
  const Layout *lay = &s_layout;
  const bool bg2_sub_only = !(mid->main & 2) && (mid->sub & 2);

  const uint16_t mosaic_on = BIT(6);
  REG_BG0CNT = BG_PRIORITY(lay->prio[0]) | BG_TILE_BASE(s_bg1_tile_base) | BG_MAP_BASE(0) | BG_COLOR_16 | MapSizeBits(0) |
               ((mid->mosaic & 1) ? mosaic_on : 0);
  REG_BG1CNT = BG_PRIORITY(lay->prio[1]) | BG_TILE_BASE(s_bg1_tile_base) | BG_MAP_BASE(4) | BG_COLOR_16 | MapSizeBits(0) |
               ((mid->mosaic & 1) ? mosaic_on : 0);
  REG_BG2CNT = BG_PRIORITY(lay->prio[2]) | BG_TILE_BASE(PPU_bgTileAdr(p, 1) * 2 / 0x4000) |
               BG_MAP_BASE(8) | BG_COLOR_16 | MapSizeBits(1) | ((mid->mosaic & 2) ? mosaic_on : 0);
  REG_BG3CNT = BG_PRIORITY(lay->prio[3]) | BG_TILE_BASE(8) | BG_MAP_BASE(12) | BG_COLOR_16 |
               MapSizeBits(2) | ((mid->mosaic & 4) ? mosaic_on : 0);
  const unsigned m = (mid->mosaic >> 4) & 15;
  REG_MOSAIC = m;  /* horizontal only: vertical is done per line */

  const uint8_t shown = mid->main | mid->sub;
  uint32_t dispcnt = MODE_0_2D | DISPLAY_SCREEN_BASE(1) | DISPLAY_SPR_2D;
  const uint32_t bg1_bits = DISPLAY_BG0_ACTIVE | (s_bg1_tile_add ? 0 : DISPLAY_BG1_ACTIVE);
  if (shown & 1) dispcnt |= bg1_bits;
  if (shown & 2) dispcnt |= DISPLAY_BG2_ACTIVE;
  if (shown & 4) dispcnt |= DISPLAY_BG3_ACTIVE;
  if (shown & 16) dispcnt |= DISPLAY_SPR_ACTIVE;
  if (s_m7.on) {
    /* DS mode 1: BG3 is a rotation background showing the mode 7 layer. */
    dispcnt = MODE_1_2D | DISPLAY_SCREEN_BASE(1) | DISPLAY_SPR_2D |
              ((shown & 1) ? DISPLAY_BG3_ACTIVE : 0) | ((shown & 16) ? DISPLAY_SPR_ACTIVE : 0);
    REG_BG3CNT = BG_PRIORITY(2) | BG_TILE_BASE(10) | BG_MAP_BASE(16) | (3 << 14) |
                 (s_m7.wrap ? BIT(13) : 0);

  } else if ((mid->bgmode & 7) != 1) {
    dispcnt &= ~(DISPLAY_BG0_ACTIVE | DISPLAY_BG1_ACTIVE | DISPLAY_BG2_ACTIVE | DISPLAY_BG3_ACTIVE);
  }

  const DsWindowRegs win = ComputeWindows(mid, bg2_sub_only, s_m7.on);
  dispcnt |= win.dispcnt_bits;
  *(vu16 *)0x04000044 = kDsRows;  /* WIN0V: lines 0..191 */
  *(vu16 *)0x04000046 = kDsRows;  /* WIN1V */
  WIN_IN = win.winin;
  WIN_OUT = win.winout;
  /* Window colour effects (fades to black) win over colour math; sprite
   * blending works in either mode (it needs only the second targets). */
  REG_BLDCNT = win.bldcnt ? (uint16_t)(win.bldcnt | (lay->obj_math ? (lay->bldcnt & 0x3f00) : 0))
                          : lay->bldcnt;
  REG_BLDALPHA = lay->bldalpha;
  REG_BLDY = win.bldcnt ? win.bldy : lay->bldy;
  StartWindowDma(s_table, win.dispcnt_bits != 0);
  REG_DISPCNT = dispcnt;
  s_shown_windows = win.dispcnt_bits != 0;

  /* Mode 7 frames: lines that are SNES mode 1 (e.g. the bridge under
   * Reznor) show the text layers instead of the mode 7 layer. The DS turns
   * a background on two lines after DISPCNT says so (off is immediate), so
   * text layers are switched on two lines early (NdsRenderCapture gives
   * those lines the scroll of the line below), and the mode 7 layer stays
   * on from the top down to its last line. */
  bool line_dispcnt = false;
  if (s_m7.on) {
    const uint32_t bgs = DISPLAY_BG0_ACTIVE | DISPLAY_BG1_ACTIVE | DISPLAY_BG2_ACTIVE | DISPLAY_BG3_ACTIVE;
    int last_m7 = -1;
    for (int y = 0; y <= kDsRows; y++)
      if (!s_rowtext[s_table][y]) last_m7 = y;
    for (int y = 0; y <= kDsRows; y++) {
      const LineRegs *l = &s_lines[s_rowline[s_table][y]];
      const uint8_t on = l->main | l->sub;
      uint32_t v = dispcnt & ~bgs;
      if (y <= last_m7 && (on & 1)) v |= DISPLAY_BG3_ACTIVE;
      bool text = false;
      for (int k = y; k <= y + 2 && k <= kDsRows; k++) text |= s_rowtext[s_table][k];
      if (text) {
        line_dispcnt = true;
        if (on & 1) v |= bg1_bits;
        if (on & 2) v |= DISPLAY_BG2_ACTIVE;
      }
      s_dispcnt[s_table][y] = v;
    }
    if (line_dispcnt) DC_FlushRange(s_dispcnt[s_table], sizeof(s_dispcnt[s_table]));
  }
  StartDispcntDma(s_table, line_dispcnt);
  s_shown_dispcnt = line_dispcnt;
  s_shown_table = s_table;

  /* Brightness fades and forced blank. */
  const unsigned b = mid->inidisp & 15;
  if (mid->inidisp & 0x80 || b == 0)
    REG_MASTER_BRIGHT = (2 << 14) | 16;
  else
    REG_MASTER_BRIGHT = b == 15 ? 0 : (2 << 14) | (((15 - b) * 16 + 7) / 15);

  memcpy((void *)DS_OAM, s_oam_stage, sizeof(s_oam_stage));
  UpdatePalettes(s_m7.on);

  /* Copy the changed 32-byte chunks to video memory. Copying is fast, but
   * a huge change (new level graphics, ~all of video memory) still may not
   * fit in VBlank; then show that one frame black so half-updated tiles
   * never appear. Scene changes fade through black anyway. */
  static uint32_t *const kVram[kRegions] = {VRAM_BG_TILES, VRAM_BG_MAPS, VRAM_BG3_TILES,
                                            VRAM_OBJ_TILES, VRAM_M7_MAP, VRAM_M7_TILES};
  static const uint32_t *const kMirror[kRegions] = {s_mir_bg, s_mir_maps, s_mir_bg3,
                                                    s_mir_obj, s_mir_m7map, s_mir_m7tiles};
  unsigned chunks = 0;
  for (int r = 0; r < kRegions; r++)
    for (uint32_t w = 0; w < kRegionChunks[r] / 32; w++) chunks += __builtin_popcount(s_upload[r][w]);
  if (chunks > kMaxChunksPerVBlank)
    REG_MASTER_BRIGHT = (2 << 14) | 16;
  for (int r = 0; r < kRegions; r++) {
    for (uint32_t w = 0; w < kRegionChunks[r] / 32; w++) {
      uint32_t bits = s_upload[r][w];
      s_upload[r][w] = 0;
      while (bits) {
        const uint32_t c = w * 32 + __builtin_ctz(bits);
        bits &= bits - 1;
        uint32_t *d = &kVram[r][c * 8];
        const uint32_t *src = &kMirror[r][c * 8];
        d[0] = src[0]; d[1] = src[1]; d[2] = src[2]; d[3] = src[3];
        d[4] = src[4]; d[5] = src[5]; d[6] = src[6]; d[7] = src[7];
      }
    }
  }

}
