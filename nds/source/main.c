#include <nds.h>
#include <stdio.h>
#include <stdint.h>

#include "nds_input.h"
#include "nds_video.h"

static uint32_t s_demo_frame[SMW_SNES_FRAME_WIDTH * SMW_SNES_FRAME_HEIGHT];

static void BuildColorBars(void) {
  static const uint32_t colors[] = {
      0x00FFFFFFu, 0x0000FFFFu, 0x00FFFF00u, 0x0000FF00u,
      0x00FF00FFu, 0x000000FFu, 0x00FF0000u, 0x00000000u,
  };
  const unsigned bar_width = SMW_SNES_FRAME_WIDTH / 8;

  for (unsigned y = 0; y < SMW_SNES_FRAME_HEIGHT; ++y) {
    for (unsigned x = 0; x < SMW_SNES_FRAME_WIDTH; ++x) {
      s_demo_frame[y * SMW_SNES_FRAME_WIDTH + x] = colors[x / bar_width];
    }
  }
}

static void PrintButtons(uint16_t buttons) {
  iprintf("\x1b[9;0HButtons held:");
  iprintf("\x1b[10;0H%12s %12s %12s %12s",
          (buttons & SMW_PAD_UP) ? "UP" : "",
          (buttons & SMW_PAD_DOWN) ? "DOWN" : "",
          (buttons & SMW_PAD_LEFT) ? "LEFT" : "",
          (buttons & SMW_PAD_RIGHT) ? "RIGHT" : "");
  iprintf("\x1b[12;0HA:%c B:%c X:%c Y:%c L:%c R:%c",
          (buttons & SMW_PAD_A) ? '1' : '-',
          (buttons & SMW_PAD_B) ? '1' : '-',
          (buttons & SMW_PAD_X) ? '1' : '-',
          (buttons & SMW_PAD_Y) ? '1' : '-',
          (buttons & SMW_PAD_L) ? '1' : '-',
          (buttons & SMW_PAD_R) ? '1' : '-');
  iprintf("\x1b[13;0HSTART:%c SELECT:%c",
          (buttons & SMW_PAD_START) ? '1' : '-',
          (buttons & SMW_PAD_SELECT) ? '1' : '-');
}

int main(void) {
  if (!NdsVideoInit()) {
    for (;;) swiWaitForVBlank();
  }

  BuildColorBars();
  NdsVideoPresentFrame(s_demo_frame, SMW_SNES_FRAME_WIDTH * sizeof(uint32_t));

  iprintf("SMW DS - hardware bring-up\n");
  iprintf("\nTop: SNES-to-DS color/scaling check\n");
  iprintf("Bottom: DS input check\n\n");
  iprintf("This is a platform diagnostic,\nnot gameplay yet.\n");
  iprintf("\nPress START to exit.\n");

  while (pmMainLoop()) {
    swiWaitForVBlank();
    const uint16_t buttons = NdsInputReadHeld();
    PrintButtons(buttons);
    if (buttons & SMW_PAD_START) break;
  }

  return 0;
}

