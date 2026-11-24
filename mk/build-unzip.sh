#!/bin/sh
# unzip for the ports whose system has none - the Brick, Brick Pro and Rotate -
# so the update check (plorpos-koo) can unpack the same release zip people copy
# by hand. BusyBox with the unzip applet alone, static, built in the Brick's
# toolchain image: one aarch64 binary that runs on both C libraries.
#   mk/build-unzip.sh
# Output: build/unzip-aarch64/unzip and LICENSE-busybox.txt
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
VER=1.37.0
SUM=3311dff32e746499f4df0d5df04d7eb396382d7e108bb9250e7b519b837043a4
IMAGE=${IMAGE:-localhost/tortos-toolchain}
DL=${DL:-$HOME/projects/misc/coding-team/plorpos-miyoo-mini-flip/dist-inputs/src}
F=$DL/busybox-$VER.tar.bz2
W=$R/build/busybox-src
O=$R/build/unzip-aarch64

mkdir -p "$DL"
[ -s "$F" ] || curl -sSfL --max-time 300 -o "$F" "https://busybox.net/downloads/busybox-$VER.tar.bz2"
echo "$SUM  $F" | sha256sum -c --quiet -
rm -rf "$W"; mkdir -p "$W"
tar -C "$W" --strip-components=1 -xjf "$F"
# allnoconfig, then only what unpacking a release zip needs: deflate (zip -r
# writes nothing else) and the central directory, which knows the sizes of
# entries written with a data descriptor.
# The image cross-compiles only; BusyBox's kconfig wants a host gcc too.
docker run --rm -u 0 -v "$W":/src:Z -w /src "$IMAGE" sh -ec '
	command -v gcc >/dev/null && command -v bzip2 >/dev/null || { apt-get -qq -o Acquire::Check-Valid-Until=false update && apt-get -qq install -y gcc libc6-dev bzip2 >/dev/null; }
	make -s allnoconfig >/dev/null
	for o in STATIC UNZIP FEATURE_UNZIP_CDF LFS; do
		sed -i "s/^# CONFIG_$o is not set/CONFIG_$o=y/" .config
	done
	yes "" | make -s oldconfig >/dev/null
	make -s -j"$(nproc)" CROSS_COMPILE=aarch64-linux-gnu- busybox 2>&1 | grep -v "warning: Using .getpw" || true
	aarch64-linux-gnu-strip busybox'
grep -q '^CONFIG_UNZIP=y' "$W/.config" && grep -q '^CONFIG_STATIC=y' "$W/.config" ||
	{ echo "build-unzip: config did not take" >&2; exit 1; }
mkdir -p "$O"
cp "$W/busybox" "$O/unzip"   # BusyBox runs the applet named by argv[0]
cp "$W/LICENSE" "$O/LICENSE-busybox.txt"
echo "unzip: $O/unzip ($(wc -c < "$O/unzip") B, $(md5sum < "$O/unzip" | cut -c1-8))"
