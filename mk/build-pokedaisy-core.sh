#!/bin/sh
# Build PokeDaisy as a libretro core for the plain Brick: mGBA's own libretro core with the PokeDaisy companion
# layered over it (thwonp/pokedaisy-plorp, libretro/). One core for Game Boy, Game Boy Color and Game Boy Advance,
# offered in the Core row of those three shelves beside mGBA. Built in the tortos-toolchain image (Debian, GCC 10,
# glibc 2.17 symbols): the Brick's firmware glibc is 2.33.
#
#   mk/build-pokedaisy-core.sh     build, install to vendor/cores-brick
#   POKEDAISY_SRC=/path ...        use that checkout (with third_party/mgba) instead of cloning the pin
#
# Needs: docker (podman), the tortos-toolchain image (make toolchain), network for the first clone. ~2 minutes.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=${POKEDAISY_SRC:-$HOME/.cache/tortos-pokedaisy-core}
REPO=${POKEDAISY_PLORP:-https://github.com/thwonp/pokedaisy-plorp.git}
PD=${PD_COMMIT:-8554b5a8e1ce891244df7d14a21cc3adfb2cc22a} # feat/libretro-core: the parity table, 2026-11-03
SHA=${PD_SHA:-e22c8a866a3ea44619968f097d19bc47873470a6a3382cc97b0fe0ef9e757041} # sha256 of the stripped core
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
OUT=$SRC/build-brick
mkdir -p "$OUT"

docker run --rm -v "$SRC:/src:z" -v "$OUT:/out:z" -w /src $IMAGE sh -c '
set -e
cmake -S libretro -B /out -DCMAKE_TOOLCHAIN_FILE=/src/libretro/aarch64.cmake -DCMAKE_BUILD_TYPE=Release >/out/cfg.log 2>&1
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
if [ -n "$SHA" ] && [ "$GOT" != "$SHA" ]; then
	echo "!! pokedaisy core sha256 $GOT, expected $SHA" >&2
	echo "  The toolchain image or the source changed. Verify the core, then update PD_SHA here." >&2
	exit 1
fi
mkdir -p "$ROOT/vendor/cores-brick"
cp "$SO" "$ROOT/vendor/cores-brick/pokedaisy_libretro.so"
echo "  built  pokedaisy core ($(git -C "$SRC" rev-parse --short HEAD 2>/dev/null || echo local)) for Brick"
echo "  sha256 $GOT"
