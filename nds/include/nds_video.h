#ifndef SMW_NDS_VIDEO_H
#define SMW_NDS_VIDEO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
  SMW_NDS_SCREEN_WIDTH = 256,
  SMW_NDS_SCREEN_HEIGHT = 192,
  SMW_SNES_FRAME_WIDTH = 256,
  SMW_SNES_FRAME_HEIGHT = 224,
};

/* Configure a bitmap framebuffer on the top screen and a text console below. */
bool NdsVideoInit(void);

/* Convert the upstream PPU's packed 0x00BBGGRR output into DS RGB15 pixels.
 * The full 256x224 SNES picture is vertically rescaled to the DS's 256x192
 * screen, preserving the complete image and its 4:3 display proportions. */
void NdsVideoPresentFrame(const uint32_t *frame, size_t pitch_bytes);

#endif
