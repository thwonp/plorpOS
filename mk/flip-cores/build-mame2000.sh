#!/bin/sh
# mame2000 for the Miyoo Mini Flip from libretro/mame2000-libretro @ 181b42e
# with the RG Nano port's changes (patches/mame2000-nano.patch: the picoarch
# tree's diff) - the build shipped since plorpos-f1p.26. The community
# prebuilt cannot open ROMs when the content folder path is over 26 characters.
. "$(dirname "$0")/common.sh"
fetch mame2000 libretro/mame2000-libretro 181b42e5da0d2f0c7a2e59244f23633ed594de2e 5420ee002c68afda1c54479ba1b80a4810705d7f4c18e5b6165bfd978968152e
(cd "$R" && git apply --directory=build/flip-cores/src/mame2000 mk/flip-cores/patches/mame2000-nano.patch)
dk src/mame2000 'make clean >/dev/null 2>&1; make platform=unix-armv7-hardfloat-neon CC=arm-linux-gnueabihf-gcc CXX=arm-linux-gnueabihf-g++ USE_CYCLONE=1 USE_DRZ80=1 OPTIMIZE="-Ofast -DNDEBUG -fomit-frame-pointer -mtune=cortex-a7" -j4' > "$W/logs/mame2000.log" 2>&1 ||
	{ tail -20 "$W/logs/mame2000.log"; echo "RESULT mame2000 FAILED"; exit 1; }
check_so src/mame2000/mame2000_libretro.so mame2000_libretro.so
