#!/bin/sh
# pcsx_rearmed for the Miyoo Mini Flip from libretro/pcsx_rearmed @ c8816799
# with mk/patches/pcsx-rearmed-flip-tc.patch (dynarec translation cache 16 MB
# -> 4 MB) - the build shipped since plorpos-f1p.27. HAVE_PHYSICAL_CDROM=0, or
# dlopen fails on dir_list_new.
. "$(dirname "$0")/common.sh"
fetch pcsx_rearmed libretro/pcsx_rearmed 7b0e8f8fadfbce3d170d195b9d9eecaabfa08c8e 4f47bd98bac6cdf62dd4f7a56069b0b0d9999aa6800e636334d1788c5cf38ccf
(cd "$R" && git apply --directory=build/flip-cores/src/pcsx_rearmed mk/patches/pcsx-rearmed-flip-tc.patch)
XFLAGS=""
mk pcsx_rearmed pcsx_rearmed -f Makefile.libretro platform=unix-armv7-hardfloat-neon HAVE_PHYSICAL_CDROM=0 $XC
check_so src/pcsx_rearmed/pcsx_rearmed_libretro.so pcsx_rearmed_libretro.so
