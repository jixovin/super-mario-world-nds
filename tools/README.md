# Developer tools

None of these are needed to build or play the game.

| Folder | What it is |
|---|---|
| `hostrun/` | Desktop test harness: runs the same game core on Linux/macOS/MSYS2 with scripted input, saves screenshots, and can compare every frame with the original game code (`THEIRS=<rom> SMW_COMPARE=1`). Build with `make`. |
| `dstest/` | Headless DS test runner on the [melonDS](https://github.com/melonDS-emu/melonDS) core (`build.sh` clones and builds it), plus scripts that warp into levels and compare the DS picture with the reference picture of a `SMW_NDS_TEST` build. Test files live in `$SMW_TEST_DIR` (default `/tmp/emu`): `test.nds`/`test.map` from `dstest_build.sh`, and `sd/smw_assets.dat` made from your ROM. |
| `saves/` | `make_save.py` writes a test save (`smw-a.srm`) with the whole map unlocked and the castles still standing. |

The cheat test build (`make EXTRA_DEFS=-DSMW_NDS_CHEATS` in `nds/`) and the
test saves are for testing only; never use them for a release.

`dstest/melonds_headless.cpp` links against the melonDS core, which is
GPL-3.0, so the built runner is GPL-3.0 as well. It is a test tool only and is
not part of the game.
