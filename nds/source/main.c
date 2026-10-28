/* Super Mario World DS - ARM9 entry point.
 *
 * Runs the native game code (src/, from snesrev/smw) at the SNES's 60 Hz
 * logic rate and shows the picture on the top screen. The bottom screen
 * currently shows status text; the HUD/touch features come later.
 */
#include <nds.h>
#include <fat.h>
#include <stdio.h>
#include <stdlib.h>

#include "nds_timing.h"
#include "nds_video.h"
#include "smw_port.h"

#ifdef HAVE_EMBEDDED_ASSETS
#include "smw_assets_bin.h"
#endif

#define SMW_NDS_VERSION "0.1.0"

void PortPlatformFatal(const char *message) {
  iprintf("\x1b[2J\x1b[31;1mERROR\x1b[39m\n\n%s\n", message);
  for (;;) swiWaitForVBlank();
}

static uint8_t *ReadWholeFile(const char *path, size_t *size) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long length = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *data = length > 0 ? malloc((size_t)length) : NULL;
  if (data && fread(data, 1, (size_t)length, f) != (size_t)length) {
    free(data);
    data = NULL;
  }
  fclose(f);
  *size = data ? (size_t)length : 0;
  return data;
}

/* Game graphics/levels: built into the .nds by build.bat when available,
 * otherwise read from the SD card. */
static const uint8_t *LoadAssets(size_t *size, bool have_fat) {
#ifdef HAVE_EMBEDDED_ASSETS
  *size = smw_assets_bin_size;
  return smw_assets_bin;
#else
  static const char *const kPaths[] = {
    "/smw_assets.dat", "/_nds/smw-nds/smw_assets.dat",
  };
  if (!have_fat) return NULL;
  for (unsigned i = 0; i < sizeof(kPaths) / sizeof(kPaths[0]); i++) {
    uint8_t *data = ReadWholeFile(kPaths[i], size);
    if (data) return data;
  }
  return NULL;
#endif
}

static uint32_t ReadPad(void) {
  scanKeys();
  const uint32_t held = keysHeld();
  uint32_t pad = 0;
  if (held & KEY_UP) pad |= kPortPad_Up;
  if (held & KEY_DOWN) pad |= kPortPad_Down;
  if (held & KEY_LEFT) pad |= kPortPad_Left;
  if (held & KEY_RIGHT) pad |= kPortPad_Right;
  if (held & KEY_A) pad |= kPortPad_A;
  if (held & KEY_B) pad |= kPortPad_B;
  if (held & KEY_X) pad |= kPortPad_X;
  if (held & KEY_Y) pad |= kPortPad_Y;
  if (held & KEY_L) pad |= kPortPad_L;
  if (held & KEY_R) pad |= kPortPad_R;
  if (held & KEY_START) pad |= kPortPad_Start;
  if (held & KEY_SELECT) pad |= kPortPad_Select;
  return pad;
}

int main(void) {
  defaultExceptionHandler();
  NdsVideoInit();

  iprintf("Super Mario World DS v%s\n", SMW_NDS_VERSION);
  iprintf("Native port - not an emulator\n\n");

  const bool have_fat = fatInitDefault();
  size_t assets_size = 0;
  const uint8_t *assets = LoadAssets(&assets_size, have_fat);
  if (!assets)
    PortPlatformFatal("Game data not found.\n\n"
                      "Build with build.bat (it adds\n"
                      "the data from your own ROM),\n"
                      "or copy smw_assets.dat to the\n"
                      "root of the SD card.");
  const char *error = PortLoadAssets(assets, assets_size);
  if (error) PortPlatformFatal(error);

  PortInitGame();
  NdsTimingStart();

  uint32_t frames_run = 0, shown = 0, logic = 0, second_mark = 0;
  unsigned fps_render = 0, fps_logic = 0;

  for (;;) {
    /* Run game logic to catch up with real time (60 Hz), at most 4 frames
     * per drawn picture so a slow frame cannot snowball. */
    const uint32_t due = NdsTimingFramesElapsed();
    uint32_t steps = 0;
    do {
      PortRunFrame(ReadPad());
      frames_run++;
      logic++;
    } while (frames_run < due && ++steps < 4);
    if (frames_run < due) frames_run = due;

    NdsVideoRenderFrame(PortGameRam());
    shown++;

    if (due - second_mark >= 60) {
      fps_render = shown;
      fps_logic = logic;
      shown = logic = 0;
      second_mark = due;
      iprintf("\x1b[5;0Hdraw %2u fps  game %2u fps   ", fps_render, fps_logic);
      iprintf("\x1b[6;0Hmode %02X                     ", PortGameRam()[0x100]);
    }

    /* Ahead of real time: sleep until the next screen refresh. */
    if (frames_run > NdsTimingFramesElapsed())
      swiWaitForVBlank();
  }
}
