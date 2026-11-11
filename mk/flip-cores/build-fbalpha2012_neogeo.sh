#!/bin/sh
# fbalpha2012_neogeo for the Miyoo Mini Flip from libretro/fbalpha2012_neogeo @ 99e0d026a2d8c7b8984bd31cdaf45bf0708631f4 (pin: see cores.lock).
. "$(dirname "$0")/common.sh"
fetch fbalpha2012_neogeo libretro/fbalpha2012_neogeo e092097037d494c50d17c5144a2758d68538061b ab2258eb154e590715c081e72d84e39162a0f6f0dd2053a1d9489f79461a6d3f
mk fbalpha2012_neogeo fbalpha2012_neogeo -f makefile.libretro $XCW platform=unix-armv7-hardfloat-neon
check_so src/fbalpha2012_neogeo/fbalpha2012_neogeo_libretro.so fbalpha2012_neogeo_libretro.so
