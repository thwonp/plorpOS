#!/bin/sh
# adbd for the Anbernic RG DS (plorpos-rgds.11): ROCKNIX has none.
#
# Debian buster's arm64 android-tools-adbd (5.1.1) and the three libraries it
# needs that ROCKNIX lacks, as Debian built them, each pinned by sha256, into
# vendor/adbd-rgds/ with every package's copyright file. mk/payload.sh ships
# that folder as <card>/plorpOS/rgds/adb/, beside sd/rgds/adb/adb.sh.
#
# Usage: mk/fetch-rgds-adbd.sh
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$ROOT/vendor/adbd-rgds
DL=${DL:-/tmp/plorpos-adbd}
POOL=http://archive.debian.org/debian/pool/main

for t in curl sha256sum ar tar; do command -v $t > /dev/null || { echo "need $t" >&2; exit 1; }; done
mkdir -p "$DL/root" "$OUT/licenses"

get() { # pool path, sha256, package name
	F=$DL/$(basename "$1")
	[ "$(sha256sum "$F" 2> /dev/null | cut -d' ' -f1)" = "$2" ] || curl -sSfL -o "$F" "$POOL/$1"
	GOT=$(sha256sum "$F" | cut -d' ' -f1)
	[ "$GOT" = "$2" ] || { echo "  MISMATCH $3: want $2 got $GOT" >&2; exit 1; }
	ar p "$F" data.tar.xz | tar -xJ -C "$DL/root"
	cp "$DL/root/usr/share/doc/$3/copyright" "$OUT/licenses/$3"
	echo "  ok      $3"
}
get a/android-tools/android-tools-adbd_5.1.1.r38-1.1_arm64.deb \
    93ba9d9a28c60714cd3934fb0c445c791cf5e6bd898d09a4fb9914a70c4764dd android-tools-adbd
get libb/libbsd/libbsd0_0.9.1-2+deb10u1_arm64.deb \
    38a16bfa67657ba928b4ff880a93769d729fde1bb1d46d87a7bbca5e85634f20 libbsd0
get p/pcre3/libpcre3_8.39-12_arm64.deb \
    cf959aa722666827ec498a255ec14d20dd4ec3859446bb33de5332417588ff9f libpcre3
get o/openssl/libssl1.1_1.1.1n-0+deb10u3_arm64.deb \
    9d6db56bf17ec079675e0f6f7772af65b832dd3ad55c02c5409462cf6b052e23 libssl1.1

L=$DL/root/usr/lib/aarch64-linux-gnu
install -m 755 "$DL/root/usr/bin/adbd" "$OUT/adbd"
cp -L "$L/libbsd.so.0" "$L/libcrypto.so.1.1" "$DL/root/lib/aarch64-linux-gnu/libpcre.so.3" "$OUT/"
rm -rf "$DL/root"
ls -l "$OUT"
