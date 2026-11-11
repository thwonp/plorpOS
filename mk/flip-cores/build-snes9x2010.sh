#!/bin/sh
# snes9x2010 for the Miyoo Mini Flip from libretro/snes9x2010 HEAD (pin: see cores.lock),
# -Ofast + LTO + profile-guided (pgo/snes9x2010, trained by pgo/snes9x2010/TRAIN):
# faster per frame than the community prebuilt on DKC, DKC2, Star Fox and Mega Man X.
# patches/snes9x2010-flip-buffers.patch (-DPLORPOS_FLIP): HEAD widened every screen
# buffer to 1024 px for 4x Mode 7 and HD packs (+1.9 MB); the Flip keeps the NTSC width,
# maps 4x Mode 7 to 2x and loads no HD packs (plorpos-f1p.62.3.10).
XFLAGS=${XFLAGS-"-marm -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -Ofast -mtune=cortex-a7 -flto=4 -fuse-linker-plugin -lpthread -lm"}
XFLAGS="$XFLAGS -DPLORPOS_FLIP"   # patches/snes9x2010-flip-buffers.patch
. "$(dirname "$0")/common.sh"
fetch snes9x2010 libretro/snes9x2010 fe690dd321fa5a46b5234a2bde089d2518c62b0e 7d2d2a21c2c00e6c5a83c8b51c18fccb6a9b3764584aa5ceb4b231e3a7c0ab6c
(cd "$R" && git apply --directory=build/flip-cores/src/snes9x2010 mk/flip-cores/patches/snes9x2010-flip-buffers.patch)
mk snes9x2010 snes9x2010 $XCW platform=unix-armv7-hardfloat-neon
check_so src/snes9x2010/snes9x2010_libretro.so snes9x2010_libretro.so
