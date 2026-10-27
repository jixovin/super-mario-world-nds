# Super Mario World DS port

A native Nintendo DS port of *Super Mario World*, aimed first at DS Lite and
R4 flashcart testing. The planned layout uses the top screen for gameplay and
the lower screen for a compact HUD, map, and useful status information.

## Current state

The upstream v0.15.1 recompilation has been checked out locally as a reference.
The first DS target is a small hardware bring-up program that checks dual-screen
output, color conversion, frame scaling, and button input. It is not playable
yet. The DS target will reuse the recompiled game logic where practical, but it
needs a new NDS runtime, software renderer, input layer, audio path, and save
storage. The Windows executable supplied as a reference cannot run on a DS.

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


