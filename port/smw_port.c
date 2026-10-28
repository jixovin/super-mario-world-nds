/* smw-nds: replaces the desktop-only parts of snesrev's main.c (SDL window,
 * audio device, config file) with a small platform-neutral layer. */
#include "smw_port.h"

#include <stdarg.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>

#include "assets/smw_assets.h"
#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "config.h"
#include "smw_rtl.h"
#include "smw_spc_player.h"
#include "snes/ppu.h"
#include "snes/snes.h"
#include "types.h"
#include "util.h"

/* Globals that snesrev's desktop main.c normally provides. */
bool g_debug_flag;
bool g_want_dump_memmap_flags;
bool g_new_ppu = true;
bool g_other_image = true;
struct SpcPlayer *g_spc_player;
int g_got_mismatch_count;
Config g_config;

const uint8 *g_asset_ptrs[kNumberOfAssets];
uint32 g_asset_sizes[kNumberOfAssets];

/* The Super Mario All-Stars games in snesrev's tree are not part of this
 * port; these satisfy references in the shared ROM-loading path. */
const RtlGameInfo kSmb1GameInfo;
const RtlGameInfo kSmbllGameInfo;

extern Snes *g_snes;
extern Ppu *g_my_ppu;

void NORETURN Die(const char *error) {
  PortPlatformFatal(error);
  for (;;) {}
}

void Warning(const char *error) {
  fprintf(stderr, "Warning: %s\n", error);
}

/* Audio runs on the same thread for now (silent build), so no locking. */
void RtlApuLock(void) {}
void RtlApuUnlock(void) {}

void MkDir(const char *s) { mkdir(s, 0777); }

const char *PortLoadAssets(const uint8_t *data, size_t length) {
  static const char kAssetsSig[] = { kAssets_Sig };
  if (data == NULL ||
      length < 16 + 32 + 32 + 8 + kNumberOfAssets * 4 ||
      memcmp(data, kAssetsSig, 48) != 0 ||
      *(const uint32 *)(data + 80) != kNumberOfAssets)
    return "smw_assets.dat does not match this build. Re-run the asset step.";

  uint32 offset = 88 + kNumberOfAssets * 4 + *(const uint32 *)(data + 84);
  for (size_t i = 0; i < kNumberOfAssets; i++) {
    uint32 size = *(const uint32 *)(data + 88 + i * 4);
    offset = (offset + 3) & ~3u;
    if ((uint64)offset + size > length)
      return "smw_assets.dat is damaged (truncated).";
    g_asset_sizes[i] = size;
    g_asset_ptrs[i] = data + offset;
    offset += size;
  }
  return NULL;
}

MemBlk FindInAssetArray(int asset, int idx) {
  return FindIndexInMemblk((MemBlk){ g_asset_ptrs[asset], g_asset_sizes[asset] }, idx);
}

const uint8 *FindPtrInAsset(int asset, uint32 addr) {
  return FindAddrInMemblk((MemBlk){ g_asset_ptrs[asset], g_asset_sizes[asset] }, addr);
}

void PortInitGame(void) {
  /* No ROM: snesrev runs only its own reimplemented code (RM_MINE). */
  if (SnesInit(NULL, 0) == NULL)
    Die("Game initialisation failed");
  g_spc_player = SmwSpcPlayer_Create();
  g_spc_player->initialize(g_spc_player);
}

void PortLoadSave(void) {
  MkDir("saves");
  RtlReadSram();
}

void PortRunFrame(uint32_t buttons) {
  /* Bit 30 tells the game a controller is plugged into port 1. */
  RtlRunFrame((buttons & 0xfff) | (1u << 30));
  /* The game waits for replies from the sound driver, so it must run every
   * frame. Silent for now; real audio output comes in a later milestone. */
  RtlTickAudioSilent();
}

void PortRenderSoftware(uint8_t *pixels, size_t pitch) {
  PpuBeginDrawing(g_my_ppu, pixels, pitch, 0);
  g_rtl_game_info->draw_ppu_frame();
}

extern void (*g_smw_nds_line_hook)(int line);

void PortRunPpuLines(PortLineHook *hook) {
  g_smw_nds_line_hook = hook;
  g_rtl_game_info->draw_ppu_frame();
  g_smw_nds_line_hook = NULL;
}

struct Ppu *PortPpu(void) {
  return g_my_ppu;
}

uint8_t *PortGameRam(void) {
  return g_ram;
}
