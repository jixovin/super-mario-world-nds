#ifndef SMW_NDS_VIDEO_H
#define SMW_NDS_VIDEO_H

#include <stdint.h>

/* Top screen: 256x192 16-bit bitmap. Bottom screen: text console. */
void NdsVideoInit(void);

/* Draw the current game frame (software renderer, bring-up path) and copy
 * 192 of its 224 lines to the top screen. game_ram selects the crop. */
void NdsVideoRenderFrame(const uint8_t *game_ram);

#endif
