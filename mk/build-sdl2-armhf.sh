#!/bin/sh
# build/h700/lib32/libSDL2-2.0.so.0: the H700 SDL2 (the same 2.30.8 and the same
# mk/patches/sdl2-h700.patch as the aarch64 one, mk/fetch-h700-sysroot.sh) built
# for 32-bit ARM, for the armhf PortMaster ports (plorpos-2wl.10).
#
# BaseOS's own /usr/lib32/libSDL2-2.0.so.0 is the stock firmware's 2.0.12, which
# looks for pads and keyboards through udev - and BaseOS has none, so it finds
# nobody: Shovel Knight (box86) sat on its title screen with no input at all. The
# patch is what makes the built-in pad a joystick, and its evdev reads need no
# udev. Video is the same Mali fbdev window, through /usr/lib32's libEGL.
#
# glibc: BaseOS is 2.35, which is Ubuntu 22.04's (checked below, as
# fetch-h700-sysroot.sh does).
#
# Needs docker and curl (the container fetches its compiler from Ubuntu).
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$ROOT/build/h700/lib32
DL=$ROOT/build/h700/sdl32-dl
SDL_VER=2.30.8
SDL_SHA=380c295ea76b9bd72d90075793971c8bcb232ba0a69a9b14da4ae8f603350058
mkdir -p "$DL" "$OUT"
[ -f "$DL/SDL2-$SDL_VER.tar.gz" ] ||
	curl -sSfL -o "$DL/SDL2-$SDL_VER.tar.gz" \
		"https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/SDL2-$SDL_VER.tar.gz"
[ "$(sha256sum "$DL/SDL2-$SDL_VER.tar.gz" | cut -d' ' -f1)" = "$SDL_SHA" ] || { echo "SDL2: hash mismatch" >&2; exit 1; }
rm -rf "$DL/src"
mkdir -p "$DL/src"
tar -xzf "$DL/SDL2-$SDL_VER.tar.gz" -C "$DL/src"
# patch(1), not git apply: inside this repo git skips files outside its index, silently.
patch -s -p1 -d "$DL/src/SDL2-$SDL_VER" < "$ROOT/mk/patches/sdl2-h700.patch"
# ALSA's headers are all SDL needs of it: it loads libasound at run time.
if [ ! -d "$DL/alsa/usr/include/alsa" ]; then
	mkdir -p "$DL/alsa"
	curl -sSfL -o "$DL/alsa-dev.deb" \
		"http://snapshot.debian.org/archive/debian/20260901T000000Z/pool/main/a/alsa-lib/libasound2-dev_1.2.4-1.1_arm64.deb"
	(cd "$DL/alsa" && ar x ../alsa-dev.deb data.tar.xz && tar -xJf data.tar.xz)
fi
docker run --rm -i -v "$DL:/dl:Z" -v "$OUT:/out:Z" docker.io/library/ubuntu:22.04 sh -s << 'INNER'
set -e
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq > /dev/null
apt-get install -y -qq gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf cmake make binutils-arm-linux-gnueabihf > /dev/null
# ALSA's headers, and a stand-in for the library cmake looks for (SDL_ALSA_SHARED never links it).
mkdir -p /sys32/usr/include /sys32/usr/lib
cp -r /dl/alsa/usr/include/alsa /sys32/usr/include/
: > /sys32/usr/lib/libasound.so
cat > /tmp/tc.cmake << T
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)
set(CMAKE_FIND_ROOT_PATH /sys32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
T
S=/dl/src/SDL2-2.30.8
cmake -S $S -B /tmp/b -DCMAKE_TOOLCHAIN_FILE=/tmp/tc.cmake -DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_C_FLAGS="-I$S/src/video/khronos" \
	-DSDL_STATIC=OFF -DSDL_TEST=OFF -DSDL_RPATH=OFF \
	-DSDL_MALI=ON -DSDL_OPENGLES=ON -DSDL_OPENGL=OFF -DSDL_X11=OFF -DSDL_WAYLAND=OFF \
	-DSDL_KMSDRM=OFF -DSDL_VIVANTE=OFF -DSDL_VULKAN=OFF -DSDL_DIRECTFB=OFF -DSDL_RPI=OFF \
	-DSDL_OFFSCREEN=OFF -DSDL_ALSA=ON -DSDL_ALSA_SHARED=ON -DSDL_PULSEAUDIO=OFF \
	-DSDL_PIPEWIRE=OFF -DSDL_JACK=OFF -DSDL_SNDIO=OFF -DSDL_OSS=OFF -DSDL_LIBSAMPLERATE=OFF \
	-DSDL_DBUS=OFF -DSDL_IBUS=OFF -DSDL_LIBUDEV=OFF -DSDL_HIDAPI=OFF > /tmp/cmake.log 2>&1 ||
	{ tail -30 /tmp/cmake.log; exit 1; }
cp /tmp/cmake.log /out/cmake.log
grep -q "define SDL_VIDEO_DRIVER_MALI 1" /tmp/b/include-config-*/SDL2/SDL_config.h /tmp/b/include/SDL2/SDL_config.h 2> /dev/null ||
	{ grep -i "egl\|opengles\|mali" /tmp/cmake.log | head -20; echo "!! SDL2 built without the mali driver" >&2; exit 1; }
cmake --build /tmp/b -j"$(nproc)" > /tmp/build.log 2>&1 || { tail -30 /tmp/build.log; exit 1; }
cp -L /tmp/b/libSDL2-2.0.so.0 /out/libSDL2-2.0.so.0
arm-linux-gnueabihf-strip --strip-unneeded /out/libSDL2-2.0.so.0
if arm-linux-gnueabihf-objdump -T /out/libSDL2-2.0.so.0 | grep -E 'GLIBC_2\.(3[6-9]|[4-9][0-9])'; then
	echo "!! needs glibc newer than BaseOS's 2.35" >&2
	exit 1
fi
echo built
INNER
ls -la "$OUT/libSDL2-2.0.so.0"
