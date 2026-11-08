#!/bin/sh
cd "$(dirname "$0")"
timeout 1500 docker run --rm -v "$PWD":/w:z -w /w/mame2000 localhost/miyoo-toolchain sh -c '
make clean >/dev/null 2>&1
make platform=unix-armv7-hardfloat-neon CC=arm-linux-gnueabihf-gcc CXX=arm-linux-gnueabihf-g++ USE_CYCLONE=1 USE_DRZ80=1 OPTIMIZE="-Ofast -DNDEBUG -fomit-frame-pointer -mtune=cortex-a7" -j4 2>&1 | tail -15
ls -l mame2000_libretro.so'
