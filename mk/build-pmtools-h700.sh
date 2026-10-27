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
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$ROOT/build/h700
DL=$OUT/pmtools-dl
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
ln -s /mnt/SDCARD/TortOS/lib/libSDL2-2.0.so.0 "$DL/tree/lib/libSDL2.so"
# zip too: Mindustry's script repacks its jar with it, and BaseOS has unzip only.
# Libraries the ports' runtimes link and BaseOS lacks: LOVE (Balatro) wants
# OpenAL, Vorbis and mpg123. Debian bookworm's arm64 builds, which is BaseOS's
# userland (glibc 2.36, gcc 12's libstdc++: trixie's libopenal wants 2.38).
# What they need besides libc and libasound is carried here too (sndio, bsd,
# md, Ogg, Vorbis). The versions that went in are listed in lib/VERSIONS.
PM_DEBS="libopenal1 libsndio7.0 libbsd0 libmd0 libvorbisfile3 libvorbis0a libvorbisenc2 libmpg123-0 libogg0 zip"
docker run --rm -v "$DL:/s:Z" docker.io/library/debian:bookworm-slim sh -c "
	dpkg --add-architecture arm64 && apt-get update -qq > /dev/null 2>&1 &&
	apt-get install -y -qq squashfs-tools > /dev/null 2>&1 &&
	mkdir -p /debs /x && cd /debs &&
	apt-get download \$(for d in $PM_DEBS; do printf '%s:arm64 ' \$d; done) > /dev/null 2>&1 &&
	for f in *.deb; do dpkg-deb -x \$f /x; echo \$f >> /s/tree/lib/VERSIONS; done &&
	for so in libopenal.so.1 libvorbisfile.so.3 libvorbis.so.0 libvorbisenc.so.2 libmpg123.so.0 libogg.so.0 libsndio.so.7.0 libbsd.so.0 libmd.so.0; do
		cp -L /x/usr/lib/aarch64-linux-gnu/\$so /s/tree/lib/\$so || exit 1
	done &&
	cp /x/usr/bin/zip /s/tree/bin/zip &&
	mkdir -p /s/tree/lib/licenses && cp -r /x/usr/share/doc/. /s/tree/lib/licenses/ &&
	mksquashfs /s/tree /s/pmtools.squashfs -comp xz -all-root -noappend -quiet"
mv "$DL/pmtools.squashfs" "$OUT/pmtools.squashfs"
rm -rf "$DL/tree"
echo "$OUT/pmtools.squashfs  $(du -h "$OUT/pmtools.squashfs" | cut -f1)"
