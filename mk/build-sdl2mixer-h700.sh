#!/bin/sh
# build/h700/libSDL2_mixer-2.0.so.0: SDL2_mixer for PortMaster's own GUI
# (plorpos-2wl.10.13). pugwash, its Python front end, imports sdl2.sdlmixer for
# its menu sounds and dies at that import without the library; BaseOS has none.
#
# Only the decoders SDL2_mixer carries itself (WAV, FLAC, MP3, Vorbis through
# stb_vorbis): no fluidsynth, modplug, opusfile or FLAC library to carry, which
# Debian's build would link (and fluidsynth pulls in glib and sndfile).
# Links the H700 SDL2 of mk/fetch-h700-sysroot.sh.
#
# Needs docker with the tortos-toolchain image, curl, and sysroot-h700.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$ROOT/build/h700
DL=$OUT/mixer-dl
VER=2.8.0
SHA=1cfb34c87b26dbdbc7afd68c4f545c0116ab5f90bbfecc5aebe2a9cb4bb31549
[ -d "$ROOT/sysroot-h700/usr/include/SDL2" ] || { echo "no H700 sysroot; run: mk/fetch-h700-sysroot.sh" >&2; exit 1; }
mkdir -p "$DL"
[ -f "$DL/SDL2_mixer-$VER.tar.gz" ] ||
	curl -sSfL -o "$DL/SDL2_mixer-$VER.tar.gz" \
		"https://github.com/libsdl-org/SDL_mixer/releases/download/release-$VER/SDL2_mixer-$VER.tar.gz"
[ "$(sha256sum "$DL/SDL2_mixer-$VER.tar.gz" | cut -d' ' -f1)" = "$SHA" ] || { echo "SDL2_mixer: hash mismatch" >&2; exit 1; }
rm -rf "$DL/src"
mkdir -p "$DL/src"
tar -xzf "$DL/SDL2_mixer-$VER.tar.gz" -C "$DL/src"
docker run --rm -i -v "$DL/src:/src:Z" -v "$ROOT/sysroot-h700:/sysroot:ro,Z" -v "$OUT:/out:Z" tortos-toolchain sh -s << EOF
set -e
cat > /tmp/tc.cmake << T
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_FIND_ROOT_PATH /sysroot)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
T
cmake -S /src/SDL2_mixer-$VER -B /tmp/b -DCMAKE_TOOLCHAIN_FILE=/tmp/tc.cmake -DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_PREFIX_PATH=/sysroot/usr -DBUILD_SHARED_LIBS=ON -DSDL2MIXER_VENDORED=OFF -DSDL2MIXER_DEPS_SHARED=OFF \
	-DSDL2MIXER_SAMPLES=OFF -DSDL2MIXER_INSTALL=OFF -DSDL2MIXER_CMD=OFF -DSDL2MIXER_GME=OFF \
	-DSDL2MIXER_MIDI=OFF -DSDL2MIXER_MOD=OFF -DSDL2MIXER_OPUS=OFF -DSDL2MIXER_WAVPACK=OFF \
	-DSDL2MIXER_FLAC=ON -DSDL2MIXER_FLAC_LIBFLAC=OFF -DSDL2MIXER_FLAC_DRFLAC=ON \
	-DSDL2MIXER_MP3=ON -DSDL2MIXER_MP3_MPG123=OFF -DSDL2MIXER_MP3_DRMP3=ON \
	-DSDL2MIXER_VORBIS=STB -DSDL2MIXER_WAVE=ON > /tmp/cmake.log 2>&1 &&
	cmake --build /tmp/b -j\$(nproc) >> /tmp/cmake.log 2>&1 ||
		{ tail -30 /tmp/cmake.log; echo "!! SDL2_mixer failed" >&2; exit 1; }
cp -L /tmp/b/libSDL2_mixer-2.0.so.0 /out/libSDL2_mixer-2.0.so.0
aarch64-linux-gnu-strip --strip-unneeded /out/libSDL2_mixer-2.0.so.0
echo "needs:"; aarch64-linux-gnu-readelf -d /out/libSDL2_mixer-2.0.so.0 | grep NEEDED
EOF
cp "$DL/src/SDL2_mixer-$VER/LICENSE.txt" "$OUT/SDL2_mixer-LICENSE.txt"
echo "$OUT/libSDL2_mixer-2.0.so.0  $(du -h "$OUT/libSDL2_mixer-2.0.so.0" | cut -f1)"
