/* Bring-up video path: the SNES picture is drawn in software and copied to
 * a top-screen bitmap. This is a stepping stone; the planned renderer
 * hands SNES tiles/sprites straight to the DS's own 2D hardware. */
#include "nds_video.h"

#include <nds.h>

#include "smw_port.h"

enum { kSnesWidth = 256, kSnesHeight = 224, kDsHeight = 192 };

static uint16_t *s_top;
static uint32_t s_frame[kSnesWidth * kSnesHeight]; /* XRGB8888 from the PPU */

void NdsVideoInit(void) {
  videoSetMode(MODE_5_2D | DISPLAY_BG3_ACTIVE);
  vramSetBankA(VRAM_A_MAIN_BG);
  const int bg = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
  s_top = (uint16_t *)bgGetGfxPtr(bg);
  consoleDemoInit(); /* bottom screen text, VRAM C */
}

/* Which 192 of the 224 SNES lines to show. In levels (game mode $14) the
 * top 32 lines are the status bar, which will move to the bottom screen;
 * elsewhere the picture is centred. */
static unsigned CropTop(const uint8_t *game_ram) {
  return game_ram[0x100] == 0x14 ? 32 : 16;
}

void NdsVideoRenderFrame(const uint8_t *game_ram) {
  PortRenderSoftware((uint8_t *)s_frame, kSnesWidth * 4);

  const uint32_t *src = s_frame + CropTop(game_ram) * kSnesWidth;
  uint16_t *dst = s_top;
  for (unsigned i = 0; i < kSnesWidth * kDsHeight; i++) {
    const uint32_t c = src[i]; /* bytes: B, G, R, X */
    dst[i] = (uint16_t)(0x8000 | ((c >> 19) & 0x1f) | (((c >> 11) & 0x1f) << 5) |
                        (((c >> 3) & 0x1f) << 10));
  }
}
