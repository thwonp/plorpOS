#!/bin/sh
# fake-08 (PICO-8) for the Miyoo Mini Flip from jtothebell/fake-08 tag v0.0.2.19
# (18a1c8a) + its z8lua submodule. The libretro Makefile has no armv7 stanza
# (its "miyoomini" one names the output _miyoomini.so), so platform=unix with
# the cortex-a7/neon flags from the CC wrapper.
XFLAGS=${XFLAGS-"-marm -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -Ofast -mtune=cortex-a7 -flto=4 -fuse-linker-plugin"}
PGO_SUB=x/y   # objects are built as ../../source/*.o
. "$(dirname "$0")/common.sh"
fetch fake08 jtothebell/fake-08 814991a2571ad3970e386cef48f3b148aa1c27b9 c3ac9b0b93c0e4caa473128a48585e16c4304f8a1aa1b170f8622985202a852c
fetch fake08-z8lua jtothebell/z8lua e6928578d46b61fd5ea30cfcf547e855a30a0553 e9fadd5bcc17bce83d242831c55a2b57a32b6997934278b1d960b3d13367b3a4 fake08/libs/z8lua
# plorpOS's three fake08 patches, as on every other port (mk/build-fake08.sh says why).
# REQUIRED, not upstream (never merged): the save state / rewind fixes - GC held
# through a state restore (fake08-states) and a bounded Lua state (fake08-bound-lua) -
# plus the cart load in retro_load_game (fake08-load). The build fails if one does not apply.
for p in fake08-load fake08-states fake08-bound-lua; do
  (cd "$R" && git apply --directory=build/flip-cores/src/fake08 mk/patches/$p.patch)
done
mk fake08 fake08/platform/libretro $XCW platform=unix
check_so src/fake08/platform/libretro/fake08_libretro.so fake08_libretro.so
