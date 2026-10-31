#!/bin/sh
# Build PokeDaisy's Linux host (lidor30/pokedaisy linux/: mGBA plus the
# Pokemon companion, GPL-3.0) for GBA games set to Play With PokeDaisy
# (plorpos-c9p). A separate program the launcher runs, never linked into it.
#
# One binary for every device: it needs only libSDL2-2.0.so.0 (the SDL 2.0
# calls every device's SDL2 has) and glibc 2.27, so it is linked against the
# Brick's SDL2 - the oldest firmware - and runs on the GKD, H700 and Rotate.
#
#   pokedaisy-save.patch - --save FILE: the game's own Saves/<console>/<rom>.srm,
#   the same raw save Diatom's mGBA keeps, so both play one save. Its backups
#   then go to DATA/backups, not beside the save.
#
# Only the mgba submodule is needed (not melonDS or rcheevos). Its CMake build
# renames a zlib header in its tree, hence the forced update before each build.
#
#   mk/build-pokedaisy.sh    -> vendor/pokedaisy/ (mk/plorpos-files.sh ships it)
#
# Needs: docker (podman), the tortos-toolchain image, sysroot/ (mk/fetch-sysroot.sh),
# network for the first clone. ~2 minutes.
# SPDX-License-Identifier: MIT
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=${SRC:-$HOME/.cache/plorpos-pokedaisy}
PIN=52f959770de4085bacfda16e6d77be7e91134bc4   # lidor30/pokedaisy tag v1.3.3, 2026-10-28
IMAGE=${TOOLCHAIN_IMAGE:-tortos-toolchain}
OUT=$ROOT/vendor/pokedaisy

command -v docker >/dev/null || { echo "need docker" >&2; exit 1; }
[ -d "$ROOT/sysroot/usr/lib" ] || { echo "need sysroot/ (mk/fetch-sysroot.sh)" >&2; exit 1; }
[ -d "$SRC/.git" ] || git clone -q https://github.com/lidor30/pokedaisy.git "$SRC"
git -C "$SRC" fetch -q origin "$PIN" 2>/dev/null || git -C "$SRC" fetch -q origin
git -C "$SRC" checkout -q -f "$PIN"
git -C "$SRC" clean -qfdx
git -C "$SRC" submodule -q update --init --force third_party/mgba
git -C "$SRC/third_party/mgba" clean -qfdx
git -C "$SRC" apply "$ROOT/mk/patches/pokedaisy-save.patch"

docker run --rm -v "$SRC:/w:z" -v "$ROOT/sysroot:/sysroot:ro,z" -w /w $IMAGE sh -c '
set -e
cat > /tmp/tc.cmake <<EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_FIND_ROOT_PATH /sysroot)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_C_FLAGS_INIT "-mcpu=cortex-a53")
EOF
cmake -S linux -B build -DCMAKE_TOOLCHAIN_FILE=/tmp/tc.cmake -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_LTO=OFF -DBUILD_TESTING=OFF >build.cfg.log
cmake --build build -j"$(nproc)" >build.log
aarch64-linux-gnu-strip -o build/pokedaisy-linux.stripped build/pokedaisy-linux
# Never ship one a device cannot load: libSDL2 and libc only, nothing past
# the Brick firmware glibc 2.33.
aarch64-linux-gnu-readelf -d build/pokedaisy-linux | grep NEEDED | grep -vqE "libSDL2-2.0|libm|libpthread|libc\." \
  && { echo "!! unexpected library" >&2; exit 1; }
echo "glibc: $(aarch64-linux-gnu-objdump -T build/pokedaisy-linux | grep -oE "GLIBC_2\.[0-9]+" | sort -uV | tail -1)"
' 2>&1 | tail -5

BIN=$SRC/build/pokedaisy-linux.stripped
[ -f "$BIN" ] || { echo "!! build failed (see $SRC/build.log)" >&2; exit 1; }
rm -rf "$OUT"
mkdir -p "$OUT/licenses"
cp "$BIN" "$OUT/pokedaisy-linux"
cp "$SRC/LICENSE" "$OUT/LICENSE"
cp "$SRC/NOTICE" "$OUT/NOTICE"
# The notices linux/package.py ships, the same files.
M=$SRC/third_party/mgba
cp "$M/LICENSE" "$OUT/licenses/mGBA-MPL-2.0.txt"
cp "$M/src/third-party/blip_buf/license.txt" "$OUT/licenses/blip_buf-LGPL-2.1.txt"
cp "$M/src/third-party/inih/LICENSE.txt" "$OUT/licenses/inih-BSD.txt"
echo "Pixel Operator by Jayvee Enaguas. CC0. See NOTICE." > "$OUT/licenses/PixelOperator.txt"
# GPL-3.0 section 6: where the exact source is.
cat > "$OUT/SOURCE.txt" <<EOF
PokeDaisy Linux host, GPL-3.0. Source:
  https://github.com/lidor30/pokedaisy commit $PIN
  with mGBA $(git -C "$SRC/third_party/mgba" rev-parse HEAD)
  plus plorpOS's patch, mk/patches/pokedaisy-save.patch in
  https://github.com/thwonp/plorpOS
EOF
echo "  built  pokedaisy ${PIN%${PIN#???????}} -> vendor/pokedaisy"
echo "  sha256 $(sha256sum "$OUT/pokedaisy-linux" | cut -d' ' -f1)"
