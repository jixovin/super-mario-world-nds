# Super Mario World DS

A native Nintendo DS port of *Super Mario World*, made for the DS Lite with
an R4-style flashcart. It is not an emulator: the game's own logic runs as
native ARM code, and the picture is drawn by the DS's 2D graphics hardware.
The top screen shows the game; the bottom screen shows the status screen
and touch features.

## Current state (v0.6.0)

- Runs at full speed (60 fps) on a real DS Lite + R4 (tested through most
  of Yoshi's Island 1).
- Hardware renderer: SNES tiles, maps, palettes and sprites are converted
  to the DS 2D engine each frame; scanline effects use HBlank DMA.
- Music and sound effects: the game's own sound driver runs, and the SNES
  sound chip's 8 voices are played on the DS's hardware sound channels
  (samples decoded from the SNES format once and cached). No echo yet.
- Bottom screen: a game-themed status screen (sky, hills and panels) with
  the level name, big lives/coins/time/score counters drawn with the
  game's own digits and icons, a large reserve-item box (tap it to use the
  item), a level-progress track with Mario on it, and an FPS counter (tap
  it to hide or show).
- SNES window effects: message boxes, keyhole and circle wipes.
- Saves go to `/_nds/smw-nds/saves/` on the SD card, alternating between
  `smw-a.srm` and `smw-b.srm` (each written as a new file and checked by
  reading it back; the newest valid one is loaded), with "Game saved!"
  shown on the bottom screen. Older `smw.srm` saves are still loaded.
- Smart camera: in levels the top screen slides up to keep Mario and what's
  above him in view.
- Mode 7 boss rooms (Iggy/Larry, Morton/Roy/Ludwig, Reznor, Bowser),
  translucent effects (ghost house fog etc.) and music echo.
- Every pipe and door in the game leads where it should (v0.6.0 fixed the
  data for exits that go back into a level, e.g. Yoshi's Island 4's pipe).
- Checked automatically: every level, map, castle cutscene and the ending
  against the reference picture; every pipe/door exit two deep, and
  whole level runs, against the original game code (see tools/).

See [the roadmap](ROADMAP.md) for what's next.

## How it works

| Folder | What it is |
|---|---|
| `src/` | [snesrev/smw](https://github.com/snesrev/smw) (MIT): a complete C reimplementation of the game logic. DS changes are marked `smw-nds`. |
| `port/` | Platform-neutral glue: game data, input, frame stepping. |
| `nds/` | DS program: entry point, timing, hardware renderer (top screen), HUD (bottom screen). |
| `assets/` | snesrev's tool that extracts graphics/levels from **your own ROM**. |
| `tools/hostrun/` | Desktop test harness: runs the same game core with scripted input and saves screenshots. |
| `tools/dstest/` | Headless DS test runner built on the melonDS core. |

## Building

1. Install devkitPro's **NDS Development** package (Windows: to `C:\devkitPro`)
   and Python 3.
2. Copy your Super Mario World (USA) ROM to `roms\smw.sfc`
   (unheadered, CRC32 `B19ED489`).
3. Double-click `build.bat`. The first run extracts the game data from your
   ROM into `nds\data\` (kept on your PC only). It is made again
   automatically when a new version needs different data.
4. Copy `nds\smw-nds.nds` to your flashcart's microSD card.

The GitHub Actions build checks that everything compiles. Because it never
sees a ROM, its `.nds` has no game data built in and looks for
`smw_assets.dat` on the SD card instead.

## ROM and copyright

This repository contains no Nintendo data: no ROM, no extracted graphics,
levels or music, and no `.nds` built with game data. You need your own
legally obtained copy of the game. This is an independent homebrew project
and is not affiliated with Nintendo.
