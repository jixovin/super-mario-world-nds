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
             "w1=%d-%d w2=%d-%d wlog=%04x cgwsel=%02x cgadsub=%02x fixed=%04x bg3=%d,%d bg1=%d,%d\n",
             line, p->inidisp, p->bgmode, p->screenEnabled[0], p->screenEnabled[1],
             p->screenWindowed[0], p->screenWindowed[1], (unsigned)p->windowsel,
             p->window1left, p->window1right, p->window2left, p->window2right, p->wbgobjlog,
             p->cgwsel, p->cgadsub, p->fixedColor, p->hScroll[2], p->vScroll[2],
             p->hScroll[0], p->vScroll[0]);
      memcpy(g_prev_regs, r, PPU_SAVESTATE_REGS_SIZE);
    }
  }
  ppu_runLine(g_ppu, line);
}

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

  FILE *script = fopen(argv[3], "r");
  if (!script) PortPlatformFatal("cannot read script");
  char line[256];
  int frame = 0, shot = 0;
  PortRunFrame(0);
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
    if (strncmp(line, "lines", 5) == 0) {
      PortRunFrame(0);
      frame++;
      PortRunPpuLines(&DumpLineHook);  /* also renders, via the hook */
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
      PortRunFrame(buttons);
      PortRenderSoftware(g_pixels, 256 * 4); /* exactly once per frame */
    }
  }
  fclose(script);
  printf("ran %d frames\n", frame);
  return 0;
}
