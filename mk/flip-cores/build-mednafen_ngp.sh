#!/bin/sh
# Beetle NeoPop (Neo Geo Pocket) for the Miyoo Mini Flip from
# libretro/beetle-ngp-libretro @ a50d5ac (HEAD 2026-06-14): the per-game
# alternative to RACE on both Neo Geo Pocket shelves (plorpos-f1p.85). Slower
# than RACE's DrZ80 build but draws every game, Cool Cool Jam included.
. "$(dirname "$0")/common.sh"
fetch mednafen_ngp libretro/beetle-ngp-libretro a50d5ac288a81f2104ddf43195a4efdd15c72227 ffeabf2a357548f3a55423d542c31bb1e08571d81d7104f8213df3bfa2eb9c9f
mk mednafen_ngp mednafen_ngp $XCW platform=unix-armv7-hardfloat-neon
check_so src/mednafen_ngp/mednafen_ngp_libretro.so mednafen_ngp_libretro.so
