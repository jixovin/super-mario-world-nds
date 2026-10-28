#ifndef SMW_NDS_HUD_H
#define SMW_NDS_HUD_H

#include <stdbool.h>
#include <stdint.h>

/* Bottom screen (sub engine): the game's status bar, a large touchable
 * reserve-item box and an optional FPS counter. Built from the game's own
 * BG3 tiles and sprites, so it always matches what the game shows. */

void NdsHudInit(void);

/* The renderer calls this whenever it converts a BG3 (status bar / font)
 * tile, so the bottom screen has the same tiles. */
void NdsHudOnBg3Tile(unsigned index, const uint32_t *tile);

/* Adds buttons from the touch screen (tapping the item box = Select) and
 * handles HUD taps. Call once per frame with the held SNES buttons. */
uint32_t NdsHudInput(uint32_t pad);

/* During VBlank, after the top screen is presented. */
void NdsHudPresent(void);

/* Shown when the FPS counter is on. */
void NdsHudSetFps(unsigned fps);

#endif
