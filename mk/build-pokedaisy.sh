#!/bin/sh
# Fetch PokeDaisy's Linux host for GBA games set to Play With PokeDaisy
# (plorpos-c9p): the Linux host of lidor30/pokedaisy (mGBA plus the Pokemon
# companion, GPL-3.0) as plorpOS's fork, https://github.com/thwonp/pokedaisy-plorp.
# A separate program the launcher runs, never linked into it.
#
# One binary for every device (libSDL2 + glibc 2.27 only, linked against the
# Brick's SDL2). The fork's release asset pokedaisy-plorpos-<tag>-aarch64.tar.gz
# holds it with its licenses and SOURCE.txt; this downloads the pinned tag and
# checks the pinned sha256 (plorpos-l22.16).
#
#   mk/build-pokedaisy.sh    -> vendor/pokedaisy/ (mk/plorpos-files.sh ships it)
#
# Working on the fork: POKEDAISY_SRC=<checkout of thwonp/pokedaisy-plorp> builds
# that tree with its plorpos/build.sh instead (needs docker, the
# tortos-toolchain image, sysroot/ from mk/fetch-sysroot.sh; ~2 minutes).
# SPDX-License-Identifier: MIT
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TAG=plorpos-v1
SHA256=unset   # of the .tar.gz; the release's .sha256 file has it
OUT=$ROOT/vendor/pokedaisy
URL=https://github.com/thwonp/pokedaisy-plorp/releases/download/$TAG/pokedaisy-plorpos-$TAG-aarch64.tar.gz
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT

if [ -n "$POKEDAISY_SRC" ]; then
	[ -d "$ROOT/sysroot/usr/lib" ] || { echo "need sysroot/ (mk/fetch-sysroot.sh)" >&2; exit 1; }
	SYSROOT=$ROOT/sysroot TOOLCHAIN_IMAGE=${TOOLCHAIN_IMAGE:-tortos-toolchain} \
		sh "$POKEDAISY_SRC/plorpos/build.sh" dev | tail -4
	ARC=$(ls "$POKEDAISY_SRC"/plorpos/dist/pokedaisy-plorpos-dev-aarch64.tar.gz)
else
	[ "$SHA256" != unset ] || { echo "no sha256 pinned for $TAG yet; set POKEDAISY_SRC to build the fork locally" >&2; exit 1; }
	ARC=$T/asset.tar.gz
	curl -fsSL -o "$ARC" "$URL"
	[ "$(sha256sum "$ARC" | cut -d' ' -f1)" = "$SHA256" ] || { echo "!! sha256 mismatch for $URL" >&2; exit 1; }
fi

tar -C "$T" -xzf "$ARC"
BIN=$T/pokedaisy/pokedaisy-linux
[ -f "$BIN" ] || { echo "!! no pokedaisy-linux in $ARC" >&2; exit 1; }
# Never ship one a device cannot load: libSDL2 and libc only.
if command -v readelf >/dev/null; then
	readelf -d "$BIN" | grep NEEDED | grep -vqE "libSDL2-2.0|libm|libpthread|libc\." \
		&& { echo "!! unexpected library" >&2; exit 1; }
fi
rm -rf "$OUT"
mkdir -p "$ROOT/vendor"
mv "$T/pokedaisy" "$OUT"
echo "  fetched pokedaisy ${POKEDAISY_SRC:+(local fork build) }$TAG -> vendor/pokedaisy"
echo "  sha256 $(sha256sum "$OUT/pokedaisy-linux" | cut -d' ' -f1)"
