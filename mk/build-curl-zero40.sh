#!/bin/sh
# aarch64 static-TLS curl for the MagicX Zero 40 (plorpos-auv.19): oakMOSS
# ships no curl, so net.c's net_online() was false and every network feature
# (Plex, scraping, RetroAchievements) said it needed Wi-Fi. The Flip's pinned
# curl and Mbed TLS (mk/flip-cores/build-curl.sh), built in the Brick
# toolchain image; only libc stays dynamic (the toolchain's 2.31 runs on
# oakMOSS's 2.38). net.c finds it beside cacert.pem: plorpOS/curl.
# The GKD on BasedOS ships the same file (BasedOS has no curl either).
# Output: build/zero40/curl.
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
W=$R/build/zero40/curl-src
DL=${DL:-$HOME/projects/misc/coding-team/plorpos-miyoo-mini-flip/curl-build/dl}
CURL=8.10.1;  CURL_SHA=73a4b0e99596a09fa5924a4fb7e4b995a85fda0d18a2c02ab9cf134bebce04ee
MBED=3.6.2;   MBED_SHA=8b54fb9bcf4d5a7078028e0520acddefb7900b3e66fec7f7175bb5b7d85ccdca
mkdir -p "$DL"
[ -s "$DL/curl-$CURL.tar.xz" ] || curl -sSfL --max-time 300 -o "$DL/curl-$CURL.tar.xz" \
	"https://curl.se/download/curl-$CURL.tar.xz"
[ -s "$DL/mbedtls-$MBED.tar.bz2" ] || curl -sSfL --max-time 300 -o "$DL/mbedtls-$MBED.tar.bz2" \
	"https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-$MBED/mbedtls-$MBED.tar.bz2"
echo "$CURL_SHA  $DL/curl-$CURL.tar.xz"     | sha256sum -c --quiet -
echo "$MBED_SHA  $DL/mbedtls-$MBED.tar.bz2" | sha256sum -c --quiet -
rm -rf "$W"; mkdir -p "$W"         # the image has no bzip2/xz: unpack here
tar -C "$W" -xf "$DL/mbedtls-$MBED.tar.bz2"
tar -C "$W" -xf "$DL/curl-$CURL.tar.xz"
timeout 1500 docker run --rm -v "$R/build/zero40":/w:z -w /w/curl-src tortos-toolchain sh -c '
set -e
cd mbedtls-'$MBED'
cmake -S . -B b -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
  -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc -DCMAKE_C_FLAGS="-Os -mcpu=cortex-a53" \
  -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF -DUSE_SHARED_MBEDTLS_LIBRARY=OFF \
  -DCMAKE_INSTALL_PREFIX=/w/curl-src/mbed > ../mbed.log 2>&1
cmake --build b -j8 >> ../mbed.log 2>&1 && cmake --install b >> ../mbed.log 2>&1
cd ../curl-'$CURL'
./configure --host=aarch64-linux-gnu CC=aarch64-linux-gnu-gcc CFLAGS="-Os -mcpu=cortex-a53" \
  --with-mbedtls=/w/curl-src/mbed --without-zlib --without-brotli --without-zstd \
  --without-libpsl --without-libidn2 --without-nghttp2 --without-libssh2 --without-librtmp \
  --disable-shared --enable-static --disable-ldap --disable-ldaps --disable-rtsp --disable-dict \
  --disable-telnet --disable-tftp --disable-pop3 --disable-imap --disable-smtp --disable-gopher \
  --disable-mqtt --disable-manual --disable-docs --disable-ntlm --disable-unix-sockets \
  --disable-threaded-resolver --disable-ipv6 --disable-websockets --disable-ftp --disable-file \
  --disable-smb --disable-ares --disable-verbose --disable-libcurl-option \
  > ../curl-conf.log 2>&1
make -j8 > ../curl-make.log 2>&1
aarch64-linux-gnu-strip --strip-unneeded -o /w/curl src/curl'
ls -l "$R/build/zero40/curl"
