#!/bin/sh
# build/zero40/lib32.squashfs: a 32-bit ARM userland for the Zero 40's armhf
# PortMaster ports (plorpos-auv.7). oakMOSS is 64-bit only, and the GE8300
# has no 32-bit driver at all: these ports draw with Mesa's llvmpipe into an
# offscreen surface, and tools/fbpresent32.c (preloaded) copies each frame to
# the panel. Debian bookworm's armhf packages (glibc 2.36). The versions that
# went in are listed in VERSIONS; their licences in share/doc.
#
#   lib/arm-linux-gnueabihf/   every library, ld-linux-armhf.so.3, dri/, the shim
#   share/glvnd/egl_vendor.d/  Mesa's EGL vendor file
#
# pm-run.sh links /usr/lib/arm-linux-gnueabihf (the loader's own search path)
# and /usr/lib32 (what ports written for ROCKNIX and BasedOS look in) to the
# tree: no environment, which port scripts rewrite (Westonpack's eval made
# a $LIB in LD_PRELOAD empty). The shim is preloaded by the loader itself:
# this copy reads /tmp/plorpos32.pre, not /etc/ld.so.preload (same length),
# so only a 32-bit process ever loads it.
#
# Needs docker. sd/zero40/portmaster/pm-run.sh mounts it for a run.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$ROOT/build/zero40
W=$OUT/lib32-work
rm -rf "$W"
mkdir -p "$W/src" "$OUT"
cp "$ROOT/tools/fbpresent32.c" "$W/src/"
PKGS="libc6 libstdc++6 libgcc-s1 libsdl2-2.0-0 libopenal1 libzip4 libfreetype6 libbz2-1.0 zlib1g libasound2 libegl1 libegl-mesa0 libgles2 libgl1-mesa-dri libglapi-mesa libgbm1 libpng16-16 libudev1"
docker run --rm -v "$W:/w:Z" docker.io/library/debian:bookworm-slim sh -c "
	set -e
	dpkg --add-architecture armhf && apt-get update -qq > /dev/null 2>&1
	apt-get install -y -qq --no-install-recommends gcc-arm-linux-gnueabihf libc6-dev-armhf-cross \
		libegl-dev:armhf libgles-dev:armhf squashfs-tools \$(for p in $PKGS; do printf '%s:armhf ' \$p; done) > /dev/null 2>&1
	T=/w/tree; mkdir -p \$T/lib/arm-linux-gnueabihf \$T/share/glvnd/egl_vendor.d \$T/share/doc
	cp -a /usr/lib/arm-linux-gnueabihf/. \$T/lib/arm-linux-gnueabihf/
	rm -rf \$T/lib/arm-linux-gnueabihf/gconv \$T/lib/arm-linux-gnueabihf/*.a \$T/lib/arm-linux-gnueabihf/pkgconfig
	# No desktop GL: what these ports draw with is GLES, and a libGL in
	# /usr/lib32 has PortMaster offer opengl ports the Zero 40 cannot run.
	rm -f \$T/lib/arm-linux-gnueabihf/libGL.so* \$T/lib/arm-linux-gnueabihf/libGLX*.so* \$T/lib/arm-linux-gnueabihf/libOpenGL.so*
	# Libraries Debian keeps in a subfolder behind an absolute RUNPATH.
	cp -a /usr/lib/arm-linux-gnueabihf/pulseaudio/. \$T/lib/arm-linux-gnueabihf/
	sed 's|/etc/ld.so.preload|/tmp/plorpos32.pre|' /usr/lib/ld-linux-armhf.so.3 > \$T/lib/arm-linux-gnueabihf/ld-linux-armhf.so.3
	chmod 755 \$T/lib/arm-linux-gnueabihf/ld-linux-armhf.so.3
	grep -q /tmp/plorpos32.pre \$T/lib/arm-linux-gnueabihf/ld-linux-armhf.so.3 || { echo 'lib32: loader not patched' >&2; exit 1; }
	cp /usr/share/glvnd/egl_vendor.d/50_mesa.json \$T/share/glvnd/egl_vendor.d/
	arm-linux-gnueabihf-gcc -O2 -Wall -shared -fPIC -I/usr/include -o \$T/lib/arm-linux-gnueabihf/fbpresent32.so /w/src/fbpresent32.c -ldl
	for p in $PKGS; do d=/usr/share/doc/\$p; [ -d \$d ] || d=/usr/share/doc/\$p:armhf; [ -d \$d ] && cp -rL \$d \$T/share/doc/\$p; dpkg-query -W -f='\${Package} \${Version}\n' \$p:armhf >> \$T/VERSIONS; done
	# Every library's needs met inside the tree (no system armhf library).
	arm-linux-gnueabihf-readelf -d \$T/lib/arm-linux-gnueabihf/libSDL2-2.0.so.0 | grep -q NEEDED || { echo 'lib32: no readelf' >&2; exit 1; }
	cd \$T/lib/arm-linux-gnueabihf && miss=\$(for f in *.so* dri/*.so; do [ -f \$f ] && arm-linux-gnueabihf-readelf -d \$f 2>/dev/null | sed -n 's/.*Shared library: \\[\\(.*\\)\\]/\\1/p'; done | sort -u | while read n; do [ -e \$n ] || echo \$n; done) && cd /
	[ -z \"\$miss\" ] || { echo \"lib32: not in the tree: \$miss\" >&2; exit 1; }
	mksquashfs \$T /w/lib32.squashfs -comp xz -all-root -noappend -quiet
"
mv "$W/lib32.squashfs" "$OUT/lib32.squashfs"
rm -rf "$W"
echo "$OUT/lib32.squashfs  $(du -h "$OUT/lib32.squashfs" | cut -f1)"
