#!/bin/sh
# Build Gambatte, the Game Boy and Game Boy Color shelves' core on the Brick, H700, GKD and Rotate
# (plorpos-ssa), from the libretro/gambatte-libretro commit the Flip ships (mk/flip-cores/cores.lock,
# plorpos-f1p.77). mGBA stays a selectable Core on both shelves.
# Built in the tortos-toolchain image (Debian, GCC 10): its glibc and libstdc++ are no newer than the
# Brick's (2.33, libstdc++ 6.0.28); the H700 (BaseOS), GKD and Rotate carry newer ones. Installed to
# vendor/cores-brick, which every aarch64 payload ships from.
# Needs: docker (podman), the tortos-toolchain image (make toolchain), network for the first clone. ~1 minute.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=${GAMBATTE_SRC:-$HOME/.cache/tortos-gambatte}
PIN=77f2567d718b8973d02caa2daa8dce4953faf811                                    # libretro/gambatte-libretro, as the Flip
SHA=${GAMBATTE_SHA:-3988b8949e6b28126a01185d2a4f77d4ffdd10d258ea5f1195e777249a9fa733} # stripped core, identical from two fresh clones; generic armv8 -O2
IMAGE=${TOOLCHAIN_IMAGE:-tortos-toolchain}

command -v docker >/dev/null || { echo "need docker" >&2; exit 1; }
[ -d "$SRC/.git" ] || git clone -q https://github.com/libretro/gambatte-libretro.git "$SRC"
git -C "$SRC" fetch -q origin 2>/dev/null || true
git -C "$SRC" checkout -q -f "$PIN"
git -C "$SRC" clean -qfdx

docker run --rm -v "$SRC:/src:z" -w /src $IMAGE sh -c '
set -e
make -f Makefile.libretro platform=unix CC=aarch64-linux-gnu-gcc CXX=aarch64-linux-gnu-g++ -j"$(nproc)" >/src/build.log 2>&1
aarch64-linux-gnu-strip --strip-unneeded -o /src/gambatte_libretro.stripped.so /src/gambatte_libretro.so
[ "$(aarch64-linux-gnu-nm -D -u /src/gambatte_libretro.stripped.so | grep -v " w " | grep -vc @)" = 0 ] \
  || { echo "!! undefined symbols" >&2; exit 1; }
g=$(aarch64-linux-gnu-objdump -T /src/gambatte_libretro.stripped.so | grep -oE "GLIBC_2\.[0-9]+" | sort -uV | tail -1)
echo "glibc: $g"
[ "$(echo "$g" | cut -d. -f2)" -le 33 ] || { echo "!! needs $g, the Brick has 2.33" >&2; exit 1; }
' 2>&1 | tail -4

SO=$SRC/gambatte_libretro.stripped.so
[ -f "$SO" ] || { echo "!! build failed (see $SRC/build.log)" >&2; exit 1; }
GOT=$(sha256sum "$SO" | cut -d' ' -f1)
if [ -n "$SHA" ] && [ "$GOT" != "$SHA" ]; then
	echo "!! gambatte core sha256 $GOT, expected $SHA" >&2
	echo "  The toolchain image or the source changed. Verify the core, then update GAMBATTE_SHA here." >&2
	exit 1
fi
mkdir -p "$ROOT/vendor/cores-brick"
cp "$SO" "$ROOT/vendor/cores-brick/gambatte_libretro.so"
echo "  built  gambatte ($PIN) for the aarch64 ports"
echo "  sha256 $GOT"
