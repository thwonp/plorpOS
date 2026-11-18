#!/bin/sh
# aarch64 dropbear for the MagicX Zero 40 (plorpos-auv): oakMOSS ships
# no SSH server. The Flip's pinned source and key-folder patch
# (mk/flip-cores/build-dropbear.sh), built in the Brick toolchain image.
# Output: build/zero40/dropbearmulti (dropbear + dropbearkey).
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
W=$R/build/zero40/dropbear-src
DL=${DL:-$HOME/projects/misc/coding-team/plorpos-miyoo-mini-flip/dist-inputs/src}
REV=28216cd9af822732a1549b78621c2ea9a76a0fe5
SUM=e3dbb92dfe44a439ff6d371eaf0120410e23e0d0aae0c6c365c3489a54b55165
F=$DL/dropbear-$REV.tar.gz
mkdir -p "$DL"
[ -s "$F" ] || curl -sSfL --max-time 300 -o "$F" "https://github.com/mkj/dropbear/archive/$REV.tar.gz"
echo "$SUM  $F" | sha256sum -c --quiet -
rm -rf "$W"; mkdir -p "$W"
tar -C "$W" --strip-components=1 -xzf "$F"
# Dynamic, unlike the Flip's: a static glibc looks users up through the
# device's NSS modules, and against oakMOSS's 2.38 they do not load - every
# login was "nonexistent user" (2026-11-17). Built against the toolchain's
# 2.31, it runs on the newer one. Key login only, keys from
# plorpOS/ssh/authorized_keys (sd/zero40/ssh.sh).
printf '#define DROPBEAR_SVR_PASSWORD_AUTH 0\n' > "$W/localoptions.h"
git -C "$R" apply --directory=build/zero40/dropbear-src mk/flip-cores/patches/dropbear-keydir.patch
docker run --rm -v "$R/build/zero40":/w -w /w/dropbear-src tortos-toolchain sh -c '
set -e
./configure --host=aarch64-linux-gnu CC=aarch64-linux-gnu-gcc CFLAGS="-Os -mcpu=cortex-a53" \
  --disable-zlib --enable-bundled-libtom \
  --disable-lastlog --disable-utmp --disable-utmpx --disable-wtmp --disable-wtmpx \
  --disable-pututline --disable-pututxline --disable-pam > conf.log 2>&1
make -j8 PROGRAMS="dropbear dropbearkey" MULTI=1 > make.log 2>&1
aarch64-linux-gnu-strip --strip-unneeded -o ../dropbearmulti dropbearmulti'
ls -l "$R/build/zero40/dropbearmulti"
