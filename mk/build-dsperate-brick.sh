#!/bin/sh
# Build the Nintendo DS core, DSperate, for the plain Brick (plorpos-reo.21).
# Same dsperate-lr wrapper as mk/build-dsperate.sh, a different toolchain: the
# Brick's firmware glibc is 2.33, and the GCC13/Ubuntu 24.04 build that serves
# the GKD and Rotate needs 2.38. Our own tortos-toolchain (Debian, GCC 10,
# glibc 2.17 symbols) loads. GCC 10 cannot use DSperate's committed PGO
# profile (made with GCC 13), so this core is built without it. DSperate's
# profile for GCC 10.5.0 does apply to our 10.2.1 (0 functions lost theirs, with
# its fingerprint re-stamped), and was measured on the Brick against this
# build in Pokemon Pearl and GTA: Chinatown Wars - identical frame skips, so
# the plain build ships. What decides the speed is the frame-skip rule in
# diatom (src/main.c), not the compiler.
#
#   mk/build-dsperate-brick.sh     build, install to vendor/cores-brick
#
# Needs: docker (podman), the tortos-toolchain image (make toolchain), network
# for the first clone. ~10 minutes.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=${SRC:-$HOME/.cache/tortos-dsperate-brick}
REPO=${DSPERATE_LR:-https://github.com/thwonp/dsperate-lr.git}
LR=093d50fcbd10531273e0cc22a23c6644a26b9b71 # the same pin as the GKD's
IMAGE=${TOOLCHAIN_IMAGE:-tortos-toolchain}
TUNE=${TUNE--mcpu=cortex-a53}

command -v docker >/dev/null || { echo "need docker" >&2; exit 1; }
[ -d "$SRC/.git" ] || git clone -q "$REPO" "$SRC"
git -C "$SRC" fetch -q origin 2>/dev/null || true
git -C "$SRC" checkout -q -f "$LR"
git -C "$SRC" submodule -q update --init --force
git -C "$SRC" clean -qfdx
# The wrapper's fixes for the pinned DSperate, on the host: the toolchain
# image has no git.
for p in "$SRC"/patches/*.patch; do
	[ -e "$p" ] || continue
	git -C "$SRC/DSperate" apply "$p"
done
# The wrapper's frame skip: the core leaves a frame's rendering out when the
# frontend says it will run late (plorpos-reo.21.7; slotPlus does the same).
git -C "$SRC" apply "$ROOT/mk/patches/dsperate-lr-frameskip.patch"

docker run --rm -e TUNE="$TUNE" -v "$SRC:/w:z" -w /w $IMAGE sh -c '
set -e
O=/w/build
TC=/w/DSperate/cmake/aarch64-linux-gnu.cmake
CFG="-G \"Unix Makefiles\" -DCMAKE_TOOLCHAIN_FILE=$TC \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_PROJECT_INCLUDE=/w/cmake/dsperate-pic.cmake \
  -DDSPERATE_TUNE=${TUNE} \
  -DDSPERATE_TESTS=OFF -DDSPERATE_HEADLESS=OFF -DDSPERATE_SDL=OFF \
  -DDSPERATE_CHEEVOS=OFF -DDSPERATE_NET=OFF"
eval cmake -S /w/DSperate -B $O/dsperate $CFG -DDSPERATE_PGO=off >$O.cfg1.log
cmake --build $O/dsperate --target dsperate_core -j"$(nproc)" >$O.build1.log
cmake -S /w -B $O/lr -G "Unix Makefiles" -DCMAKE_TOOLCHAIN_FILE=$TC \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DDSPERATE_BUILD=$O/dsperate >$O.cfg2.log
cmake --build $O/lr -j"$(nproc)"
aarch64-linux-gnu-strip --strip-unneeded -o $O/dsperate_libretro.so $O/lr/dsperate_libretro.so
# Never ship one that cannot load on the Brick: nothing newer than its 2.33,
# and no unversioned undefined symbol.
[ "$(aarch64-linux-gnu-nm -D -u $O/dsperate_libretro.so | grep -v " w " | grep -vc @)" = 0 ] \
  || { echo "!! undefined symbols" >&2; exit 1; }
echo "glibc: $(aarch64-linux-gnu-objdump -T $O/dsperate_libretro.so | grep -oE "GLIBC_2\.[0-9]+" | sort -uV | tail -1)"
' 2>&1 | tail -15

SO=$SRC/build/dsperate_libretro.so
[ -f "$SO" ] || { echo "!! build failed (see $SRC/build.*.log)" >&2; exit 1; }
mkdir -p "$ROOT/vendor/cores-brick"
cp "$SO" "$ROOT/vendor/cores-brick/"
echo "  built  dsperate (dsperate-lr ${LR%${LR#???????}}) for Brick, no PGO"
echo "  sha256 $(sha256sum "$SO" | cut -d' ' -f1)"
