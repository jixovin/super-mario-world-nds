#ifndef SMW_NDS_RENDER_H
#define SMW_NDS_RENDER_H

#include <stdint.h>

/* Hardware renderer: shows the game on the top screen using the DS's own 2D
 * engine. The SNES tiles, tile maps, palettes and sprites the game writes
 * are converted to DS format; per-scanline effects use HBlank DMA. */

/* Set up the top screen (main engine). Call once. */
void NdsRenderInit(void);

/* After a game frame (before VBlank): record the frame's per-line PPU state
 * and prepare the upload. crop_top is which SNES line (0-32) becomes the
 * top DS line. */
void NdsRenderCapture(unsigned crop_top);

/* During VBlank: copy the prepared frame into DS video memory/registers. */
void NdsRenderPresent(void);

#endif
