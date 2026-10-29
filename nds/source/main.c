/* Super Mario World DS - ARM9 entry point.
 *
 * Runs the native game code (src/, from snesrev/smw) at the SNES's 60 Hz
 * logic rate and shows the picture on the top screen. The bottom screen
 * currently shows status text; the HUD/touch features come later.
 */
#include <nds.h>
#include <fat.h>
#include <filesystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "nds_timing.h"
#include "nds_audio.h"
#include "nds_hud.h"
#include "nds_render.h"
#include "smw_port.h"

#ifdef HAVE_EMBEDDED_ASSETS
#include "smw_assets_bin.h"
#endif

#define SMW_NDS_VERSION "0.4.5"

void PortPlatformFatal(const char *message) {
  consoleDemoInit(); /* the bottom screen may be showing the HUD */
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

/* Game graphics/levels, from (in order): data built into the program by
 * build.bat, a file packed into the .nds file system, or the SD card. */
static const uint8_t *LoadAssets(size_t *size, bool have_fat) {
#ifdef HAVE_EMBEDDED_ASSETS
  *size = smw_assets_bin_size;
  return smw_assets_bin;
#else
  static const char *const kSdPaths[] = {
    "/smw_assets.dat", "/_nds/smw-nds/smw_assets.dat",
  };
  if (nitroFSInit(NULL)) {
    uint8_t *data = ReadWholeFile("nitro:/smw_assets.dat", size);
    if (data) return data;
  }
  if (!have_fat) return NULL;
  for (unsigned i = 0; i < sizeof(kSdPaths) / sizeof(kSdPaths[0]); i++) {
    uint8_t *data = ReadWholeFile(kSdPaths[i], size);
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

/* Which 192 of the 224 SNES lines to show. In levels the top 32 lines are
 * the status bar, which is on the bottom screen instead; elsewhere the
 * picture is centred. "In a level" includes the fade-in (modes $12-$13)
 * and the fade-out back to the map ($0B-$0D after a level), so the status
 * bar never shows for a moment. */
static unsigned CropTop(const uint8_t *game_ram) {
  static bool level_scene;
  const uint8_t mode = game_ram[0x100];
  if (mode >= 0x12 && mode <= 0x16) level_scene = true;
  else if (!(mode >= 0x0b && mode <= 0x0d && level_scene)) level_scene = false;
  return level_scene ? 32 : 16;
}

int main(void) {
  defaultExceptionHandler();
  consoleDemoInit(); /* bottom screen text for now */

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
  if (have_fat) {
    /* Saves go to /_nds/smw-nds/saves/smw.srm on the SD card. */
    mkdir("/_nds", 0777);
    mkdir("/_nds/smw-nds", 0777);
    if (chdir("/_nds/smw-nds") == 0)
      PortLoadSave();
  }
  NdsRenderInit();
  NdsHudInit();
  NdsTimingStart();
  NdsAudioInit();

  uint32_t frames = 0, last_second = 0, busy_ticks = 0;

  for (;;) {
    /* One SNES frame per DS screen refresh (59.8 Hz vs the SNES's 60.1). */
    const uint32_t t0 = NdsTimingTicks();
    PortRunFrame(NdsHudInput(ReadPad()));
    NdsAudioFrame();
    NdsRenderCapture(CropTop(PortGameRam()));
    busy_ticks += NdsTimingTicks() - t0;
#ifdef SMW_NDS_TEST_DROP
    { /* test: miss a VBlank every 8th frame */
      static unsigned n;
      if ((++n & 7) == 0) { const uint32_t s = NdsTimingTicks(); while (NdsTimingTicks() - s < 600) {} }
    }
#endif

    swiWaitForVBlank();
    NdsRenderPresent();
    NdsHudPresent();
    frames++;

    const uint32_t now = NdsTimingFramesElapsed();
    if (now - last_second >= 60) {
      /* CPU load: share of the frame time spent before waiting for VBlank
       * (547 ticks per frame). */
      NdsHudSetStats(frames * 60 / (now - last_second), busy_ticks * 100 / (547 * frames));
      frames = 0;
      busy_ticks = 0;
      last_second = now;
    }
  }
}
