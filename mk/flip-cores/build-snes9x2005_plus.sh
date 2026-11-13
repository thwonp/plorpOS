#!/bin/sh
# snes9x2005_plus for the Miyoo Mini Flip from libretro/snes9x2005 HEAD (pin: see cores.lock):
# Snes9x 1.43 with blargg's SNES_SPC sound (USE_BLARGG_APU=1). Replaces snes9x2010 as the
# SNES default: lighter per frame, so heavy games hold the panel lock (plorpos-f1p.70).
# Same commit as the RG Nano's picoarch build (a79dfe9), rebuilt for glibc/armhf here.
. "$(dirname "$0")/common.sh"
fetch snes9x2005_plus libretro/snes9x2005 a79dfe9047e7fec58808aefe48ad2bf499c7af11 ed988a94a83ac67c2f8afa89dd7d444e61dc2f47ab9e4c75d8c90a46ce779975
mk snes9x2005_plus snes9x2005_plus $XCW platform=unix USE_BLARGG_APU=1
check_so src/snes9x2005_plus/snes9x2005_plus_libretro.so snes9x2005_plus_libretro.so
