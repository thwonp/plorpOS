#!/bin/sh
cd "$(dirname "$0")"
timeout 1500 docker run --rm -v "$PWD":/w:z -w /w/pcsx_rearmed localhost/miyoo-toolchain sh -c '
true
make -f Makefile.libretro platform=unix-armv7-hardfloat-neon HAVE_PHYSICAL_CDROM=0 CC=arm-linux-gnueabihf-gcc CXX=arm-linux-gnueabihf-g++ -j4 > /w/build-pcsx.full.log 2>&1; echo make-rc=$?
tail -3 /w/build-pcsx.full.log | cut -c1-200
echo undefined-unversioned=$(arm-linux-gnueabihf-nm -D -u pcsx_rearmed_libretro.so | grep -v " w " | grep -vc "@")
arm-linux-gnueabihf-strip --strip-unneeded -o /w/pcsx_flip_libretro.so pcsx_rearmed_libretro.so && ls -l /w/pcsx_flip_libretro.so'
