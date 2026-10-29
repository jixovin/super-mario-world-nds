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

## Next milestones

| # | Milestone | Done when |
|---|---|---|
| 1 | Real-hardware check | 60 fps confirmed. Saving fixed in v0.4.2; to confirm on DS Lite. |
| 2 | Bottom screen HUD | Done in v0.3.0. |
| 3 | Audio | Done in v0.4.0 (no echo yet). Echo/reverb via DS sound capture is a follow-up. |
| 4 | Smart camera | Vertical crop follows Mario so nothing important is hidden. |
| 5 | Remaining SNES effects | Windows (message box, keyhole, circle wipes), colour math (translucent layers), mode 7 (Bowser, Iggy/Larry, Reznor, etc.). |
| 6 | Touch menu | Options/save menu on the bottom screen. |
| 7 | Full playthrough | Every world, castle and the ending checked on hardware; performance tuned. |
| 8 | Release polish | Options, credits, build instructions, known-issues list. |

## Testing approach

- `tools/hostrun`: runs the game core on a PC with scripted input; with
  `-fsanitize=alignment` it finds memory accesses the ARM9 can't do.
- `tools/dstest`: runs the real `.nds` in the melonDS emulator core, with
  scripted buttons and a virtual SD card, saving screenshots of both screens.
- Real DS Lite + R4 for final checks.
