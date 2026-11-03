# Super Mario World DS

A native Nintendo DS port of *Super Mario World*, made for the DS Lite with
an R4-style flashcart. It is not an emulator: the game's own logic runs as native ARM code, and the
picture is drawn by the DS's 2D graphics hardware.

**No game data is included.** You make it from your own copy of the game
(see [Getting the game running](#getting-the-game-running)).

## What you get

- The whole game, from the title screen to the ending: every level, map,
  secret exit, castle, boss (including the Mode 7 bosses) and cutscene.
- Full speed (60 fps) on a real DS Lite, with the game's music and sound
  effects (including echo) played on the DS's sound hardware.
- **Top screen:** the game at 1:1 pixels. The DS screen is 32 lines shorter
  than the SNES picture, so in levels a smart camera slides up and down to
  keep Mario and what's around him in view.
- **Bottom screen:** a pixel-art status screen whose background changes with
  where you are: the overworld map (coloured per world), or the level's
  setting (grass, sky, cave, underwater, castle over lava, ghost house,
  forest, ice, night/Star Road, switch palaces in their own colour,
  Chocolate Island). On it:
  - the level name, in the game's own font;
  - big lives, coins, time and score counters, in the game's own digits;
  - a big reserve-item box: **tap it to use your stored item**;
  - Mario walking along the ground towards the goal posts as you get
    through the level;
  - an FPS counter (tap it to hide or show).
- Saving to the microSD card (`/_nds/smw-nds/saves/`), with
  "Game saved!" shown on the bottom screen.
- Its own icon and name in TWiLight Menu++ and the DS menu.

### Controls

The DS buttons match the SNES pad: D-pad, A, B, X, Y, L, R, Start, Select.
Tapping the item box on the bottom screen also drops your reserve item.

### Known small differences

The game runs the original game logic, and every level, map and cutscene
was checked picture by picture against a reference render. These effects
are close approximations rather than exact:

- The SNES's limit of 32 sprites per line is not copied, so in a few busy
  spots sprites that would flicker on a SNES stay visible.
- In two dark rooms with a spotlight, the area outside the light is dimmed
  slightly differently.
- In one Boo room, a see-through Boo is blended with a different layer.
- The map fades in after losing a life about one brightness step differently.

## Getting the game running

There are two ways. Both need your own **Super Mario World (USA)** ROM
(`.sfc` or `.smc`, with or without the 512-byte header).

### Option A: ready-made .nds + your game data (no compiler needed)

1. Install [Python 3](https://www.python.org/downloads/) (on Windows, tick
   "Add python.exe to PATH" in the installer).
2. Download this repository (green **Code** button → **Download ZIP**) and
   unzip it.
3. Put your ROM in the `roms` folder, then double-click **`make_data.bat`**
   (or drag your ROM onto it). It makes `smw_assets.dat` next to it.
   On macOS/Linux: `python3 assets/restool.py -r path/to/rom.sfc --no-include-rom`.
4. On your microSD card, make the folder `_nds/smw-nds/` and copy
   `smw_assets.dat` into it.
5. Copy `smw-nds.nds` from the [Releases](../../releases) page to the card,
   and start it from your flashcart's menu.

### Option B: build it yourself (Windows)

1. Install devkitPro with the **NDS Development** option, to the default
   `C:\devkitPro` ([installer](https://github.com/devkitPro/installer/releases)),
   and [Python 3](https://www.python.org/downloads/).
2. Download this repository and put your ROM in the `roms` folder.
3. Double-click **`build.bat`**. The first run extracts the game data from
   your ROM into `nds\data\` (it stays on your PC), then builds.
4. Copy `nds\smw-nds.nds` to your microSD card. This build has the game
   data inside it, so it needs nothing else on the card.

On macOS/Linux with devkitPro installed: run the `restool.py` command from
Option A inside `nds/data/`, rename the result to `smw_assets.bin`, then run
`make` in `nds/`.

**Don't share a `.nds` you built yourself or your `smw_assets.dat`**: they
contain data from your ROM.

### Troubleshooting

| Message | Fix |
|---|---|
| `ROM with hash ... not supported` | The ROM isn't Super Mario World (USA). Other versions and ROM hacks aren't supported. |
| `Python 3 is needed` | Install Python 3 and tick "Add python.exe to PATH". |
| `devkitPro was not found` | Install devkitPro to `C:\devkitPro` with NDS Development ticked (Option B only). |
| On the DS: `Game data not found` | Copy `smw_assets.dat` to `/_nds/smw-nds/` on the card (Option A). |
| On the DS: `smw_assets.dat is from an older version` | Make it again with `make_data.bat` from this version. |

## How it works

| Folder | What it is |
|---|---|
| `src/` | [snesrev/smw](https://github.com/snesrev/smw): a complete C reimplementation of the game logic. DS changes are marked `smw-nds`. |
| `port/` | Platform-neutral glue: game data, input, saves, frame stepping. |
| `nds/` | DS program: timing, the hardware renderer (top screen) that turns SNES tiles, sprites, scrolling, Mode 7 and colour effects into DS 2D-engine setup each frame, the sound driver bridge, and the bottom screen. |
| `assets/` | snesrev's tool that extracts graphics, levels and music from **your own ROM**. |
| `tools/` | Developer test tools (see [tools/README.md](tools/README.md)). |

See [the roadmap](ROADMAP.md) for the version history.

## Credits

- **jixovin**: project lead and hardware testing.
- **[snesrev](https://github.com/snesrev/smw)**: the C reimplementation of
  Super Mario World this port is built on.
- **Claude** (Anthropic): wrote the DS port code.
- **devkitPro / libnds** and **melonDS** (used for testing).

## License and copyright

The code is under the [MIT License](LICENSE); `src/` and `assets/` keep
snesrev's MIT license ([src/LICENSE-snesrev.txt](src/LICENSE-snesrev.txt)).

This repository contains no Nintendo data: no ROM, no extracted graphics,
levels or music, and no `.nds` built with game data. You need your own
legally obtained copy of the game. *Super Mario World* is a trademark of
Nintendo. This is an independent, unofficial homebrew project, not
affiliated with or endorsed by Nintendo.
