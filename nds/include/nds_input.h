#ifndef SMW_NDS_INPUT_H
#define SMW_NDS_INPUT_H

#include <stdint.h>

enum {
  SMW_PAD_UP     = 1u << 0,
  SMW_PAD_DOWN   = 1u << 1,
  SMW_PAD_LEFT   = 1u << 2,
  SMW_PAD_RIGHT  = 1u << 3,
  SMW_PAD_A      = 1u << 4,
  SMW_PAD_B      = 1u << 5,
  SMW_PAD_X      = 1u << 6,
  SMW_PAD_Y      = 1u << 7,
  SMW_PAD_L      = 1u << 8,
  SMW_PAD_R      = 1u << 9,
  SMW_PAD_START  = 1u << 10,
  SMW_PAD_SELECT = 1u << 11,
};

/* Read the buttons held during this frame, using direct SNES-name mappings. */
uint16_t NdsInputReadHeld(void);

#endif
