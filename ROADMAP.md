# Roadmap

Target: a faithful native port of Super Mario World for the DS Lite (R4
flashcart). Top screen: the game, pixel-for-pixel, with the camera crop
hiding 32 of the SNES's 224 lines. Bottom screen: status bar, reserve item
and touch features.

## History

- **v0.0.x (Oct 26, 2026, ChatGPT Codex):** DS hardware bring-up (colour bars,
  buttons, SD-card ROM check) confirmed on DS Lite + R4. Explored the
  SuperMarioWorldRecomp static recompiler; its generated code was ~9 MB,
  more than twice the DS's 4 MB of RAM.
- **v0.1.0 (Oct 27, 2026):** switched to snesrev/smw, a hand-written C
  reimplementation (~0.5 MB of ARM code for the whole game). Fixed ARM9
  alignment issues, ran the game on DS with a software renderer
  (correct but ~7 fps).
- **v0.2.0 (Oct 27, 2026):** hardware renderer using the DS 2D engine: full
  60 fps with ~5 ms of game logic per frame and <1 ms of VBlank upload.
  Confirmed at a stable 60 fps on a real DS Lite + R4.
- **v0.3.0 (Oct 28, 2026):** SNES windows (message boxes were invisible
  before, which looked like a freeze), bottom-screen status bar with a big
  touchable reserve-item box, FPS counter.
- **v0.4.0 (Oct 28, 2026):** audio. Software emulation of the SNES sound
  chip took more than a whole frame on the ARM9, so instead the SNES
  voices are mapped onto DS hardware channels: the game's sound driver
  and the chip's envelopes run as before, BRR samples are decoded to PCM
  once (cached), and DS channels follow pitch/volume/pan each frame.
  Pitch matches the SNES reference exactly; ~60% CPU in YI1 at 60 fps.
- **v0.4.1 (Oct 28, 2026):** redesigned bottom screen: hand-drawn SMW-style
  scene with panels, level name, 2x counters, item box, progress track.
- **v0.4.2 (Oct 28, 2026):** saving fixed on real hardware. The save at the
  Yellow Switch Palace never reached Liz's SD card (the old write renamed
  the file to a backup first). Saves are now written in place, synced and
  read back to verify, with a "Game saved!" / "Save failed!" message.
  Verified in melonDS with a bot run: YI1 -> Yellow Switch Palace -> save
  -> restart -> file shows 2 exits and loads there.
- **v0.4.3 (Oct 28, 2026):** fixed brief grey "static" flashes on the top
  screen. Big graphics changes (scene changes) were converted during
  VBlank and could run into the visible picture. Conversion now happens
  before VBlank into RAM copies; VBlank only copies changed 32-byte chunks
  (a full ~150 KB refresh falls back to one black frame). melonDS bot run:
  no uploads past VBlank during play.
- **v0.4.4 (Oct 28, 2026):** saving, take 3: on Liz's DS the only write
  that ever reached the card was creating a new file; overwriting an
  existing one never did. Saves now alternate between two files, each
  deleted and created fresh, with a counter so the newest valid one loads.
  Also: the status bar no longer flashes at the top while a level fades in
  or out, and the rows above the status-bar split show the level.
  Saving confirmed working on Liz's DS Lite + R4.
- **v0.4.5 (Oct 28, 2026):** the real cause of the grey "static" flashes.
  The per-line scroll/backdrop/window values are fed by HBlank DMA from a
  table that must be restarted every VBlank. When a frame occasionally ran
  long (a missed VBlank, every 10-15 s on hardware), the restart was late
  and the DMA ran past the end of the table, filling one picture with
  garbage. An interrupt at line 192 now restarts it every VBlank, so a slow
  frame just repeats the previous picture. Reproduced and verified in
  melonDS with a test build that forces a missed VBlank every 8 frames.
- **v0.5.0 (Oct 28, 2026):** smart camera; mode 7 boss rooms (DS rotation
  background, per-line registers, pixel-exact against snesrev's renderer in
  melonDS); colour math (translucent layer 3, sprite math); echo via the
  DS sound capture units; map lives icon fixed (OAM priority rotation).
  New test build (SMW_NDS_TEST) renders the SNES reference picture in the
  same pass, with level warp, for automatic comparisons.
- **v0.5.1 (Oct 29, 2026):** fixes from the automated sweep (every level
  number, all 7 maps, title/demo, the 7 castle cutscenes and the whole
  ending compared with the reference picture): mosaic fades, crop during
  pipe/door fades, ghost house lighting (half colour math), dark rooms
  (fixed-colour subtract), layer priorities (enemy roll call names,
  sinking castles, map sprites behind the border).

## Next milestones

| # | Milestone | Done when |
|---|---|---|
| 1 | Real-hardware check | Done: 60 fps and saving confirmed on DS Lite + R4. |
| 2 | Bottom screen HUD | Done in v0.3.0. |
| 3 | Audio | Done in v0.4.0 (no echo yet). Echo/reverb via DS sound capture is a follow-up. |
| 4 | Smart camera | Done in v0.5.0. |
| 5 | Remaining SNES effects | Done in v0.5.0. |
| 6 | Touch menu | Options/save menu on the bottom screen. |
| 7 | Full playthrough | In progress: automated sweep of all levels, maps, cutscenes and the ending; then hardware. |
| 8 | Release polish | Options, credits, build instructions, known-issues list. |

## Testing approach

- `tools/hostrun`: runs the game core on a PC with scripted input; with
  `-fsanitize=alignment` it finds memory accesses the ARM9 can't do.
- `tools/dstest`: runs the real `.nds` in the melonDS emulator core, with
  scripted buttons and a virtual SD card, saving screenshots of both screens.
- Real DS Lite + R4 for final checks.
