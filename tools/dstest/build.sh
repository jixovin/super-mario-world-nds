#!/bin/bash
# Build the headless DS test runner on Linux.
#   ./build.sh [melonDS checkout dir]   (cloned and built here if missing)
set -e
M=${1:-$PWD/melonDS}
if [ ! -d "$M" ]; then git clone --depth 1 https://github.com/melonDS-emu/melonDS.git "$M"; fi
cmake -S "$M" -B "$M/build" -DBUILD_QT_SDL=OFF -DENABLE_OGLRENDERER=OFF -DENABLE_GDBSTUB=OFF \
      -DCMAKE_BUILD_TYPE=Release -DENABLE_LTO_RELEASE=OFF >/dev/null
make -C "$M/build" -j4 core >/dev/null
g++ -std=gnu++17 -O2 -fwrapv -DARCHITECTURE_x86_64=1 -DJIT_ENABLED \
    -I"$M/src" -I"$M/build/src" -I"$M/src/teakra/include" melonds_headless.cpp \
    "$M/build/src/libcore.a" "$M/build/src/teakra/src/libteakra.a" -lpthread -o melonds_headless
echo "built tools/dstest/melonds_headless"
echo "usage: SDDIR=<folder with smw_assets.dat> ./melonds_headless game.nds out_dir 300 shot 10:a ..."
