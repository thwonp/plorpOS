#!/bin/sh
# dropbear sshd for the Miyoo Mini Flip from mkj/dropbear tag DROPBEAR_2026.94
# (28216cd). Static (--enable-static), bundled libtomcrypt/libtommath, no zlib,
# no utmp/wtmp/lastlog logging. Multi-call binary: out/bin/dropbearmulti with
# dropbear + dropbearkey (run as `dropbearmulti dropbear ...` or via symlinks).
# ed25519 host keys are on by default (DROPBEAR_ED25519 1); -D <dir> sets the
# authorized_keys directory.
# Static glibc note: getpwnam() still dlopens the device's libnss_files.so.2 at
# run time; the device and the toolchain are both glibc 2.28.
XFLAGS=
. "$(dirname "$0")/common.sh"
fetch dropbear mkj/dropbear 28216cd9af822732a1549b78621c2ea9a76a0fe5 e3dbb92dfe44a439ff6d371eaf0120410e23e0d0aae0c6c365c3489a54b55165
(cd "$R" && git apply --directory=build/flip-cores/src/dropbear mk/flip-cores/patches/dropbear-keydir.patch)
grep -q "plorpOS (Miyoo Mini Flip)" "$W/src/dropbear/src/svr-authpubkey.c" || { echo "RESULT dropbear FAILED: patch"; exit 1; }
mkdir -p "$W/out/bin"
dk src/dropbear '
set -e
./configure --host=arm-linux-gnueabihf CC=arm-linux-gnueabihf-gcc \
  CFLAGS="-Os '"$ARCH"'" --enable-static --disable-zlib --enable-bundled-libtom \
  --disable-lastlog --disable-utmp --disable-utmpx --disable-wtmp --disable-wtmpx \
  --disable-pututline --disable-pututxline --disable-pam > /w/logs/dropbear-conf.log 2>&1
make -j4 PROGRAMS="dropbear dropbearkey" MULTI=1 > /w/logs/dropbear.log 2>&1
arm-linux-gnueabihf-strip --strip-unneeded -o /w/out/bin/dropbearmulti dropbearmulti
o=/w/out/bin/dropbearmulti
echo "RESULT dropbearmulti static=$(arm-linux-gnueabihf-readelf -l $o | grep -c INTERP | sed "s/^0$/yes/;s/^1$/no/") size=$(stat -c %s $o) md5=$(md5sum $o | cut -d" " -f1)"
grep -i "warning.*statically linked" /w/logs/dropbear.log | sed "s/.*warning: //" | sort -u | head -5' \
  || { tail -20 "$W/logs/dropbear.log" "$W/logs/dropbear-conf.log"; echo "RESULT dropbear FAILED"; exit 1; }
