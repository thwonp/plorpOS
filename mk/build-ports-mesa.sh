#!/bin/sh
# Build the RG Rotate's ports Mesa (plorpos-3yv.86.13): a second Mesa, only
# for PortMaster's ports, beside Debian's (which the launcher, Diatom, sway and
# Xwayland keep using).
#
# Debian's Mesa links libLLVM 19 into every GL process. PortMaster's Mono
# (mono-sgen) exports its own, older, static LLVM, so LLVM 19 bound to Mono's
# symbols and every Mono/FNA port (Stardew, TMNT, Axiom Verge, Celeste) died
# in libLLVM's initializers while SDL opened EGL. Panfrost needs no LLVM, so
# this one is built without it, and with GLES 1 (Debian's gives no GLES 1
# context: RVGL, plorpos-3yv.86.17) - like ROCKNIX's.
#
# No glvnd: Mesa's own libEGL, libGL, libGLESv1_CM, libGLESv2 and libgbm, all
# in one lib directory, so a port picks the whole stack up from
# LD_LIBRARY_PATH alone (sd/rotate/portmaster/pm-run.sh) and never mixes it
# with Debian's. Platforms Wayland (SDL) and X11 (GLX ports, Xwayland).
#
# The version is Debian trixie's, so it expects the same kernel driver and
# libdrm the system Mesa does.
#
#   mk/build-ports-mesa.sh   -> vendor/rotate-mesa/ (installs at /usr/lib/plorpos-mesa;
#                               mk/rotate-image.sh puts it in the card's rootfs)
#
# On a running Rotate (once):
#   tar -C vendor/rotate-mesa -cf - . | ssh root@ROTATE 'mkdir -p /usr/lib/plorpos-mesa && tar -C /usr/lib/plorpos-mesa -xf -'
#
# Needs: docker. ~1 GB of build files in $SRC (not /tmp: tmpfs on some hosts).
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=${SRC:-$HOME/.cache/plorpos-ports-mesa}
VER=25.0.7
SHA=592272df3cf01e85e7db300c449df5061092574d099da275d19e97ef0510f8a6
PREFIX=/usr/lib/plorpos-mesa
IMAGE=rotate-mesa-cross
OUT=$ROOT/vendor/rotate-mesa

command -v docker >/dev/null || { echo "need docker" >&2; exit 1; }
docker image inspect $IMAGE >/dev/null 2>&1 ||
	docker build -q -t $IMAGE -f "$ROOT/mk/rotate-mesa.Dockerfile" "$ROOT/mk" >/dev/null

mkdir -p "$SRC"
TAR=$SRC/mesa-$VER.tar.xz
[ -f "$TAR" ] || curl -sfL -o "$TAR" "https://archive.mesa3d.org/mesa-$VER.tar.xz"
[ "$(sha256sum "$TAR" | cut -d' ' -f1)" = "$SHA" ] || { echo "!! $TAR: wrong sha256" >&2; exit 1; }
rm -rf "$SRC/mesa-$VER" "$SRC/build" "$SRC/install"
tar -C "$SRC" -xf "$TAR"

docker run --rm -v "$SRC:/w" $IMAGE sh -c "
set -e
cat > /w/cross.txt <<EOF
[binaries]
c = 'aarch64-linux-gnu-gcc'
cpp = 'aarch64-linux-gnu-g++'
ar = 'aarch64-linux-gnu-ar'
strip = 'aarch64-linux-gnu-strip'
nm = 'aarch64-linux-gnu-nm'
pkg-config = 'pkg-config'
[properties]
pkg_config_libdir = ['/usr/lib/aarch64-linux-gnu/pkgconfig', '/usr/share/pkgconfig']
[host_machine]
system = 'linux'
cpu_family = 'aarch64'
cpu = 'cortex-a75'
endian = 'little'
EOF
meson setup /w/build /w/mesa-$VER --cross-file /w/cross.txt \
  --prefix=$PREFIX --libdir=lib --buildtype=release -Db_ndebug=true \
  -Dplatforms=wayland,x11 -Dgallium-drivers=panfrost -Dvulkan-drivers= \
  -Dllvm=disabled -Dglvnd=disabled -Dglx=dri -Dgles1=enabled -Dgles2=enabled \
  -Degl=enabled -Dgbm=enabled -Dshared-glapi=enabled -Dzstd=enabled -Dexpat=enabled \
  -Dgallium-va=disabled -Dgallium-vdpau=disabled -Dgallium-xa=disabled \
  -Dgallium-nine=false -Dgallium-rusticl=false \
  -Dvideo-codecs= -Dtools= -Dvalgrind=disabled -Dlibunwind=disabled \
  -Dlmsensors=disabled -Dmicrosoft-clc=disabled \
  -Dxlib-lease=disabled -Dbuild-tests=false \
  > /w/configure.log 2>&1 || { tail -30 /w/configure.log; exit 1; }
ninja -C /w/build > /w/build.log 2>&1 || { grep -m5 -A8 error /w/build.log; exit 1; }
DESTDIR=/w/install ninja -C /w/build install > /w/install.log 2>&1
find /w/install$PREFIX -name '*.so*' -type f -exec aarch64-linux-gnu-strip --strip-unneeded {} +
# The point of it: nothing may pull in LLVM.
! grep -rl libLLVM /w/install$PREFIX/lib || { echo '!! links LLVM' >&2; exit 1; }
"
# Headers and pkg-config files are for building against it; nothing does.
rm -rf "$SRC/install$PREFIX/include" "$SRC/install$PREFIX/lib/pkgconfig"
rm -rf "$OUT"
mkdir -p "$(dirname "$OUT")"
cp -a "$SRC/install$PREFIX" "$OUT"
echo "  built  ports Mesa $VER -> $OUT ($(du -sh "$OUT" | cut -f1))"
ls "$OUT/lib"
