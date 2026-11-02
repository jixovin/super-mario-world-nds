#ifndef SMW_NDS_HUD_ART_H
#define SMW_NDS_HUD_ART_H

#include <stdint.h>

/* Themed pixel-art backgrounds for the bottom screen. All art is drawn on a
 * 128x96 grid where each cell is a 2x2 block of DS pixels, so every edge
 * lines up with the 2x status-bar glyphs. No blending or gradients: bands
 * and checkerboard dithering only. */

enum {
  kThemeGrass, kThemeSky, kThemeCave, kThemeWater, kThemeCastle, kThemeGhost,
  kThemeForest, kThemeSnow, kThemeNight, kThemePalace, kThemeChocolate,
  kThemeMap,
  kThemeCount,
};

/* variant: palace colour (0 yellow, 1 green, 2 red, 3 blue), night style
 * (0 star, 1 special) or map submap (0-6). */
void HudArtDrawScene(uint16_t *fb, int theme, int variant);

/* An opaque panel in the theme's colours: x, y, w, h in DS pixels (even). */
void HudArtPanel(uint16_t *fb, int theme, int x, int y, int w, int h);

/* Goal posts on the ground for the level progress track (DS pixels). */
void HudArtGoal(uint16_t *fb, int x);

#endif
