#include "nds_video.h"

#include <nds.h>
#include <stdint.h>

static uint16_t *s_top_pixels;

bool NdsVideoInit(void) {
  videoSetMode(MODE_5_2D | DISPLAY_BG3_ACTIVE);
  vramSetBankA(VRAM_A_MAIN_BG);

  const int background = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
  if (background < 0) return false;
  bgSetPriority(background, 0);
  s_top_pixels = (uint16_t *)bgGetGfxPtr(background);
  if (s_top_pixels == NULL) return false;

  /* libnds' prototype console uses the lower display and VRAM C. */
  consoleDemoInit();
  return true;
}

void NdsVideoPresentFrame(const uint32_t *frame, size_t pitch_bytes) {
  if (s_top_pixels == NULL || frame == NULL ||
      pitch_bytes < SMW_SNES_FRAME_WIDTH * sizeof(uint32_t)) {
    return;
  }

  for (unsigned y = 0; y < SMW_NDS_SCREEN_HEIGHT; ++y) {
    const unsigned source_y =
        (y * (SMW_SNES_FRAME_HEIGHT - 1u)) / (SMW_NDS_SCREEN_HEIGHT - 1u);
    const uint8_t *source_bytes = (const uint8_t *)frame + source_y * pitch_bytes;
    const uint32_t *source = (const uint32_t *)source_bytes;
    uint16_t *destination = s_top_pixels + y * SMW_NDS_SCREEN_WIDTH;

    for (unsigned x = 0; x < SMW_NDS_SCREEN_WIDTH; ++x) {
      const uint32_t color = source[x]; /* Upstream PPU packs 0x00BBGGRR. */
      const uint16_t red = (uint16_t)((color & 0xF8u) >> 3);
      const uint16_t green = (uint16_t)(((color >> 8) & 0xF8u) << 2);
      const uint16_t blue = (uint16_t)(((color >> 16) & 0xF8u) << 7);
      destination[x] = (uint16_t)(BIT(15) | red | green | blue);
    }
  }
}

