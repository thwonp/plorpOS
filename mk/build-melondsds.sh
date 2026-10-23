#!/bin/sh
# Build the Nintendo DS core, melonDS DS, for the RG Rotate (plorpos-3yv.51).
#
# Built rather than fetched, for three things the buildbot binary does not have:
#
# mk/patches/melondsds-plorpos.patch - the resident died on every second DS
# game: LoadGame's ApplyConfig dereferenced the null console through the render
# state the last game left behind (RetroArch never sees it; it unloads the core
# between games, Diatom keeps it, ADR-0006). Unload now leaves a fresh core,
# which also frees the core's copy of the ROM (128 MiB for Dragon Quest V) and
# the stale savestate size. And states from before the patch below still load.
#
# mk/patches/melonds-plorpos.patch, on the melonDS the core vendors -
#  - CRC32 on the ARMv8 CRC instructions (bit-identical, 35x): every savestate
#    CRCs the cart binaries, a quarter of each rewind capture;
#  - a DS-mode state holds the console's 4 MiB of main RAM, not all 16:
#    19.7 -> 7.2 MB, serialize 13.7 -> 4.4 ms (savestate minor 1; older states
#    still load);
#  - JIT code slices give their pages back when freed (34 MB stayed resident).
#  - "attempt to restore CPSR under bad mode" at Debug, not Warn: Pokemon
#    Pearl does it ~750,000 times a second, harmlessly, and Diatom writes every
#    Warn to the SD card (plorpos-3yv.81/.83).
#
# And PGO: profile-guided optimization, trained on the device with the
# mk/pgo/melondsds.tgz profile (tools/ds-pgo-train.sh regenerates it). The
# emulation is mostly C++ hardware emulation, not JIT output, so the profile
# pays: Pokemon's 3D intro 10.0 -> 8.6 ms per frame.
#
# The core vendors JesseTG/melonDS at a branch name; MELONDS pins the commit
# that branch had for 1.4.0 (= upstream master 906e9eb + one GL-only fix).
#
#   mk/build-melondsds.sh                 build with the profile, install to vendor/cores-rotate
#   mk/build-melondsds.sh --instrument    build the training core -> $SRC/out/melondsds_libretro.pgo.so
#
# Needs: docker (podman), network for the first clone. ~3 minutes.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=${SRC:-/tmp/tortos-melondsds}
MODE=${1:-use}
CORE_PIN=f394adbacb5722ee97c1b37c8064da9a25818310     # JesseTG/melonds-ds v1.4.0
MELONDS=16f127dbb587a73371827ce79689c5d237c4b59b      # JesseTG/melonDS jtg/fix-uninitialized-opengl
SHA=b9a11a2894fd1a73b999bb006e161fd6476505b4bf7a569a8dfd408a741f4f81
IMAGE=rotate-cores-cross
FLAGS="-mcpu=cortex-a75.cortex-a55"

command -v docker >/dev/null || { echo "need docker" >&2; exit 1; }
docker image inspect $IMAGE >/dev/null 2>&1 ||
	docker build -q -t $IMAGE -f "$ROOT/mk/rotate-cores.Dockerfile" "$ROOT/mk" >/dev/null

clone() { # dir url commit
	[ -d "$1/.git" ] || git clone -q "$2" "$1"
	git -C "$1" fetch -q origin "$3" 2>/dev/null || git -C "$1" fetch -q origin
	git -C "$1" checkout -q -f "$3"
	git -C "$1" clean -qfdx
}
mkdir -p "$SRC/out"
clone "$SRC/melonds-ds" https://github.com/JesseTG/melonds-ds.git "$CORE_PIN"
clone "$SRC/melonDS" https://github.com/JesseTG/melonDS.git "$MELONDS"
git -C "$SRC/melonds-ds" apply "$ROOT/mk/patches/melondsds-plorpos.patch"
git -C "$SRC/melonDS" apply "$ROOT/mk/patches/melonds-plorpos.patch"

# The profile is keyed by each object's path, so both modes build in /out/build
# and the dependencies in /out/deps; the downloaded dependency sources stay.
rm -rf "$SRC/out/build" "$SRC/out"/deps/*-build "$SRC/out"/deps/*-subbuild
find "$SRC/out" -name '*.gcda' -delete
if [ "$MODE" = --instrument ]; then
	FLAGS="$FLAGS -fprofile-generate -fprofile-update=atomic"
	LFLAGS=-fprofile-generate
else
	tar xzf "$ROOT/mk/pgo/melondsds.tgz" -C "$SRC/out"
	FLAGS="$FLAGS -fprofile-use -fprofile-partial-training -fprofile-correction -Wno-missing-profile"
	LFLAGS=
fi

docker run --rm -v "$SRC/melonds-ds:/src" -v "$SRC/melonDS:/melonds" -v "$SRC/out:/out" \
	-v "$ROOT/mk:/mk:ro" $IMAGE sh -c "
set -e
cmake -S /src -B /out/build -G Ninja -DCMAKE_TOOLCHAIN_FILE=/mk/rotate-aarch64.cmake \
  -DCMAKE_BUILD_TYPE=Release -DFETCHCONTENT_BASE_DIR=/out/deps \
  -DFETCHCONTENT_SOURCE_DIR_MELONDS=/melonds -DFETCHCONTENT_UPDATES_DISCONNECTED=ON \
  -DENABLE_OPENGL=OFF -DENABLE_JIT=ON -DENABLE_THREADED_RENDERER=ON \
  -DCMAKE_C_FLAGS='$FLAGS' -DCMAKE_CXX_FLAGS='$FLAGS' -DCMAKE_SHARED_LINKER_FLAGS='$LFLAGS' \
  > /out/configure.log 2>&1 || { tail -30 /out/configure.log; exit 1; }
cmake --build /out/build -j\$(nproc) > /out/build.log 2>&1 || { grep -m5 -A8 error /out/build.log; exit 1; }
aarch64-linux-gnu-strip --strip-unneeded -o /out/melondsds_libretro.so \
  /out/build/src/libretro/melondsds_libretro.so
so=/out/melondsds_libretro.so
# Never ship one that cannot load: an unversioned undefined symbol will not dlopen.
[ \"\$(aarch64-linux-gnu-nm -D -u \$so | grep -v ' w ' | grep -vc '@')\" = 0 ] \
  || { echo '!! undefined symbols - the core will not dlopen' >&2; exit 1; }
# Nothing newer than trixie's glibc (2.41).
aarch64-linux-gnu-objdump -T \$so | grep -oE 'GLIBC_2\.[0-9]+' | sort -uV | tail -1
"

SO=$SRC/out/melondsds_libretro.so
if [ "$MODE" = --instrument ]; then
	mv "$SO" "$SRC/out/melondsds_libretro.pgo.so"
	echo "  training core: $SRC/out/melondsds_libretro.pgo.so (tools/ds-pgo-train.sh)"
	exit 0
fi
GOT=$(shasum -a 256 "$SO" | cut -d' ' -f1)
if [ "$GOT" != "$SHA" ]; then
	echo "!! melondsds sha256 $GOT, expected $SHA" >&2
	echo "  The toolchain image, the source, the patches or the profile changed." >&2
	echo "  Verify the core, then update SHA here." >&2
	exit 1
fi
mkdir -p "$ROOT/vendor/cores-rotate"
cp "$SO" "$ROOT/vendor/cores-rotate/"
echo "  built  melondsds 1.4.0 + plorpOS patches + PGO"
echo "  sha256 $GOT"
