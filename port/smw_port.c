/* smw-nds: replaces the desktop-only parts of snesrev's main.c (SDL window,
 * audio device, config file) with a small platform-neutral layer. */
#include "smw_port.h"

#include <stdarg.h>
#include <sys/stat.h>
#include <unistd.h>
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

static int s_save_count, s_save_ok;

/* Saves alternate between two files, each written as a brand-new file
 * (delete, then create) with a trailer holding a save counter; the newest
 * valid one is loaded. Overwriting an existing file in place never reached
 * the SD card on a real DS + R4, while creating a file did. If a write
 * fails, the other file still holds the previous save. */
enum { kSaveMagic = 0x53444d53 /* "SMDS" */ };
typedef struct SaveTrailer {
  uint32 magic, counter, size, checksum;
} SaveTrailer;

static const char *const kSaveSlots[2] = {"saves/smw-a.srm", "saves/smw-b.srm"};
static uint32 s_save_counter;
static int s_save_slot = -1;  /* slot holding the newest save */

static uint32 SaveChecksum(const uint8 *data, size_t size) {
  uint32 h = 2166136261u;
  for (size_t i = 0; i < size; i++) h = (h ^ data[i]) * 16777619u;
  return h;
}

/* Reads a slot into `out`; returns its counter, or -1 if missing/invalid. */
static int64 ReadSlot(const char *path, uint8 *out, size_t size) {
  FILE *f = fopen(path, "rb");
  if (!f) return -1;
  SaveTrailer t;
  const bool ok = fread(out, 1, size, f) == size && fread(&t, sizeof(t), 1, f) == 1;
  fclose(f);
  if (!ok || t.magic != kSaveMagic || t.size != size || t.checksum != SaveChecksum(out, size))
    return -1;
  return t.counter;
}

void PortWriteSram(const uint8 *data, size_t size) {
  static uint8 check[4096];
  const int slot = s_save_slot == 0 ? 1 : 0;  /* never overwrite the newest */
  const char *path = kSaveSlots[slot];
  const SaveTrailer t = {kSaveMagic, s_save_counter + 1, (uint32)size, SaveChecksum(data, size)};
  bool ok = false;
  MkDir("saves");
  for (int attempt = 0; attempt < 2 && !ok && size <= sizeof(check); attempt++) {
    remove(path);
    FILE *f = fopen(path, "wb");
    if (!f) continue;
    const bool wrote = fwrite(data, 1, size, f) == size && fwrite(&t, sizeof(t), 1, f) == 1 &&
                       fflush(f) == 0;
    fsync(fileno(f));
    fclose(f);
    ok = wrote && ReadSlot(path, check, size) == (int64)t.counter && memcmp(check, data, size) == 0;
  }
  if (ok) {
    s_save_counter = t.counter;
    s_save_slot = slot;
  } else {
    fprintf(stderr, "Unable to write %s\n", path);
  }
  s_save_count++;
  s_save_ok = ok;
}

int PortSaveStatus(bool *ok) {
  *ok = s_save_ok;
  return s_save_count;
}

void PortLoadSave(void) {
  static uint8 buf[4096];
  MkDir("saves");
  if (g_sram_size > sizeof(buf)) return;
  int64 best = -1;
  for (int i = 0; i < 2; i++) {
    const int64 c = ReadSlot(kSaveSlots[i], buf, g_sram_size);
    if (c > best) {
      best = c;
      s_save_slot = i;
      memcpy(g_sram, buf, g_sram_size);
    }
  }
  if (best >= 0) {
    s_save_counter = (uint32)best;
  } else {
    RtlReadSram();  /* saves/smw.srm from earlier versions */
  }
}

static int s_audio_mode = kPortAudio_Silent;

void PortSetAudioMode(int mode) {
  s_audio_mode = mode;
}

struct Dsp *PortDsp(void) {
  return g_spc_player->dsp;
}

const uint8_t *PortSpcRam(void) {
  return g_spc_player->ram;
}

void PortRenderAudio(int16_t *stereo, int samples) {
  RtlRenderAudio(stereo, samples, 2);
}

void PortRunFrame(uint32_t buttons) {
  /* Bit 30 tells the game a controller is plugged into port 1. */
  RtlRunFrame((buttons & 0xfff) | (1u << 30));
  /* The game waits for replies from the sound driver, so it must run every
   * frame; with audio on, PortRenderAudio does that. */
  if (s_audio_mode == kPortAudio_Silent)
    RtlTickAudioSilent();
  else if (s_audio_mode == kPortAudio_Hardware)
    RtlTickAudioEnvelopes();
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

/* Same composition as the game's UpdateLevelName (049d07): up to three
 * pieces from the level-name string table. */
static int AppendNamePiece(uint16 j, uint8_t *codes, int n, int max) {
  uint8 t;
  do {
    t = kUpdateLevelName_LevelNameStrings[j++];
    if (n < max) codes[n++] = t & 0x7f;
  } while (!(t & 0x80));
  return n;
}

int PortLevelNameCodes(uint8_t translevel, uint8_t *codes, int max) {
  const uint16 r0w = kLevelNames[translevel];
  int n = 0;
  const uint16 v1 = kUpdateLevelName_DATA_049C91[(r0w >> 8) & 0x7f];
  if ((kUpdateLevelName_LevelNameStrings[v1] & 0x80) == 0)
    n = AppendNamePiece(v1, codes, n, max);
  const uint16 v2 = kUpdateLevelName_DATA_049CCF[(r0w & 0xf0) >> 4];
  if (kUpdateLevelName_LevelNameStrings[v2] != 0x9f)
    n = AppendNamePiece(v2, codes, n, max);
  n = AppendNamePiece(kUpdateLevelName_DATA_049CED[r0w & 0xf], codes, n, max);
  while (n > 0 && codes[n - 1] == 0x1f) n--;  /* trim trailing spaces */
  return n;
}

uint8_t *PortGameRam(void) {
  return g_ram;
}
