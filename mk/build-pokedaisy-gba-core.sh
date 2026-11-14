#!/bin/sh
# Build pokedaisy_gba, the PokeDaisy Game Boy Advance core over armGBA (a recompiler), for every port: the same companion as the mGBA-based
# pokedaisy core (thwonp/pokedaisy-lr, libretro/armgba/), with the GBA run by armGBA. It is the Game Boy Advance shelf's Core
# row "PokeDaisy" (plorpos-oe0.45); the Game Boy and Color shelves keep pokedaisy_gbc (mk/build-pokedaisy-gbc-core.sh) (armGBA runs those on Gambatte, GPL-2.0
# only). On the Brick (A53, EXPAND on) a frame takes 4.2 ms against 7.1. Built in the tortos-toolchain image (Debian, GCC 10,
# glibc 2.17 symbols; the core pins exp / pow to the base glibc version): the Brick's firmware glibc is 2.33. The same file runs
# on the H700 (BaseOS glibc 2.35), GKD (2.40) and Rotate (2.41); it is installed to vendor/cores-brick only, and every payload
# ships it from there as an extra core.
#
#   mk/build-pokedaisy-gba-core.sh     build, install to vendor/cores-brick
#   POKEDAISY_SRC=/path ...               use that checkout (with third_party/armgba and third_party/zlib) instead of cloning the pin
#
# Needs: docker (podman), the tortos-toolchain image (make toolchain), network (the first clone). ~4 minutes.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=${POKEDAISY_SRC:-$HOME/.cache/tortos-pokedaisy-gba-core}
REPO=${POKEDAISY_PLORP:-https://github.com/thwonp/pokedaisy-lr.git}
PD=${PD_COMMIT:-3c2587ac3990b55e6fb0c0a7710329e8a66cf646} # feat/armgba-1.6: b9518c90 + armGBA v1.6 (de50d047), 2026-11-13 (plorpos-0yp)
SHA=${PD_SHA:-0e7971503d98459065303cd6f8fb07d1b9439c6b51d615a312bd430405a1bd31} # sha256 of the stripped core (identical from a fresh clone and a local checkout); PD_SHA=- builds an unpinned tree
IMAGE=${TOOLCHAIN_IMAGE:-tortos-toolchain}

command -v docker >/dev/null || { echo "need docker" >&2; exit 1; }
if [ -z "$POKEDAISY_SRC" ]; then
	[ -d "$SRC/.git" ] || git clone -q "$REPO" "$SRC"
	git -C "$SRC" fetch -q origin 2>/dev/null || true
	git -C "$SRC" checkout -q -f "$PD"
	# Only the two this core needs: armGBA's own Armulators submodule (its app) is not built.
	git -C "$SRC" submodule -q update --init --force third_party/armgba third_party/zlib
	git -C "$SRC" clean -qfdx -e third_party/armgba -e third_party/zlib
fi
[ -f "$SRC/third_party/armgba/CMakeLists.txt" ] && [ -f "$SRC/third_party/zlib/CMakeLists.txt" ] \
	|| { echo "!! $SRC has no third_party/armgba / third_party/zlib" >&2; exit 1; }
OUT=${POKEDAISY_GBA_OUT:-$HOME/.cache/tortos-pokedaisy-gba-core-out}
mkdir -p "$OUT"

docker run --rm -v "$SRC:/src:z" -v "$OUT:/out:z" -w /src $IMAGE sh -c '
set -e
# A fresh build dir every time: CMake reads the toolchain file'"'"'s flags only on a first configure.
rm -rf /out/* && cmake -S libretro/armgba -B /out -DCMAKE_TOOLCHAIN_FILE=/src/libretro/aarch64.cmake \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=-mcpu=cortex-a53 -DCMAKE_POLICY_VERSION_MINIMUM=3.5 >/out/cfg.log 2>&1
cmake --build /out -j"$(nproc)" --target pokedaisy_armgba_libretro >/out/build.log 2>&1
aarch64-linux-gnu-strip --strip-unneeded -o /out/pokedaisy_gba_libretro.stripped.so /out/pokedaisy_gba_libretro.so
# Never ship one that cannot load on the Brick: only the libretro entry points may leave it, and it may need nothing newer
# than its glibc 2.33.
NM=aarch64-linux-gnu-nm sh libretro/test/check_exports.sh /out/pokedaisy_gba_libretro.so
g=$(aarch64-linux-gnu-objdump -T /out/pokedaisy_gba_libretro.so | grep -oE "GLIBC_2\.[0-9]+" | sort -uV | tail -1)
echo "glibc: $g"
[ "$(echo "$g" | cut -d. -f2)" -le 33 ] || { echo "!! needs $g, the Brick has 2.33" >&2; exit 1; }
' 2>&1 | tail -6
# zlib's CMake renames zconf.h inside its source tree: put the submodule back as it was.
[ -z "$POKEDAISY_SRC" ] && git -C "$SRC/third_party/zlib" checkout -q -- . 2>/dev/null || true

SO=$OUT/pokedaisy_gba_libretro.stripped.so
[ -f "$SO" ] || { echo "!! build failed (see $OUT/build.log)" >&2; exit 1; }
GOT=$(sha256sum "$SO" | cut -d' ' -f1)
if [ -n "$SHA" ] && [ "$SHA" != "-" ] && [ "$GOT" != "$SHA" ]; then
	echo "!! pokedaisy armGBA core sha256 $GOT, expected $SHA" >&2
	echo "  The toolchain image or the source changed. Verify the core, then update PD_SHA here." >&2
	exit 1
fi
mkdir -p "$ROOT/vendor/cores-brick"
cp "$SO" "$ROOT/vendor/cores-brick/pokedaisy_gba_libretro.so"
echo "  built  pokedaisy armGBA core ($(git -C "$SRC" rev-parse --short HEAD 2>/dev/null || echo local)) for Brick"
echo "  sha256 $GOT"
