#!/bin/sh
# The Miyoo Mini Flip sysroot, for `make PLATFORM=miyoo` (plorpos-f1p): SDL2,
# SDL2_image, SDL2_ttf and FFmpeg built from source for armhf. The Flip's
# stock firmware has SDL 1.2 only, so what links here is also what ships on
# the card, as on the H700 (mk/fetch-h700-sysroot.sh, whose versions and
# tarball pins this reuses).
#
# Built lean, and different from the H700's in three ways:
#   - no GPU (SSD202D): SDL's video is offscreen + dummy only. The launcher
#     draws with SDL's software renderer and src/platform_miyoo.c presents to
#     /dev/fb0 itself, the way the H700 draws its menus over a frozen child.
#   - no SDL audio backend: ALSA hw:0,0 and OSS /dev/dsp both play at half
#     rate on this device (measured and heard, plorpos-f1p.3); sound goes
#     through the vendor MI_AO library, in Muse and diatom, not through SDL.
#   - armhf, glibc 2.28: the miyoo-toolchain image (mk/miyoo-toolchain.Dockerfile).
#
# FFmpeg as on the H700: LGPL only, --disable-everything plus what Muse plays.
#
# SQLite (public domain) for the settings database: src/db.c dlopens
# libsqlite3.so.0, which every other device's firmware ships and the Flip's
# does not - without it nothing set on the Flip was remembered (2026-11-04).
#
# Needs: docker with the miyoo-toolchain image, curl, tar.
#
#   mk/fetch-miyoo-sysroot.sh [sysroot-dir]   default: sysroot-miyoo
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-$ROOT/sysroot-miyoo}
# On disk, not /tmp: FFmpeg's tree is too big for a RAM tmpfs.
DL=$ROOT/build/miyoo/sysroot-dl

SDL_VER=2.30.8
SDL_SHA=380c295ea76b9bd72d90075793971c8bcb232ba0a69a9b14da4ae8f603350058
IMG_VER=2.6.3
IMG_SHA=931c9be5bf1d7c8fae9b7dc157828b7eee874e23c7f24b44ba7eff6b4836312c
TTF_VER=2.20.2
TTF_SHA=9dc71ed93487521b107a2c4a9ca6bf43fb62f6bddd5c26b055e6b91418a22053
FF_VER=6.1
FF_SHA=938dd778baa04d353163ca5cb06c909c918850055f549205b29b1224e45a5316
SQ_VER=3460100
SQ_SHA=67d3fe6d268e6eaddcae3727fce58fcc8e9c53869bdd07a0c61e38ddf2965071

for t in docker curl tar; do
	command -v $t > /dev/null || { echo "need $t" >&2; exit 1; }
done
mkdir -p "$DL"

fetch() { # file sha url
	[ -f "$DL/$1" ] || curl -sSfL -o "$DL/$1" "$3"
	got=$(sha256sum "$DL/$1" | cut -d' ' -f1)
	[ "$got" = "$2" ] || { echo "$1: hash mismatch: $got" >&2; exit 1; }
}
fetch SDL2-$SDL_VER.tar.gz "$SDL_SHA" \
	"https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/SDL2-$SDL_VER.tar.gz"
fetch SDL2_image-$IMG_VER.tar.gz "$IMG_SHA" \
	"https://github.com/libsdl-org/SDL_image/releases/download/release-$IMG_VER/SDL2_image-$IMG_VER.tar.gz"
fetch SDL2_ttf-$TTF_VER.tar.gz "$TTF_SHA" \
	"https://github.com/libsdl-org/SDL_ttf/releases/download/release-$TTF_VER/SDL2_ttf-$TTF_VER.tar.gz"
fetch ffmpeg-$FF_VER.tar.gz "$FF_SHA" "https://ffmpeg.org/releases/ffmpeg-$FF_VER.tar.gz"
fetch sqlite-autoconf-$SQ_VER.tar.gz "$SQ_SHA" "https://www.sqlite.org/2024/sqlite-autoconf-$SQ_VER.tar.gz"

SRC=$DL/src
rm -rf "$SRC" "$OUT"
mkdir -p "$SRC" "$OUT/usr/lib" "$OUT/usr/include"
for t in SDL2-$SDL_VER SDL2_image-$IMG_VER SDL2_ttf-$TTF_VER ffmpeg-$FF_VER sqlite-autoconf-$SQ_VER; do
	tar -xzf "$DL/$t.tar.gz" -C "$SRC"
done

ARCH_FLAGS="-marm -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard"
docker run --rm -i -v "$SRC:/src:z" -v "$OUT:/out:z" miyoo-toolchain sh -s << EOF
set -e
cat > /tmp/tc.cmake << T
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)
set(CMAKE_C_FLAGS_INIT "$ARCH_FLAGS")
set(CMAKE_CXX_FLAGS_INIT "$ARCH_FLAGS")
set(CMAKE_FIND_ROOT_PATH /out)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
T
C="-DCMAKE_TOOLCHAIN_FILE=/tmp/tc.cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_PREFIX_PATH=/out/usr"
build() { # name srcdir options...
	n=\$1; s=\$2; shift 2
	cmake -S "\$s" -B /tmp/b-\$n \$C "\$@" > /tmp/\$n.log 2>&1 &&
	cmake --build /tmp/b-\$n -j\$(nproc) >> /tmp/\$n.log 2>&1 &&
	DESTDIR=/out cmake --install /tmp/b-\$n >> /tmp/\$n.log 2>&1 ||
		{ tail -30 /tmp/\$n.log; echo "!! \$n failed" >&2; exit 1; }
	echo "  built  \$n"
}
build SDL2 /src/SDL2-$SDL_VER \
	-DSDL_STATIC=OFF -DSDL_TEST=OFF -DSDL_RPATH=OFF \
	-DSDL_OFFSCREEN=ON -DSDL_DUMMYVIDEO=ON \
	-DSDL_OPENGL=OFF -DSDL_OPENGLES=OFF -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_KMSDRM=OFF \
	-DSDL_VIVANTE=OFF -DSDL_VULKAN=OFF -DSDL_DIRECTFB=OFF -DSDL_RPI=OFF \
	-DSDL_ALSA=OFF -DSDL_OSS=OFF -DSDL_PULSEAUDIO=OFF -DSDL_PIPEWIRE=OFF -DSDL_JACK=OFF \
	-DSDL_SNDIO=OFF -DSDL_LIBSAMPLERATE=OFF \
	-DSDL_DBUS=OFF -DSDL_IBUS=OFF -DSDL_LIBUDEV=OFF -DSDL_HIDAPI=OFF
grep -q "define SDL_VIDEO_DRIVER_OFFSCREEN 1" /out/usr/include/SDL2/SDL_config.h ||
	{ echo "!! SDL2 built without the offscreen driver" >&2; exit 1; }
build SDL2_image /src/SDL2_image-$IMG_VER -DSDL2IMAGE_BACKEND_STB=ON -DSDL2IMAGE_VENDORED=OFF \
	-DSDL2IMAGE_DEPS_SHARED=OFF -DSDL2IMAGE_SAMPLES=OFF -DSDL2IMAGE_AVIF=OFF -DSDL2IMAGE_JXL=OFF \
	-DSDL2IMAGE_TIF=OFF -DSDL2IMAGE_WEBP=OFF -DSDL2IMAGE_JPG=ON -DSDL2IMAGE_PNG=ON
build SDL2_ttf /src/SDL2_ttf-$TTF_VER -DSDL2TTF_VENDORED=ON -DSDL2TTF_HARFBUZZ=OFF \
	-DSDL2TTF_SAMPLES=OFF -DBUILD_SHARED_LIBS=ON
cd /src/ffmpeg-$FF_VER
./configure --prefix=/usr --enable-cross-compile --arch=arm --cpu=cortex-a7 --target-os=linux \
	--cross-prefix=arm-linux-gnueabihf- --extra-cflags="$ARCH_FLAGS" --enable-neon \
	--enable-shared --disable-static \
	--disable-programs --disable-doc --disable-autodetect --disable-network \
	--disable-avdevice --disable-swscale --disable-postproc --disable-everything \
	--enable-swresample --enable-protocol=file \
	--enable-demuxer=mp3,aac,mov,ogg,flac,wav \
	--enable-decoder=mp3,mp3float,aac,alac,flac,vorbis,opus,pcm_s16le,pcm_s24le,pcm_s32le,pcm_f32le,pcm_u8 \
	--enable-parser=mpegaudio,aac,flac,opus,vorbis \
	--enable-filter=abuffer,abuffersink,aresample,aformat,atempo,volume > /tmp/ffmpeg.log 2>&1 &&
	make -j\$(nproc) >> /tmp/ffmpeg.log 2>&1 &&
	make install DESTDIR=/out >> /tmp/ffmpeg.log 2>&1 ||
		{ tail -30 /tmp/ffmpeg.log; echo "!! ffmpeg failed" >&2; exit 1; }
grep -q "^License: LGPL" /tmp/ffmpeg.log || { echo "!! ffmpeg is not LGPL-only" >&2; exit 1; }
echo "  built  ffmpeg $FF_VER (\$(grep ^License: /tmp/ffmpeg.log))"
cd /src/sqlite-autoconf-$SQ_VER
CFLAGS="-O2 $ARCH_FLAGS" ./configure --host=arm-linux-gnueabihf --prefix=/usr \
	--enable-shared --disable-static --disable-readline > /tmp/sqlite.log 2>&1 &&
	make -j\$(nproc) >> /tmp/sqlite.log 2>&1 &&
	make install DESTDIR=/out >> /tmp/sqlite.log 2>&1 ||
		{ tail -30 /tmp/sqlite.log; echo "!! sqlite failed" >&2; exit 1; }
echo "  built  sqlite $SQ_VER"
for l in libSDL2-2.0.so.0 libSDL2_image-2.0.so.0 libSDL2_ttf-2.0.so.0 \
         libavformat.so.60 libavcodec.so.60 libavfilter.so.9 libswresample.so.4 libavutil.so.58 \
         libsqlite3.so.0; do
	arm-linux-gnueabihf-strip --strip-unneeded "\$(readlink -f /out/usr/lib/\$l)"
	if arm-linux-gnueabihf-objdump -T /out/usr/lib/\$l | grep -qE 'GLIBC_2\.(29|[3-9][0-9])'; then
		echo "!! \$l needs a newer glibc than the Flip's 2.28" >&2; exit 1
	fi
done
EOF
cp "$SRC/ffmpeg-$FF_VER/COPYING.LGPLv2.1" "$OUT/usr/lib/"
echo "sysroot ready: $OUT"
