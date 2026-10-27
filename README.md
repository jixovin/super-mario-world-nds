# Super Mario World DS port

A native Nintendo DS port of *Super Mario World*, aimed first at DS Lite and
R4 flashcart testing. The planned layout uses the top screen for gameplay and
the lower screen for a compact HUD, map, and useful status information.

## Current state

The initial dual-screen/input diagnostic boots on a DS Lite with an R4. The
upstream analyzer is installed and has generated 65816-to-C game code locally
from the supplied USA ROM. That generated code is not yet linked into the NDS
target or playable. The upstream desktop runtime depends on SDL/OpenGL and
host-side services, so the next step is identifying and replacing the minimum
runtime pieces needed to boot the original game logic on ARM9. The Windows
executable supplied as a reference cannot run on a DS.

See [the roadmap](ROADMAP.md) for the staged plan and estimates.

## Build the DS bring-up target

Install the official devkitPro NDS Development package, including devkitARM,
libnds, and `ndstool`. In a devkitPro/MSYS2 terminal, from this directory:

```sh
cd nds
make
```

The output is `nds/build/smw-nds.nds`. Copy that file to the microSD card used
by the R4 and launch it from the DS Lite. The current target is a screen/input
diagnostic, not the game. Once devkitPro is installed, this executable is the
first hardware check before integrating SMW.

## Source and ROM handling

The local `reference/` directory contains a separate checkout of
[SuperMarioWorldRecomp v0.15.1](https://github.com/mstan/SuperMarioWorldRecomp)
and is intentionally excluded from this repository. Its code uses the
[PolyForm Noncommercial License 1.0.0](https://polyformproject.org/licenses/noncommercial/1.0.0/).
The upstream recompiler can generate C from a legally obtained USA `.sfc` ROM,
but generated game code and the ROM are local build inputs and must not be
committed here. This repository does not include Nintendo game data, the ROM,
generated ROM-derived C, or the upstream checkout.

This is an independent homebrew project and is not affiliated with Nintendo.


