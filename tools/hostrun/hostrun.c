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
