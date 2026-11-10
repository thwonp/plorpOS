# Shared helpers for the build-<core>.sh scripts (sourced, not run).
# Sources: GitHub archive tarballs of pinned commits, cached once in $DL and
# checked against cores.lock-style sha256s; unpacked under build/flip-cores/src
# (gitignored). Docker (rootless podman) mounts only build/flip-cores as /w.
set -e
R=$(cd "$(dirname "$0")/../.." && pwd)
W=$R/build/flip-cores
DL=${DL:-$HOME/projects/misc/coding-team/plorpos-miyoo-mini-flip/dist-inputs/src}
IMG=localhost/miyoo-toolchain
ARCH="-marm -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard"
OFAST="-Ofast -mtune=cortex-a7"
XC="CC=arm-linux-gnueabihf-gcc CXX=arm-linux-gnueabihf-g++"
XFLAGS=${XFLAGS-"$ARCH $OFAST"}
mkdir -p "$W/src" "$W/out" "$W/bin" "$W/logs" "$DL"
# CC wrappers append $XFLAGS last, so they win over each Makefile's own -O/-mcpu.
# Named like the real tools (picodrive's mkoffsets.sh derives objdump from $CC).
for t in gcc g++; do
  f=$W/bin/arm-linux-gnueabihf-$t
  [ -x "$f" ] || printf '#!/bin/sh\nexec /usr/bin/arm-linux-gnueabihf-%s "$@" $XFLAGS\n' $t > "$f"
  chmod +x "$f"
done
for t in objdump readelf; do ln -sfn /usr/bin/arm-linux-gnueabihf-$t "$W/bin/arm-linux-gnueabihf-$t"; done
XCW="CC=/w/bin/arm-linux-gnueabihf-gcc CXX=/w/bin/arm-linux-gnueabihf-g++"

# fetch <name> <org/repo> <sha40> <tarball-sha256> [dest]
# Unpacks into src/<name> (wiped first), or for submodules into src/<dest>.
fetch() {
  f=$DL/$1-$3.tar.gz
  [ -s "$f" ] || curl -sSfL --max-time 300 -o "$f" "https://github.com/$2/archive/$3.tar.gz"
  echo "$4  $f" | sha256sum -c --quiet -
  d=$W/src/${5:-$1}
  [ -n "$5" ] || rm -rf "$d"
  mkdir -p "$d"
  tar -C "$d" --strip-components=1 -xzf "$f"
}

# dk <workdir-under-/w> <script>: run in the toolchain image, 25 min cap.
dk() { timeout 1500 docker run --rm -e XFLAGS="$XFLAGS" -v "$W":/w:z -w "/w/$1" "$IMG" sh -c "$2"; }

# mk <name> <srcdir-under-src> <make args...>: clean + build, log to logs/<name>.log
mk() {
  n=$1 d=$2; shift 2
  echo "build $n: XFLAGS=$XFLAGS make $*"
  dk "src/$d" "make $* clean >/dev/null 2>&1; make -j4 $*" > "$W/logs/$n.log" 2>&1 \
    || { tail -20 "$W/logs/$n.log"; echo "RESULT $n FAILED"; exit 1; }
}

# check_so <built .so path under /w> <out name>: strip to out/, then report
# undefined unversioned symbols (must be 0), max GLIBC version, size, md5.
check_so() {
  dk . '
set -e
o=out/'"$2"'
arm-linux-gnueabihf-strip --strip-unneeded -o $o '"$1"'
# binutils 2.31 nm -D prints no versions: use readelf (UND, non-weak, no @VER)
und() { arm-linux-gnueabihf-readelf --dyn-syms -W $o | awk '"'"'$7=="UND" && $5!="WEAK" && $8!="" && $8!~/@/ {print $8}'"'"'; }
u=$(und | wc -l)
g=$(arm-linux-gnueabihf-objdump -T $o | grep -o "GLIBC_[0-9.]*" | sort -uV | tail -1)
echo "RESULT '"$2"' textrel=$(arm-linux-gnueabihf-readelf -d $o | grep -c TEXTREL || true) undefined-unversioned=$u glibc-max=$g size=$(stat -c %s $o) md5=$(md5sum $o | cut -d" " -f1)"
[ "$u" = 0 ] || { und; exit 1; }
case "$g" in GLIBC_2.2[0-8]|GLIBC_2.[0-9]|GLIBC_2.1[0-9]|GLIBC_2.[0-9].*|GLIBC_2.1[0-9].*|GLIBC_2.2[0-8].*|"") ;; *) echo "glibc too new: $g"; exit 1;; esac'
}
