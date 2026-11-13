#!/bin/sh
# fceumm for the Miyoo Mini Flip from libretro/libretro-fceumm HEAD (pin: see cores.lock),
# HD packs off (HAVE_HDPACK=0, as the RG Nano port), -Ofast + LTO + profile-guided
# (pgo/fceumm, trained by pgo/fceumm/TRAIN). Plain -Ofast measured 14% slower per frame
# than the community prebuilt; this build is at or under it (plorpos-f1p.62.3.4).
XFLAGS=${XFLAGS-"-marm -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -Ofast -mtune=cortex-a7 -flto=4 -fuse-linker-plugin"}
. "$(dirname "$0")/common.sh"
fetch fceumm libretro/libretro-fceumm e0f3086a8104f8fbbe49a2615b10401504a9533f 7a81aa960e0424f736ceaf3221b93663f7ae0b0a68483ac17f8eb0c4afff28f7
mk fceumm fceumm -f Makefile.libretro $XCW platform=unix-armv7-hardfloat-neon HAVE_HDPACK=0
check_so src/fceumm/fceumm_libretro.so fceumm_libretro.so
