#!/bin/sh
# gambatte (Game Boy, Game Boy Color) for the Miyoo Mini Flip from libretro/gambatte-libretro HEAD
# (pin: see cores.lock). Replaced mgba on both shelves: about half its cost per frame (plorpos-f1p.77).
XFLAGS=${XFLAGS-"-marm -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -Ofast -mtune=cortex-a7"}
. "$(dirname "$0")/common.sh"
fetch gambatte libretro/gambatte-libretro 77f2567d718b8973d02caa2daa8dce4953faf811 45e63bc79390093b78de025172ea289ded139f4ecf1e8072242f34d7dd540338
mk gambatte gambatte -f Makefile.libretro $XCW platform=unix-armv7-hardfloat-neon
check_so src/gambatte/gambatte_libretro.so gambatte_libretro.so
