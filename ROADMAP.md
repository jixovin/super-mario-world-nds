# Development roadmap and time estimates

## Scope and estimate basis

The target is a native `.nds` program for DS Lite/R4, with the main game view
on the top screen and useful HUD/map information on the bottom screen. The
first playable milestone is the opening section through the first castle; the
long-term goal is the complete original single-player route through Bowser and
the ending. Postgame, online play, and features that require Nintendo network
services are out of scope.

These are engineering-effort estimates, not promises. They assume one
experienced NDS/C programmer doing most implementation with occasional help
from the project owner, who can spend up to about seven hours per week on
setup, testing, feedback, and art. The attached v0.15.1 Windows package is not
the DS source/runtime. The local source checkout is a static recompilation:
the 65816 game code is translated to C, while SNES video, audio, and hardware
behavior are supplied by a large C runtime. The DS must replace or substantially
adapt desktop presentation, input, audio, timing, filesystem, and parts of the
rendering/runtime. That platform work is the main uncertainty.

Calendar estimates assume the owner's seven-hour weekly availability is the
project's effective iteration cadence. The ranges include time to run a build
on the flashcart, report what happened, and adjust. They can move sharply if
the existing runtime is too slow on the DS ARM9 or if a game feature needs
custom rendering.

## Progress so far — October 26, 2026

- Installed the official devkitPro NDS package, including devkitARM, libnds,
  and `ndstool`.
- Built `nds/build/smw-nds.nds`, a two-screen color/scaling and input
  diagnostic and confirmed it boots on the DS Lite/R4: the top-screen color
  bars display, button input registers on the lower screen, and START exits.
- Checked out upstream SuperMarioWorldRecomp v0.15.1 locally. The Windows
  package is not portable game source. The local analyzer has now been built
  successfully with Rust, MSVC, and the Windows SDK. The supplied USA ROM was
  read locally and the upstream generator successfully emitted ROM-derived C:
  17 banks, 8,076 analysis roots, 8,276 exact variants, and 11,987 edges.
  Generated C totals about 68.3 MB across 102 C files, plus a 6.2 MB manifest.
  The ROM, generated C, and upstream checkout remain local and ignored by Git.
- Cross-compiled all 102 full-set generated C units for the DS ARM946E-S with
  devkitARM at `-Os`. Their object files total 14.1 MB and their code sections
  total 9.06 MB, before adding the runtime. A monolithic full-game AOT link
  therefore cannot fit in the DS's 4 MB main RAM; the project needs selective
  AOT, code overlays, or another way to keep only part of the generated code
  resident.
- Ran a separate reset-vector-only generation spike: 9 roots, 389 exact
  variants (339 AOT and 50 interpreter-only), and 11 generated C units. Those
  units compile to 1.24 MB of ARM code. This is a promising size for a boot
  experiment, but it has not yet been linked or shown to reach gameplay.
- Also confirmed that the CPU state/infrastructure, 65816 interpreter bridge,
  SNES CPU/PPU pieces, game initialization, game renderer/video, and SPC player
  sources compile for ARM9. The upstream `common_rtl.c` needed a temporary
  NDS-side config-header shim to avoid its desktop SDL include; the actual NDS
  runtime is not linked yet.
- The generated C is only the translated 65816 game code. It depends on the
  upstream SNES CPU/runtime interfaces, while the upstream desktop game target
  also brings SDL, OpenGL, desktop threading, and host-side services. None of
  that has been linked into an NDS game yet. The next engineering task is to
  isolate the minimum CPU/game runtime and video hooks needed for one booted
  frame; the generated source volume alone does not tell us the final `.nds`
  size or prove that the full runtime fits the DS.
- Created the [`jixovin/super-mario-world-nds`](https://github.com/jixovin/super-mario-world-nds)
  repository and published the initial source and project documentation. ROM
  data, generated game C, upstream code, and the `.nds` build are excluded.

## Stages

| Stage | Deliverable and exit check | Engineering effort | Approx. elapsed time |
|---|---|---:|---:|
| 0. Toolchain and clean project setup | Repository has only project code/docs; devkitARM, libnds, and `ndstool` work; ROM and generated outputs are excluded. | 4–10 hours | 1–2 weeks |
| 1. DS hardware bring-up | A bootable `.nds` displays correctly on both screens, verifies color conversion/scaling, reads buttons, and exits or resets cleanly. | 8–18 hours | 2–3 weeks |
| 2. Recompiler and runtime inventory | Identify the smallest set of upstream generated/runtime files needed for one game frame; record dependencies and licensing; generate local C from the supplied USA ROM without adding it to Git. | 12–30 hours | 2–5 weeks |
| 3. CPU/game-state integration | Boot SMW logic on ARM9; reset/title flow advances; input reaches the original joypad state; detect crashes and unsupported desktop assumptions. | 30–70 hours | 5–10 weeks |
| 4. First visible gameplay | Render a stable, correct 256×192 image from the SNES frame; play the first level with tilemaps, sprites, scrolling, doors, and transitions. | 40–90 hours | 6–13 weeks |
| 5. Playable opening milestone | Finish the opening route through the first castle with reliable controls, deaths/respawns, power-ups, Yoshi where encountered, and level exits. | 35–80 hours | 5–12 weeks |
| 6. Bottom-screen interface | Add game-relevant HUD and a useful map/status view; ensure the bottom screen updates without slowing the top-screen game. | 20–45 hours | 3–7 weeks |
| 7. Audio, saves, and reliability | Add SNES audio or a carefully chosen fallback, save-file support on R4, reset behavior, and recovery from bad saves. | 30–80 hours | 5–12 weeks |
| 8. Performance and compatibility | Profile on hardware; reach a stable target of 20–30 fps if feasible; reduce memory/CPU use; test emulator and real DS Lite/R4. | 30–90 hours | 5–13 weeks |
| 9. Complete main story | Complete and tune all seven main worlds, castles, Bowser's castle, ending, save progression, secrets on the intended main route, and level-to-level state. | 120–300 hours | 18–43 weeks |
| 10. Release polish | Title/options, control instructions, credits, clean build steps, known-issues list, and a release candidate tested from a fresh microSD copy. | 20–50 hours | 3–8 weeks |

Stages overlap where practical. The initial table sums to roughly **320–790
engineering hours**, or **about 46–113 weeks** at seven hours per week. Its
calendar range of **around 1–2 years for the full main story** and **about 4–10
months for the first playable castle** is now provisional: the full generated
AOT set is over twice the DS's main RAM even before runtime code. We will revise
the schedule after the selective-AOT/overlay decision and a first runtime link
spike establishes actual resident code and graphics memory.

## Performance and hardware risks

- The DS has an ARM946E-S at about 67 MHz and no hardware floating-point unit;
  the original SNES frame is 256×224 while each DS screen is 256×192. The
  display path needs a deliberate scale/crop policy and the two-screen UI must
  preserve the full game view.
- The DS has 4 MB main RAM and limited video RAM, so desktop-sized buffers,
  dynamic allocation patterns, wide renderer snapshots, and host diagnostics
  cannot simply be carried over.
- The DS has 2D tile/bitmap engines and 3D hardware, but no desktop OpenGL or
  SDL. The port needs a direct libnds backend and may need a game-specific
  renderer instead of the desktop PPU presentation pipeline.
- SNES audio emulation may cost more CPU than the gameplay logic. First
  prototypes can use silent audio or a reduced audio path; authentic sound is
  a later milestone.
- The top screen has enough vertical resolution only after rescaling the
  original frame or coordinating content across both screens. The initial
  backend uses nearest-neighbor vertical scaling to show the complete image;
  final presentation needs on-device visual review.

## Current next actions

1. Trace the reset-vector subset through the CPU state, memory bus, interpreter
   fallback, PPU, and frame driver. Decide which runtime pieces can be reused
   behind a small NDS config/platform layer.
2. Link and boot a reset/title-flow experiment on the ARM9. Keep the vector-only
   code resident at first; then add only profiled AOT roots as the game reaches
   more routines.
3. Measure the rendered frame and runtime memory on hardware before choosing
   between further selective AOT and a bank/overlay loader for the full story.
4. Keep ROM-derived source and the ROM local; continue using GitHub issues to
   track hardware feedback and implementation milestones.

