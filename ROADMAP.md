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

## Next milestones

| # | Milestone | Done when |
|---|---|---|
| 1 | Real-hardware check | v0.2.0 runs on Liz's DS Lite + R4; controls feel right; saves persist. |
| 2 | Bottom screen HUD | Status bar (lives, coins, time, score, stars) and reserve item drawn on the bottom screen; top screen shows the full play area. |
| 3 | Touch features | Tap the reserve item to drop it; touch menu (save/options). |
| 4 | Smart camera | Vertical crop follows Mario so nothing important is hidden. |
| 5 | Remaining SNES effects | Windows (message box, keyhole, circle wipes), colour math (translucent layers), mode 7 (Bowser, Iggy/Larry, Reznor, etc.). |
| 6 | Audio | Music and sound effects (SPC700 driver + DSP on the DS, likely using the ARM7). |
| 7 | Full playthrough | Every world, castle and the ending checked on hardware; performance tuned. |
| 8 | Release polish | Options, credits, build instructions, known-issues list. |

## Testing approach

- `tools/hostrun`: runs the game core on a PC with scripted input; with
  `-fsanitize=alignment` it finds memory accesses the ARM9 can't do.
- `tools/dstest`: runs the real `.nds` in the melonDS emulator core, with
  scripted buttons and a virtual SD card, saving screenshots of both screens.
- Real DS Lite + R4 for final checks.
