#!/bin/sh
# The sysroot for `make PLATFORM=gkdb`: the GKD 350H Ultra on the BasedOS fork
# (plorpos-gkd.94). No device needed.
#
# SDL2, SDL2_image and SDL2_ttf - headers and libraries - come out of the
# fork's Buildroot output (thwonp/BasedOS, tools/buildroot/build.sh). The base
# ships those very SDL2_image and SDL2_ttf builds. Its SDL2 is BasedOS's own
# (tools/sdl2, 2.32.10), newer than Buildroot's 2.30.12 here: what links
# against the older one runs on the newer, and the link refuses any SDL call
# the device might lack.
#
# glibc is not copied: the toolchain's own (2.31) links, and the device's 2.41
# runs what it links, as on ROCKNIX (mk/fetch-gkd-sysroot.sh).
#
# FFmpeg for Muse is the H700's build (mk/fetch-h700-sysroot.sh), copied whole
# with its headers and licence texts. BasedOS has none; it ships on the card,
# as on the Zero 40 (mk/payload.sh).
#
# Usage: [BR_OUT=<Buildroot output>] mk/fetch-gkdb-sysroot.sh [out-dir]
#   BR_OUT   default ~/basedos-host/basedos-br/out, where the fork's
#            tools/container/run.sh leaves it
#   out-dir  default sysroot-gkdb/
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-$ROOT/sysroot-gkdb}
BR_OUT=${BR_OUT:-$HOME/basedos-host/basedos-br/out}
BR=$BR_OUT/host/aarch64-buildroot-linux-gnu/sysroot/usr
H700=$ROOT/sysroot-h700/usr

[ -f "$BR/include/SDL2/SDL.h" ] || {
	echo "no Buildroot output at $BR_OUT; build the fork first (BasedOS: tools/buildroot/build.sh build)" >&2; exit 1; }
[ -f "$H700/lib/libavformat.so.60" ] || {
	echo "no H700 sysroot for Muse's FFmpeg; run: mk/fetch-h700-sysroot.sh" >&2; exit 1; }
command -v readelf > /dev/null || { echo "need readelf (binutils)" >&2; exit 1; }

LIB=$OUT/usr/lib
INC=$OUT/usr/include
mkdir -p "$LIB" "$INC"

# One library, under its soname.
take() {
	[ -e "$BR/lib/$1" ] || { echo "Buildroot output has no /usr/lib/$1" >&2; exit 1; }
	cp -f "$(readlink -f "$BR/lib/$1")" "$LIB/$1"
}

echo "SDL from the Buildroot output"
for base in SDL2-2.0 SDL2_image-2.0 SDL2_ttf-2.0; do
	take "lib$base.so.0"
	ln -sf "lib$base.so.0" "$LIB/lib${base%-2.0}.so"   # what -lSDL2 etc. find
done
rm -rf "$INC/SDL2"
cp -r "$BR/include/SDL2" "$INC/SDL2"

# Every NEEDED of every library here, until nothing is missing, so the linker
# follows each one through -rpath-link. glibc's sonames are the toolchain's.
GLIBC='libc.so.6 libm.so.6 libdl.so.2 libpthread.so.0 librt.so.1 libresolv.so.2
libutil.so.1 ld-linux-aarch64.so.1 libgcc_s.so.1'
needed() {
	for f in "$LIB"/*.so*; do readelf -d "$f" 2> /dev/null; done |
		sed -n 's/.*(NEEDED).*\[\(.*\)\].*/\1/p' | sort -u
}
pass=0
while :; do
	pass=$((pass + 1))
	[ $pass -le 10 ] || { echo "dependency closure did not converge" >&2; exit 1; }
	missing=
	for so in $(needed); do
		[ -e "$LIB/$so" ] && continue
		case " $(echo $GLIBC) " in *" $so "*) continue ;; esac
		missing="$missing $so"
	done
	[ -n "$missing" ] || break
	for so in $missing; do echo "  + $so"; take "$so"; done
done

echo "FFmpeg from the H700 sysroot"
for l in libavformat libavcodec libavfilter libswresample libavutil; do
	cp -a "$H700/lib/$l".so* "$LIB/"
	rm -rf "$INC/$l"
	cp -r "$H700/include/$l" "$INC/$l"
done
cp "$H700/lib/COPYING.LGPLv3" "$H700/lib/COPYING.GPLv3" "$H700/lib/LICENSE.mbedtls" "$LIB/"

ver() { sed -n "s/^#define $2 *\([0-9]*\).*/\1/p" "$INC/SDL2/$1" | head -1; }
{
	echo "generated: $(date +%Y-%m-%d)"
	echo "buildroot: $BR_OUT"
	echo "sdl2:      $(ver SDL_version.h SDL_MAJOR_VERSION).$(ver SDL_version.h SDL_MINOR_VERSION).$(ver SDL_version.h SDL_PATCHLEVEL) (the device runs BasedOS's own 2.32.10)"
	echo "ffmpeg:    $(sed -n 's/^#define FFMPEG_VERSION "\(.*\)"/\1/p' "$INC/libavutil/ffversion.h") (sysroot-h700)"
} > "$OUT/PROVENANCE"

echo "done:"
ls "$LIB"
cat "$OUT/PROVENANCE"
