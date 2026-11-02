/* smw-nds: platform-neutral glue between the snesrev/smw game code and a
 * host platform (the DS build, or the desktop test harness).
 *
 * The game logic in src/ is a C reimplementation of the original game.
 * It runs natively; there is no CPU emulation in this build.
 */
#ifndef SMW_PORT_H
#define SMW_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* SNES joypad bits, in the order the game code expects. */
enum {
  kPortPad_B      = 1u << 0,
  kPortPad_Y      = 1u << 1,
  kPortPad_Select = 1u << 2,
  kPortPad_Start  = 1u << 3,
  kPortPad_Up     = 1u << 4,
  kPortPad_Down   = 1u << 5,
  kPortPad_Left   = 1u << 6,
  kPortPad_Right  = 1u << 7,
  kPortPad_A      = 1u << 8,
  kPortPad_X      = 1u << 9,
  kPortPad_L      = 1u << 10,
  kPortPad_R      = 1u << 11,
};

/* Platform hook: report a fatal error and never return. */
void PortPlatformFatal(const char *message);

/* Index an smw_assets.dat image (kept in memory for the program's lifetime).
 * Returns NULL on success or an error message. */
const char *PortLoadAssets(const uint8_t *data, size_t length);

/* Start the game with no ROM attached (native code only). */
void PortInitGame(void);
void PortInitGameFromRom(const uint8_t *rom, size_t size);

/* Load saves/smw.srm (relative to the current directory) if it exists.
 * The game writes the file itself when it saves. */
void PortLoadSave(void);

/* How many times the game has saved, and whether the last save was written
 * and read back correctly (for an on-screen "Game saved!" message). */
int PortSaveStatus(bool *ok);

/* Run one 60 Hz game frame with the given kPortPad_* buttons held. */
void PortRunFrame(uint32_t buttons);

/* Audio modes. The game's sound driver always runs (the game waits on its
 * replies); the mode decides what produces the sound:
 *   kPortAudio_Silent:   nothing.
 *   kPortAudio_Software: SNES DSP synthesis; call PortRenderAudio exactly
 *                        once per game frame, after PortRunFrame.
 *   kPortAudio_Hardware: only the DSP envelopes run inside PortRunFrame;
 *                        the platform plays the voices itself using
 *                        PortDsp() / PortSpcRam(). */
enum { kPortAudio_Silent, kPortAudio_Software, kPortAudio_Hardware };
void PortSetAudioMode(int mode);
void PortRenderAudio(int16_t *stereo, int samples);
struct Dsp *PortDsp(void);
const uint8_t *PortSpcRam(void);

/* Software-render the current frame into a 256x224 (or 240) XRGB8888
 * buffer. Used by the desktop harness and by the first DS bring-up. */
void PortRenderSoftware(uint8_t *pixels, size_t pitch);

/* Hardware-renderer support: run the SNES frame's per-line pass (HDMA and
 * the status-bar IRQ run as on the SNES) calling hook(line) for lines
 * 0..224 instead of drawing pixels. Line L (1..224) is screen row L-1. */
typedef void PortLineHook(int line);
void PortRunPpuLines(PortLineHook *hook);

/* The SNES PPU state (registers, VRAM, CGRAM, OAM) the game writes to. */
struct Ppu *PortPpu(void);

/* The name of level `translevel` (e.g. YOSHI'S ISLAND 1) as the game's own
 * message-font tile codes (tile 0x100 + code; 0x1f is a space). Returns
 * the number of codes written. */
int PortLevelNameCodes(uint8_t translevel, uint8_t *codes, int max);

/* Direct access to game RAM ($7E0000-$7FFFFF), for HUD/bottom-screen use. */
uint8_t *PortGameRam(void);

#endif
