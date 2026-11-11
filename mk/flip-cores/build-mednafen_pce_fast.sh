#!/bin/sh
# mednafen_pce_fast for the Miyoo Mini Flip from libretro/beetle-pce-fast-libretro @ eb57e0c21f082de9f0ba86f26b3c716a2ed18b14 (pin: see cores.lock).
. "$(dirname "$0")/common.sh"
fetch mednafen_pce_fast libretro/beetle-pce-fast-libretro 2de1ce1c3f237b161c126c54ba407990bb86d1e9 6a6846f6d3b25d5883b23a80b78ea968127b306c36e21c9499a3b5be00f6831a
mk mednafen_pce_fast mednafen_pce_fast $XCW platform=unix-armv7-hardfloat-neon
check_so src/mednafen_pce_fast/mednafen_pce_fast_libretro.so mednafen_pce_fast_libretro.so
