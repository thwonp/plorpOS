#!/bin/sh
# build/h700/pmtools.squashfs: the python3 and bash PortMaster needs and BaseOS
# lacks, as ONE squashfs that sd/h700/portmaster/pm-run.sh loop-mounts for a
# run (plorpos-2wl.10). The card is exFAT with 128 KB clusters: python's 4500
# small files would take 660 MB there, and exFAT cannot hold symlinks.
#
#   python-build-standalone cpython 3.12, aarch64-gnu, install_only_stripped
#   robxu9/bash-static 5.2.015 (static: BaseOS has no bash)
#
# Needs docker (any: only to run mksquashfs from a debian image) and curl.
# Downloads are checked against the sums below and kept in build/h700/pmtools-dl.
#
#   mk/build-pmtools-h700.sh zero40 -> build/zero40/pmtools.squashfs: the same
#     image for the MagicX Zero 40 (plorpos-auv.7). oakMOSS lacks what BaseOS
#     lacks (python3, OpenAL, Vorbis, zip; its SDL2_mixer 2.0.1 has no Ogg, so
#     PortMaster's GUI died loading its menu sounds), but it has its own SDL2,
#     SDL2_image and SDL2_ttf (/usr/magicx/lib): the unversioned names point there.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
H700=$ROOT/build/h700
OUT=$H700
SDLDIR=/mnt/SDCARD/plorpOS/lib
if [ "${1:-}" = zero40 ]; then
	OUT=$ROOT/build/zero40
	SDLDIR=/usr/magicx/lib
fi
DL=$H700/pmtools-dl
PY_URL="https://github.com/astral-sh/python-build-standalone/releases/download/20261026/cpython-3.12.16%2B20261026-aarch64-unknown-linux-gnu-install_only_stripped.tar.gz"
PY_SHA=bb4b6392670c5d3f93876996fcaa19858a4a413a0c7720144c18b0d6f1f0db2d
BASH_URL="https://github.com/robxu9/bash-static/releases/download/5.2.015-1.2.3-2/bash-linux-aarch64"
BASH_SHA=8877ad33344af461ed801066322fd9a7808cd73e4e81087da228e32e8fad54ca

mkdir -p "$DL"
fetch() { # url sha file
	[ "$(sha256sum "$DL/$3" 2> /dev/null | cut -d' ' -f1)" = "$2" ] && return 0
	curl -fsSL -o "$DL/$3" "$1"
	[ "$(sha256sum "$DL/$3" | cut -d' ' -f1)" = "$2" ] || { echo "$3: checksum differs" >&2; exit 1; }
}
fetch "$PY_URL" "$PY_SHA" python.tar.gz
fetch "$BASH_URL" "$BASH_SHA" bash

rm -rf "$DL/tree"
mkdir -p "$DL/tree/bin"
tar xzf "$DL/python.tar.gz" -C "$DL/tree"
cp "$DL/bash" "$DL/tree/bash"
chmod +x "$DL/tree/bash"
ln -s ../bash "$DL/tree/bin/bash"
# What ports dlopen by a name BaseOS lacks. Apotris's audio (SoLoud) opens
# "libSDL2.so", and without it starts no audio and crashes at its first sound;
# BaseOS and our SDL2 build have only libSDL2-2.0.so.0. An absolute link to the
# card's copy: dlopen finds the same file, so the same SDL.
mkdir -p "$DL/tree/lib"
ln -s "$SDLDIR/libSDL2-2.0.so.0" "$DL/tree/lib/libSDL2.so"
if [ "$SDLDIR" = /usr/magicx/lib ]; then
	ln -s "$SDLDIR/libSDL2_ttf-2.0.so.0" "$DL/tree/lib/libSDL2_ttf.so"
	# The H700's SDL2_image (stb_image, no libpng) ahead of oakMOSS's 2.0.5,
	# which dlopens libpng 1.2: Ren'Py exports its own static libpng 1.6, and
	# libpng 1.2's calls bound to it ("incompatible PNG dll").
	cp "$(readlink -f "$ROOT/sysroot-h700/usr/lib/libSDL2_image-2.0.so.0")" "$DL/tree/lib/libSDL2_image-2.0.so.0"
	ln -s libSDL2_image-2.0.so.0 "$DL/tree/lib/libSDL2_image.so"
	# oakMOSS's BusyBox has no printenv; Westonpack's wrapper calls it.
	cat > "$DL/tree/bin/printenv" <<-'P'
	#!/bin/sh
	[ $# -eq 0 ] && exec env
	r=1
	for n; do eval "[ -n \"\${$n+x}\" ] && { printf '%s\\n' \"\$$n\"; r=0; }"; done
	exit $r
	P
	chmod +x "$DL/tree/bin/printenv"
fi
# PortMaster's own GUI (pugwash, pysdl2) needs SDL2_mixer, which BaseOS lacks:
# mk/build-sdl2mixer-h700.sh. pysdl2 asks for the name "SDL2_mixer".
[ -f "$H700/libSDL2_mixer-2.0.so.0" ] || { echo "no $H700/libSDL2_mixer-2.0.so.0; run: mk/build-sdl2mixer-h700.sh" >&2; exit 1; }
cp "$H700/libSDL2_mixer-2.0.so.0" "$DL/tree/lib/"
ln -s libSDL2_mixer-2.0.so.0 "$DL/tree/lib/libSDL2_mixer.so"
mkdir -p "$DL/tree/lib/licenses/SDL2_mixer"
cp "$H700/SDL2_mixer-LICENSE.txt" "$DL/tree/lib/licenses/SDL2_mixer/copyright"
# zip too: Mindustry's script repacks its jar with it, and BaseOS has unzip only.
# Libraries the ports' runtimes link and BaseOS lacks: LOVE (Balatro) wants
# OpenAL, Vorbis and mpg123. Debian bookworm's arm64 builds, which is BaseOS's
# userland (glibc 2.36, gcc 12's libstdc++: trixie's libopenal wants 2.38).
# What they need besides libc and libasound is carried here too (sndio, bsd,
# md, Ogg, Vorbis). The versions that went in are listed in lib/VERSIONS.
PM_DEBS="libopenal1 libsndio7.0 libbsd0 libmd0 libvorbisfile3 libvorbis0a libvorbisenc2 libmpg123-0 libogg0 zip"
PM_SOS="libopenal.so.1 libvorbisfile.so.3 libvorbis.so.0 libvorbisenc.so.2 libmpg123.so.0 libogg.so.0 libsndio.so.7.0 libbsd.so.0 libmd.so.0"
# The Zero 40 (plorpos-auv.7): what ports link and oakMOSS lacks besides -
# NXEngine (png16), To the Moon's mkxp (cairo, pixman, pulse-simple: linked,
# no server needed), LOVE builds (theora, modplug) - with everything they
# need that oakMOSS has not (its freetype is 2.6: Debian's goes with cairo).
if [ "$SDLDIR" = /usr/magicx/lib ]; then
	PM_DEBS="$PM_DEBS libpng16-16 libcairo2 libpixman-1-0 libfontconfig1 libfreetype6 libbrotli1 libpulse0 libasyncns0 libsystemd0 libcap2 liblzma5 liblz4-1 libzstd1 libtheora0 libmodplug1 libx11-6 libx11-xcb1 libxau6 libxcb1 libxcb-render0 libxcb-shm0 libxdmcp6 libxext6 libxrender1"
	# Westonpack's Weston wants libjpeg.so.8 (libjpeg-turbo's jpeg8 ABI, as on
	# ROCKNIX); Debian has only jpeg62, so Ubuntu 22.04's (glibc 2.17 symbols).
	fetch "http://ports.ubuntu.com/ubuntu-ports/pool/main/libj/libjpeg-turbo/libjpeg-turbo8_2.1.2-0ubuntu1_arm64.deb" \
		1c47447261097e6a2105f4ae6f0bf2c7b1e8c6b8209c8ea40286c69c20f43818 jpeg8.deb
	cp "$DL/jpeg8.deb" "$DL/tree/jpeg8.deb"
	PM_SOS="$PM_SOS libjpeg.so.8 libpng16.so.16 libcairo.so.2 libpixman-1.so.0 libfontconfig.so.1 libfreetype.so.6 libbrotlidec.so.1 libbrotlicommon.so.1 libpulse.so.0 libpulse-simple.so.0 libpulsecommon-16.1.so libasyncns.so.0 libsystemd.so.0 libcap.so.2 liblzma.so.5 liblz4.so.1 libzstd.so.1 libtheoradec.so.1 libmodplug.so.1 libX11.so.6 libX11-xcb.so.1 libXau.so.6 libxcb.so.1 libxcb-render.so.0 libxcb-shm.so.0 libXdmcp.so.6 libXext.so.6 libXrender.so.1"
fi
docker run --rm -v "$DL:/s:Z" docker.io/library/debian:bookworm-slim sh -c "
	dpkg --add-architecture arm64 && apt-get update -qq > /dev/null 2>&1 &&
	apt-get install -y -qq squashfs-tools > /dev/null 2>&1 &&
	mkdir -p /debs /x && cd /debs &&
	apt-get download \$(for d in $PM_DEBS; do printf '%s:arm64 ' \$d; done) > /dev/null 2>&1 &&
	mv /s/tree/jpeg8.deb . 2>/dev/null; for f in *.deb; do dpkg-deb -x \$f /x; echo \$f >> /s/tree/lib/VERSIONS; done &&
	for so in $PM_SOS; do
		f=\$(find /x -name \$so | head -n 1) && [ -n \"\$f\" ] && cp -L \"\$f\" /s/tree/lib/\$so || { echo \"no \$so\" >&2; exit 1; }
	done &&
	cp /x/usr/bin/zip /s/tree/bin/zip &&
	mkdir -p /s/tree/lib/licenses && cp -r /x/usr/share/doc/. /s/tree/lib/licenses/ &&
	mksquashfs /s/tree /s/pmtools.squashfs -comp xz -all-root -noappend -quiet"
mkdir -p "$OUT"
mv "$DL/pmtools.squashfs" "$OUT/pmtools.squashfs"
rm -rf "$DL/tree"
echo "$OUT/pmtools.squashfs  $(du -h "$OUT/pmtools.squashfs" | cut -f1)"
