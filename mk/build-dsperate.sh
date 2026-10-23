#!/bin/sh
# Build the alternative Nintendo DS core, DSperate, for the RG Rotate
# (plorpos-3yv.80). dsperate-lr is our libretro wrapper; it carries DSperate
# itself as a submodule pinned to a release, so LR pins both.
#
# PGO: DSperate commits a GCC profile with each release and the wrapper's
# build.sh applies it - it has to be built with the profile's compiler (GCC
# 13.3, dsperate-lr's Ubuntu 24.04 Dockerfile) rather than our trixie image,
# and build.sh prints how much of the profile applied (expect 0 functions
# without theirs). Ubuntu 24.04's glibc (2.39) is older than trixie's, so the
# core loads on the card.
#
#   mk/build-dsperate.sh      build, install to vendor/cores-rotate
#
# Needs: docker (podman), network for the first clone. ~5 minutes.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=${SRC:-/tmp/tortos-dsperate}
REPO=${DSPERATE_LR:-$HOME/projects/git/dsperate-lr}
LR=a262c505fd1a78551c72d49ca40475fd4a39d697        # dsperate-lr, DSperate v3.0.5
SHA=0660843b26861291727f5efdddae95d6e7e3ac6fc3dac07c81bde117d1c179f7
IMAGE=dsperate-lr-cross

command -v docker >/dev/null || { echo "need docker" >&2; exit 1; }
[ -d "$SRC/.git" ] || git clone -q "$REPO" "$SRC"
git -C "$SRC" fetch -q origin 2>/dev/null || true
git -C "$SRC" checkout -q -f "$LR"
git -C "$SRC" submodule -q update --init --force
git -C "$SRC" clean -qfdx
docker image inspect $IMAGE >/dev/null 2>&1 ||
	docker build -q -t $IMAGE "$SRC" >/dev/null

docker run --rm -v "$SRC:/w" -w /w $IMAGE sh -c "
set -e
./build.sh /w/build | grep -v '^\['
so=/w/build/dsperate_libretro.so
# Never ship one that cannot load: an unversioned undefined symbol will not dlopen.
[ \"\$(aarch64-linux-gnu-nm -D -u \$so | grep -v ' w ' | grep -vc '@')\" = 0 ] \
  || { echo '!! undefined symbols - the core will not dlopen' >&2; exit 1; }
# Nothing newer than trixie's glibc (2.41).
aarch64-linux-gnu-objdump -T \$so | grep -oE 'GLIBC_2\.[0-9]+' | sort -uV | tail -1
"

SO=$SRC/build/dsperate_libretro.so
GOT=$(shasum -a 256 "$SO" | cut -d' ' -f1)
if [ -n "$SHA" ] && [ "$GOT" != "$SHA" ]; then
	echo "!! dsperate sha256 $GOT, expected $SHA" >&2
	echo "  The toolchain image, the source or the profile changed." >&2
	echo "  Verify the core, then update SHA here." >&2
	exit 1
fi
mkdir -p "$ROOT/vendor/cores-rotate"
cp "$SO" "$ROOT/vendor/cores-rotate/"
echo "  built  dsperate (dsperate-lr ${LR%${LR#???????}}) + PGO"
echo "  sha256 $GOT"
