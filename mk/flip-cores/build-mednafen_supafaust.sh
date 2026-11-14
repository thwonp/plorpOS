#!/bin/sh
# Supafaust (SNES) for the Miyoo Mini Flip from libretro/supafaust @ 642d1d1
# (HEAD 2026-08-23): the per-game alternative to snes9x2005_plus
# (plorpos-f1p.86/.87). Built at -O2: the core refuses -Ofast (an #error in
# mednafen/sound/DSPUtility.cpp). C++: needs the firmware's libstdc++.
. "$(dirname "$0")/common.sh"
XFLAGS="$ARCH -O2 -mtune=cortex-a7"
fetch mednafen_supafaust libretro/supafaust 642d1d1b6684aa7e306a02a89885f3f5456a5157 257b3e3d5c6a1db16b9fe8d3b6fff956732df7d8c8b0a4865ca6d6534fc9145c
mk mednafen_supafaust mednafen_supafaust $XCW platform=unix
check_so src/mednafen_supafaust/mednafen_supafaust_libretro.so mednafen_supafaust_libretro.so
