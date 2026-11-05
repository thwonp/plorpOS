#!/bin/sh
# Build PokeDaisy as a libretro core for every port: mGBA's own libretro core with the PokeDaisy companion
# layered over it (thwonp/pokedaisy-plorp, libretro/). One core for Game Boy, Game Boy Color and Game Boy Advance,
# offered in the Core row of those three shelves beside mGBA. Built in the tortos-toolchain image (Debian, GCC 10,
# glibc 2.17 symbols): the Brick's firmware glibc is 2.33.
# The same file runs on the H700 (BaseOS glibc 2.35), GKD (2.40) and Rotate (2.41); it is installed to
# vendor/cores-brick only. No payload carries it yet (no systems.cfg names it): it is deployed by hand (plorpos-oe0.22).
#
#   mk/build-pokedaisy-core.sh     build, install to vendor/cores-brick
#   POKEDAISY_SRC=/path ...        use that checkout (with third_party/mgba) instead of cloning the pin
#
# Needs: docker (podman), the tortos-toolchain image (make toolchain), network for the first clone. ~2 minutes.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=${POKEDAISY_SRC:-$HOME/.cache/tortos-pokedaisy-core}
REPO=${POKEDAISY_PLORP:-https://github.com/thwonp/pokedaisy-plorp.git}
PD=${PD_COMMIT:-6bf806e6d6eeefb938dd8cc5e89b79a5d7bd2d01} # feat/libretro-core: HUD on the visible frame, menus >= 320x240, bag underline, long setting labels, 2026-11-04
SHA=${PD_SHA:-2b964079a9e458c0772016021d031b2be9ae3e244124f72b0e605c00461e0cab} # sha256 of the stripped core; PD_SHA=- builds an unpinned tree
IMAGE=${TOOLCHAIN_IMAGE:-tortos-toolchain}

command -v docker >/dev/null || { echo "need docker" >&2; exit 1; }
if [ -z "$POKEDAISY_SRC" ]; then
	[ -d "$SRC/.git" ] || git clone -q "$REPO" "$SRC"
	git -C "$SRC" fetch -q origin 2>/dev/null || true
	git -C "$SRC" checkout -q -f "$PD"
	git -C "$SRC" submodule -q update --init --force third_party/mgba
	git -C "$SRC" clean -qfdx -e third_party/mgba
fi
[ -f "$SRC/third_party/mgba/CMakeLists.txt" ] || { echo "!! $SRC has no third_party/mgba" >&2; exit 1; }
OUT=${POKEDAISY_OUT:-$HOME/.cache/tortos-pokedaisy-core-out}
mkdir -p "$OUT"

docker run --rm -v "$SRC:/src:z" -v "$OUT:/out:z" -w /src $IMAGE sh -c '
set -e
# A fresh build dir every time: CMake reads the toolchain file'"'"'s flags only on a first configure, and a kept cache once
# froze them empty (the v0.3-pre2 asset is the generic armv8 build that made; plorpos-oe0.23).
rm -rf /out/* && cmake -S libretro -B /out -DCMAKE_TOOLCHAIN_FILE=/src/libretro/aarch64.cmake -DCMAKE_BUILD_TYPE=Release >/out/cfg.log 2>&1
cmake --build /out -j"$(nproc)" --target pokedaisy_libretro >/out/build.log 2>&1
aarch64-linux-gnu-strip --strip-unneeded -o /out/pokedaisy_libretro.stripped.so /out/pokedaisy_libretro.so
# Never ship one that cannot load on the Brick: every libretro entry point and nothing else exported,
# no unversioned undefined symbol, nothing newer than its glibc 2.33 (ADR-0006: a second mGBA is resident).
NM=aarch64-linux-gnu-nm sh libretro/test/check_exports.sh /out/pokedaisy_libretro.so
[ "$(aarch64-linux-gnu-nm -D -u /out/pokedaisy_libretro.so | grep -v " w " | grep -vc @)" = 0 ] \
  || { echo "!! undefined symbols" >&2; exit 1; }
g=$(aarch64-linux-gnu-objdump -T /out/pokedaisy_libretro.so | grep -oE "GLIBC_2\.[0-9]+" | sort -uV | tail -1)
echo "glibc: $g"
[ "$(echo "$g" | cut -d. -f2)" -le 33 ] || { echo "!! needs $g, the Brick has 2.33" >&2; exit 1; }
' 2>&1 | tail -6

SO=$OUT/pokedaisy_libretro.stripped.so
[ -f "$SO" ] || { echo "!! build failed (see $OUT/build.log)" >&2; exit 1; }
GOT=$(sha256sum "$SO" | cut -d' ' -f1)
if [ -n "$SHA" ] && [ "$SHA" != "-" ] && [ "$GOT" != "$SHA" ]; then
	echo "!! pokedaisy core sha256 $GOT, expected $SHA" >&2
	echo "  The toolchain image or the source changed. Verify the core, then update PD_SHA here." >&2
	exit 1
fi
mkdir -p "$ROOT/vendor/cores-brick"
cp "$SO" "$ROOT/vendor/cores-brick/pokedaisy_libretro.so"
echo "  built  pokedaisy core ($(git -C "$SRC" rev-parse --short HEAD 2>/dev/null || echo local)) for Brick"
echo "  sha256 $GOT"
