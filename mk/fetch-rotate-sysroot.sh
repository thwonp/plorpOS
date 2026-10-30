#!/bin/sh
# Sysroot for the Anbernic RG Rotate (plorpos-3yv): the device runs Armbian's
# Debian trixie, so the headers and libraries to link against are trixie's own
# arm64 packages - the same ones the card image installs. Nothing is built.
#
# No arm64 emulation needed: an amd64 trixie container adds arm64 as a foreign
# architecture and only DOWNLOADS the packages (apt resolves the dependencies),
# then dpkg -x unpacks them. No maintainer script ever runs.
#
# apt reads a fixed snapshot, like mk/toolchain.Dockerfile, so every fetch gets
# the same packages. The toolchain stays the bullseye image: its glibc (2.31)
# is below trixie's 2.41, and cross.mk links with --allow-shlib-undefined.
#
#   mk/fetch-rotate-sysroot.sh [OUT]     (default: sysroot-rotate)
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-$ROOT/sysroot-rotate}
SNAP=20261015T000000Z
PKGS="libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev libasound2-dev
      libavformat-dev libavcodec-dev libavfilter-dev libswresample-dev libavutil-dev
      libegl-dev libgles-dev libdrm-dev libgbm-dev"

command -v docker > /dev/null || { echo "need docker (podman)" >&2; exit 1; }
rm -rf "$OUT"
mkdir -p "$OUT"
docker run --rm -i -v "$OUT:/out" \
	-e SNAP="$SNAP" -e PKGS="$(echo $PKGS)" debian:trixie-slim@sha256:918311b7b6c4c6f68b232ba516584925f6c78ad82b6fd534b98979df6438e483 sh -s << 'EOF'
set -eu
rm -f /etc/apt/sources.list.d/*
printf '%s\n' "deb http://snapshot.debian.org/archive/debian/$SNAP trixie main" \
	"deb http://snapshot.debian.org/archive/debian-security/$SNAP trixie-security main" \
	> /etc/apt/sources.list
dpkg --add-architecture arm64
apt-get -qq -o Acquire::Check-Valid-Until=false update
arm=$(for p in $PKGS; do printf '%s:arm64 ' "$p"; done)
apt-get -qq install -y --download-only --no-install-recommends $arm > /dev/null
mkdir -p /tmp/x
# Not glibc or the kernel headers: libc is the toolchain's (its 2.31 is the
# floor the binary demands), and trixie's libm.so is a linker script naming
# /lib/aarch64-linux-gnu paths that only exist on the device.
for d in /var/cache/apt/archives/*_arm64.deb; do
	case ${d##*/} in libc6_*|libc6-dev_*|linux-libc-dev_*) continue ;; esac
	dpkg -x "$d" /tmp/x
done
# Flatten to the layout cross.mk expects: libraries in usr/lib, the arch
# headers (SDL2's _real_SDL_config.h, FFmpeg's avconfig.h ...) in usr/include.
mkdir -p /out/usr/lib /out/usr/include
cp -a /tmp/x/usr/include/. /out/usr/include/
# -n: the arch tree also holds relative links BACK to the generic headers
# (SDL2/SDL_platform.h -> ../../SDL2/...), which must not replace them.
cp -an /tmp/x/usr/include/aarch64-linux-gnu/. /out/usr/include/
cp -a /tmp/x/usr/lib/aarch64-linux-gnu/. /out/usr/lib/
[ -d /tmp/x/lib/aarch64-linux-gnu ] && cp -a /tmp/x/lib/aarch64-linux-gnu/. /out/usr/lib/
ls /var/cache/apt/archives/*_arm64.deb | wc -l > /out/.debs
EOF
echo "sysroot: $OUT ($(cat "$OUT/.debs") packages)"
