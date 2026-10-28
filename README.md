# Super Mario World DS

A native Nintendo DS port of *Super Mario World*, made for the DS Lite with
an R4-style flashcart. It is not an emulator: the game's own logic runs as
native ARM code, and the picture is drawn by the DS's 2D graphics hardware.
The top screen shows the game; the bottom screen will hold the status bar
and touch features.

## Current state (v0.2.0)

- Boots to the title screen, menus, overworld and levels at full speed
  (60 fps) in the melonDS emulator. Real DS Lite testing in progress.
- Hardware renderer: SNES tiles, maps, palettes and sprites are converted
  to the DS 2D engine each frame; scanline effects use HBlank DMA.
- Silent for now (the sound driver runs, but no audio output yet).
- Saves are written to `/_nds/smw-nds/saves/smw.srm` on the SD card.
- Not yet: status bar on the bottom screen, touch controls, mode 7 scenes
  (some boss fights), window effects (message box shape, circle wipes).

See [the roadmap](ROADMAP.md) for what's next.

## How it works

| Folder | What it is |
|---|---|
| `src/` | [snesrev/smw](https://github.com/snesrev/smw) (MIT): a complete C reimplementation of the game logic. DS changes are marked `smw-nds`. |
| `port/` | Platform-neutral glue: game data, input, frame stepping. |
| `nds/` | DS program: entry point, timing, hardware renderer. |
| `assets/` | snesrev's tool that extracts graphics/levels from **your own ROM**. |
| `tools/hostrun/` | Desktop test harness: runs the same game core with scripted input and saves screenshots. |
| `tools/dstest/` | Headless DS test runner built on the melonDS core. |

## Building

1. Install devkitPro's **NDS Development** package (Windows: to `C:\devkitPro`)
   and Python 3.
2. Copy your Super Mario World (USA) ROM to `roms\smw.sfc`
   (unheadered, CRC32 `B19ED489`).
3. Double-click `build.bat`. The first run extracts the game data from your
   ROM into `nds\data\` (kept on your PC only).
4. Copy `nds\smw-nds.nds` to your flashcart's microSD card.

The GitHub Actions build checks that everything compiles. Because it never
sees a ROM, its `.nds` has no game data built in and looks for
`smw_assets.dat` on the SD card instead.

## ROM and copyright

This repository contains no Nintendo data: no ROM, no extracted graphics,
levels or music, and no `.nds` built with game data. You need your own
legally obtained copy of the game. This is an independent homebrew project
and is not affiliated with Nintendo.
