/* smw-nds desktop test harness.
 *
 * Runs the same game core the DS uses, with scripted button input, and
 * writes selected frames as PPM images. Used to check changes without
 * hardware:   hostrun smw_assets.dat out_dir script.txt
 *
 * Script lines:  <frames> <buttons>   e.g. "120 start" or "60 right+b"
 * Lines starting with '#' are comments. "shot" takes a screenshot.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "smw_port.h"
#include "util.h"
MemBlk FindInAssetArray(int asset, int idx);
#include "snes/ppu.h"

extern Ppu *g_my_ppu;
extern Ppu *g_ppu;

/* "lines": print PPU register changes line by line during the next frame. */
static bool g_dump_lines;
static uint8_t g_prev_regs[PPU_SAVESTATE_REGS_SIZE];
static void DumpLineHook(int line) {
  if (line >= 1) {
    const uint8_t *r = (const uint8_t *)g_ppu;
    if (line == 1 || memcmp(r, g_prev_regs, PPU_SAVESTATE_REGS_SIZE)) {
      Ppu *p = g_ppu;
      printf(" line %3d: inidisp=%02x mode=%02x main=%02x sub=%02x mwin=%02x swin=%02x wsel=%06x "
             "w1=%d-%d w2=%d-%d wlog=%04x cgwsel=%02x cgadsub=%02x fixed=%04x bg3=%d,%d bg1=%d,%d m7=%d,%d,%d,%d,%d,%d,%d,%d sel=%02x\n",
             line, p->inidisp, p->bgmode, p->screenEnabled[0], p->screenEnabled[1],
             p->screenWindowed[0], p->screenWindowed[1], (unsigned)p->windowsel,
             p->window1left, p->window1right, p->window2left, p->window2right, p->wbgobjlog,
             p->cgwsel, p->cgadsub, p->fixedColor, p->hScroll[2], p->vScroll[2],
             p->hScroll[0], p->vScroll[0], p->m7matrix[0], p->m7matrix[1], p->m7matrix[2],
             p->m7matrix[3], p->m7matrix[4], p->m7matrix[5], p->m7matrix[6], p->m7matrix[7], p->m7sel);
      memcpy(g_prev_regs, r, PPU_SAVESTATE_REGS_SIZE);
    }
  }
  ppu_runLine(g_ppu, line);
}

/* "stat": distinct per-line register combinations seen over many frames. */
typedef struct StatKey { uint8_t mode, main, sub, cgwsel, cgadsub, mwin, swin, m7sel; uint16_t fixed, nba; } StatKey;
static StatKey g_stat_keys[256];
static int g_stat_count[256], g_stat_n;
static void StatLineHook(int line) {
  if (line >= 1) {
    const Ppu *p = g_ppu;
    StatKey k = {p->bgmode, p->screenEnabled[0], p->screenEnabled[1], p->cgwsel, p->cgadsub,
                 p->screenWindowed[0], p->screenWindowed[1], p->m7sel, p->fixedColor, p->bgTileAdr};
    int i;
    for (i = 0; i < g_stat_n; i++) if (!memcmp(&g_stat_keys[i], &k, sizeof(k))) break;
    if (i == g_stat_n && g_stat_n < 256) g_stat_keys[g_stat_n++] = k;
    if (i < 256) g_stat_count[i]++;
  }
  ppu_runLine(g_ppu, line);
}

extern uint16_t g_smw_nds_force_level;

void PortPlatformFatal(const char *message) {
  fprintf(stderr, "FATAL: %s\n", message);
  exit(1);
}

static uint8_t *ReadFile(const char *path, size_t *size) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  *size = (size_t)ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *data = malloc(*size);
  if (fread(data, 1, *size, f) != *size) { free(data); data = NULL; }
  fclose(f);
  return data;
}

static uint8_t g_pixels[256 * 4 * 240];

static void WritePpm(const char *path, int height) {
  FILE *f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "P6\n256 %d\n255\n", height);
  for (int i = 0; i < 256 * height; i++) {
    const uint8_t *p = &g_pixels[i * 4]; /* B, G, R, X */
    uint8_t rgb[3] = { p[2], p[1], p[0] };
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}

static uint32_t ParseButtons(const char *s) {
  static const struct { const char *name; uint32_t bit; } kNames[] = {
    {"b", kPortPad_B}, {"y", kPortPad_Y}, {"select", kPortPad_Select},
    {"start", kPortPad_Start}, {"up", kPortPad_Up}, {"down", kPortPad_Down},
    {"left", kPortPad_Left}, {"right", kPortPad_Right}, {"a", kPortPad_A},
    {"x", kPortPad_X}, {"l", kPortPad_L}, {"r", kPortPad_R},
  };
  uint32_t r = 0;
  char buf[128];
  snprintf(buf, sizeof(buf), "%s", s);
  for (char *tok = strtok(buf, "+,\r\n "); tok; tok = strtok(NULL, "+,\r\n ")) {
    for (size_t i = 0; i < sizeof(kNames) / sizeof(kNames[0]); i++)
      if (strcmp(tok, kNames[i].name) == 0) r |= kNames[i].bit;
  }
  return r;
}

/* "pin ADDR VAL": keep a game-RAM byte at VAL every frame ("unpin ADDR"). */
static int g_pin_addr[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
static uint8_t g_pin_val[8];

static FILE *g_wav;
static uint32_t g_wav_samples;

static void RunOneFrame(uint32_t buttons) {
  for (int i = 0; i < 8; i++)
    if (g_pin_addr[i] >= 0) PortGameRam()[g_pin_addr[i]] = g_pin_val[i];
  PortRunFrame(buttons);
  if (g_wav) {  /* WAV=<file>: record 534 samples (32 kHz) per frame */
    int16_t buf[534 * 2];
    PortRenderAudio(buf, 534);
    fwrite(buf, 4, 534, g_wav);
    g_wav_samples += 534;
  }
}

int main(int argc, char **argv) {
  if (argc < 4) {
    fprintf(stderr, "usage: hostrun smw_assets.dat out_dir script.txt\n");
    return 2;
  }
  size_t size;
  uint8_t *assets = ReadFile(argv[1], &size);
  if (!assets) PortPlatformFatal("cannot read assets");
  const char *err = PortLoadAssets(assets, size);
  if (err) PortPlatformFatal(err);
  PortInitGame();
  if (getenv("LOADSAVE")) PortLoadSave();  /* saves/smw.srm in the current directory */
  if (getenv("WAV")) {
    g_wav = fopen(getenv("WAV"), "wb");
    fwrite("RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x02\0\0\x7d\0\0\0\xf4\x01\0\x04\0\x10\0data\0\0\0\0", 1, 44, g_wav);
    PortSetAudioMode(kPortAudio_Software);
  }

  FILE *script = fopen(argv[3], "r");
  if (!script) PortPlatformFatal("cannot read script");
  char line[256];
  int frame = 0, shot = 0;
  RunOneFrame(0);
  PortRenderSoftware(g_pixels, 256 * 4);
  frame++;
  while (fgets(line, sizeof(line), script)) {
    if (line[0] == '#' || line[0] == '\n') continue;
    if (strncmp(line, "shot", 4) == 0) {
      char path[512];  /* the last rendered frame; never render twice */
      snprintf(path, sizeof(path), "%s/shot%02d_f%05d.ppm", argv[2], shot++, frame);
      WritePpm(path, 224);
      printf("wrote %s\n", path);
      continue;
    }
    if (strncmp(line, "levelmodes", 10) == 0) {  /* level number -> header level mode */
      for (int r = 0; r < 0x200; r++) {
        MemBlk b = FindInAssetArray(43, r);
        if (b.size > 2 && (b.ptr[1] & 0x1f) != 0) printf(" lvl %03x mode %02x\n", r, b.ptr[1] & 0x1f);
      }
      continue;
    }
    if (strncmp(line, "level ", 6) == 0) {  /* "level XXX": entering a level loads level number XXX */
      g_smw_nds_force_level = 0x8000 | (uint16_t)strtoul(line + 6, NULL, 16);
      continue;
    }
    if (strncmp(line, "stat ", 5) == 0) {  /* "stat N [buttons]": run N frames, print register combos */
      int n = atoi(line + 5);
      const char *rest = strchr(line + 5, ' ');
      const uint32_t buttons = rest ? ParseButtons(rest + 1) : 0;
      g_stat_n = 0;
      memset(g_stat_count, 0, sizeof(g_stat_count));
      for (int i = 0; i < n; i++, frame++) {
        RunOneFrame(buttons);
        PpuBeginDrawing(g_my_ppu, g_pixels, 256 * 4, 0);
        PortRunPpuLines(&StatLineHook);
      }
      for (int i = 0; i < g_stat_n; i++) {
        const StatKey *k = &g_stat_keys[i];
        printf(" stat lines=%6d mode=%02x main=%02x sub=%02x cgwsel=%02x cgadsub=%02x mwin=%02x swin=%02x m7sel=%02x fixed=%04x nba=%04x\n",
               g_stat_count[i], k->mode, k->main, k->sub, k->cgwsel, k->cgadsub, k->mwin, k->swin, k->m7sel, k->fixed, k->nba);
      }
      continue;
    }
    if (strncmp(line, "lines", 5) == 0) {
      PortRunFrame(0);
      frame++;
      PortRunPpuLines(&DumpLineHook);  /* also renders, via the hook */
      continue;
    }
    if (strncmp(line, "pin ", 4) == 0 || strncmp(line, "unpin ", 6) == 0) {
      unsigned a = 0, v = 0;
      const bool pin = line[0] == 'p';
      sscanf(line + (pin ? 4 : 6), "%x %x", &a, &v);
      for (int i = 0; i < 8; i++) {
        if (pin ? g_pin_addr[i] < 0 : g_pin_addr[i] == (int)a) {
          g_pin_addr[i] = pin ? (int)a : -1;
          g_pin_val[i] = (uint8_t)v;
          break;
        }
      }
      continue;
    }
    if (strncmp(line, "until ", 6) == 0) {  /* "until ADDR VAL [buttons]": run until ram[ADDR]==VAL (max 20000) */
      unsigned a = 0, v = 0;
      char b[64] = "";
      sscanf(line + 6, "%x %x %63s", &a, &v, b);
      const uint32_t buttons = ParseButtons(b);
      int k = 0;
      while (PortGameRam()[a] != v && k++ < 20000) {
        RunOneFrame(buttons);
        PortRenderSoftware(g_pixels, 256 * 4);
        frame++;
      }
      printf("until %x==%x: frame %d%s\n", a, v, frame, k > 20000 ? " (TIMEOUT)" : "");
      continue;
    }
    if (strncmp(line, "cgram", 5) == 0) {  /* BG3 palettes 0-7 (colours 0-3) */
      for (int i = 0; i < 32; i++) printf("%s%04x", i % 4 ? " " : "\n  pal ", g_my_ppu->cgram[i]);
      printf("\n");
      continue;
    }
    if (strncmp(line, "playeroam", 9) == 0) {
      int px = (PortGameRam()[0x94] | PortGameRam()[0x95] << 8) - (PortGameRam()[0x1a] | PortGameRam()[0x1b] << 8);
      int py = (PortGameRam()[0x96] | PortGameRam()[0x97] << 8) - (PortGameRam()[0x1c] | PortGameRam()[0x1d] << 8);
      printf("player screen %d,%d\n", px, py);
      for (int i = 0; i < 128; i++) {
        int y = g_my_ppu->oam[i * 2] >> 8, x = g_my_ppu->oam[i * 2] & 0xff;
        if (abs(x - px) < 24 && abs(y - py) < 40)
          printf("  oam %3d x=%3d y=%3d tile=%03x attr=%02x\n", i, x, y, g_my_ppu->oam[i*2+1] & 0x1ff, g_my_ppu->oam[i*2+1] >> 8);
      }
      continue;
    }
    if (strncmp(line, "names", 5) == 0) {  /* print all level names */
      for (int lv = 0; lv < 0x60; lv++) {
        uint8_t c[40]; int n = PortLevelNameCodes((uint8_t)lv, c, 40);
        printf("%02x: ", lv);
        for (int i = 0; i < n; i++) {
          uint8_t k = c[i];
          printf("%c", k < 26 ? 'A' + k : k >= 0x40 && k < 0x5a ? 'a' + k - 0x40 : k == 0x1f ? ' ' : k == 0x1a ? '!' : k == 0x1b ? '.' : '?');
        }
        printf("  [%02x..]\n", n ? c[n-1] : 0);
      }
      continue;
    }
    if (strncmp(line, "vram", 4) == 0) {  /* "vram 5000 64": print words */
      unsigned a = 0, n = 0;
      sscanf(line + 4, "%x %u", &a, &n);
      for (unsigned i = 0; i < n; i++)
        printf("%s%04x", i % 32 ? " " : "\n  ", g_my_ppu->vram[(a + i) & 0x7fff]);
      printf("\n");
      continue;
    }
    if (strncmp(line, "oam", 3) == 0) {  /* sprites in the status bar area */
      Ppu *p = g_my_ppu;
      for (int i = 0; i < 128; i++) {
        int y = p->oam[i * 2] >> 8, x = p->oam[i * 2] & 0xff;
        int ext = p->highOam[i >> 2] >> ((i & 3) * 2);
        if (y < 40) printf("  oam %3d: x=%3d y=%3d tile=%03x attr=%02x big=%d\n", i, x | ((ext & 1) << 8), y,
                           (p->oam[i * 2 + 1] & 0xff) | ((p->oam[i * 2 + 1] & 0x100)), p->oam[i * 2 + 1] >> 8, (ext >> 1) & 1);
      }
      continue;
    }
    if (strncmp(line, "regs", 4) == 0) {  /* PPU registers after the frame */
      Ppu *p = g_my_ppu;
      printf("frame %d: inidisp=%02x bgmode=%02x obsel=%02x mosaic=%02x bgsc=%02x,%02x,%02x,%02x "
             "nba=%04x main=%02x sub=%02x win=%02x/%02x cgwsel=%02x cgadsub=%02x "
             "fixed=%04x setini=%02x w1=%d-%d w2=%d-%d wsel=%06x\n  scroll:",
             frame, p->inidisp, p->bgmode, p->obsel, p->mosaic, p->bgXsc[0], p->bgXsc[1],
             p->bgXsc[2], p->bgXsc[3], p->bgTileAdr, p->screenEnabled[0], p->screenEnabled[1],
             p->screenWindowed[0], p->screenWindowed[1], p->cgwsel, p->cgadsub, p->fixedColor,
             p->setini, p->window1left, p->window1right, p->window2left, p->window2right,
             (unsigned)p->windowsel);
      for (int i = 0; i < 4; i++) printf(" bg%d=%d,%d", i + 1, p->hScroll[i], p->vScroll[i]);
      printf(" cgram0=%04x", p->cgram[0]);
      printf("\n");
      continue;
    }
    if (strncmp(line, "ram", 3) == 0) {  /* "ram 13 109 1df5": print bytes */
      printf("frame %d:", frame);
      for (char *tok = strtok(line + 3, " \r\n"); tok; tok = strtok(NULL, " \r\n")) {
        unsigned addr = (unsigned)strtoul(tok, NULL, 16);
        printf(" [%x]=%02x", addr, PortGameRam()[addr & 0x1ffff]);
      }
      printf("\n");
      continue;
    }
    int n = atoi(line);
    const char *rest = strchr(line, ' ');
    uint32_t buttons = rest ? ParseButtons(rest + 1) : 0;
    for (int i = 0; i < n; i++, frame++) {
      RunOneFrame(buttons);
      PortRenderSoftware(g_pixels, 256 * 4); /* exactly once per frame */
    }
  }
  fclose(script);
  if (g_wav) {
    uint32_t data = g_wav_samples * 4, riff = data + 36;
    fseek(g_wav, 4, SEEK_SET); fwrite(&riff, 4, 1, g_wav);
    fseek(g_wav, 40, SEEK_SET); fwrite(&data, 4, 1, g_wav);
    fclose(g_wav);
  }
  printf("ran %d frames\n", frame);
  return 0;
}
