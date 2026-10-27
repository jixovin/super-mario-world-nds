#include "nds_input.h"

#include <nds.h>

uint16_t NdsInputReadHeld(void) {
  scanKeys();
  const uint32_t held = keysHeld();
  uint16_t result = 0;

  if (held & KEY_UP) result |= SMW_PAD_UP;
  if (held & KEY_DOWN) result |= SMW_PAD_DOWN;
  if (held & KEY_LEFT) result |= SMW_PAD_LEFT;
  if (held & KEY_RIGHT) result |= SMW_PAD_RIGHT;
  if (held & KEY_A) result |= SMW_PAD_A;
  if (held & KEY_B) result |= SMW_PAD_B;
  if (held & KEY_X) result |= SMW_PAD_X;
  if (held & KEY_Y) result |= SMW_PAD_Y;
  if (held & KEY_L) result |= SMW_PAD_L;
  if (held & KEY_R) result |= SMW_PAD_R;
  if (held & KEY_START) result |= SMW_PAD_START;
  if (held & KEY_SELECT) result |= SMW_PAD_SELECT;

  return result;
}

