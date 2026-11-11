#!/bin/sh
# RACE (Neo Geo Pocket) for the Miyoo Mini Flip from libretro/RACE @ c7810dd (HEAD 2026-06-29).
# Two builds: race_libretro.so = Makefile default CZ80 (C Z80, PIC-clean);
# race_drz80_libretro.so = DrZ80 ARM asm Z80 (DRZ80=1; the asm is not PIC, so
# that .so carries TEXTRELs - glibc loads it, benchmark before choosing).
. "$(dirname "$0")/common.sh"
fetch race libretro/RACE c7810dd7f172827bfa2004813bc000b13786636b 9a7c2e4041753c8235f403bfa48f93b6049509f857d45e42e0657efffbebd898
mk race_drz80 race $XCW platform=unix-armv7-hardfloat-neon DRZ80=1 AS=arm-linux-gnueabihf-as
check_so src/race/race_libretro.so race_libretro.so
