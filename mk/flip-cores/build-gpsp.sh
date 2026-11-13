#!/bin/sh
# gpsp for the Miyoo Mini Flip from libretro/gpsp @ b0d5d27 (2025-01-20, gpsp
# v1.0.0, the version the community prebuilt reports; pin: see cores.lock),
# with the Makefile's own `miyoomini` stanza (-Ofast, LTO, Cortex-A7 NEON) on
# our toolchain. HEAD (v1.1.0) and plain -Ofast/-O2/-O3 builds measured 4-10%
# slower per frame on the Flip (plorpos-f1p.62.3.5).
XFLAGS=${XFLAGS-""}
. "$(dirname "$0")/common.sh"
fetch gpsp libretro/gpsp 5819380c2ffb0900219d700a382ee68c464ebb99 1dfc919966cd58ce1d8f2580041d9bffe2e313d9442260c713ed0286a048a733
mk gpsp gpsp platform=miyoomini $XCW AR=arm-linux-gnueabihf-ar HAVE_DYNAREC=1 $GPSP_OPTS
check_so src/gpsp/gpsp_plus_libretro.so gpsp_libretro.so
