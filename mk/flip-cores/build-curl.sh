#!/bin/sh
# Static-TLS curl for the Miyoo Mini Flip (plorpos-f1p.36): the firmware ships
# no curl and BusyBox wget has no TLS, so net.c's net_online() is always false.
# mbedTLS and libcurl are linked statically; only libc/libpthread/libdl stay
# dynamic, which is what the toolchain image (Debian buster, glibc 2.28) shares
# with the device. Run from a work dir holding dl/curl-*.tar.xz and
# dl/mbedtls-*.tar.bz2 (versions and sha256 below); output: ./curl-flip.
set -e
CURL=8.10.1;  CURL_SHA=73a4b0e99596a09fa5924a4fb7e4b995a85fda0d18a2c02ab9cf134bebce04ee
MBED=3.6.2;   MBED_SHA=8b54fb9bcf4d5a7078028e0520acddefb7900b3e66fec7f7175bb5b7d85ccdca
W=${W:-$PWD}
cd "$W"
echo "$CURL_SHA  dl/curl-$CURL.tar.xz"     | sha256sum -c -
echo "$MBED_SHA  dl/mbedtls-$MBED.tar.bz2" | sha256sum -c -
rm -rf src out; mkdir -p src out   # the image has no bzip2/xz: unpack here
tar -C src -xf dl/mbedtls-$MBED.tar.bz2
tar -C src -xf dl/curl-$CURL.tar.xz
timeout 1500 docker run --rm -v "$W":/w:z -w /w localhost/miyoo-toolchain sh -c '
set -e
cd src/mbedtls-'$MBED'
cmake -S . -B b -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=arm \
  -DCMAKE_C_COMPILER=arm-linux-gnueabihf-gcc -DCMAKE_C_FLAGS="-Os -marm -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard" \
  -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF -DUSE_SHARED_MBEDTLS_LIBRARY=OFF \
  -DCMAKE_INSTALL_PREFIX=/w/out/mbed > /w/mbed.log 2>&1
cmake --build b -j4 >> /w/mbed.log 2>&1 && cmake --install b >> /w/mbed.log 2>&1
cd ../curl-'$CURL'
./configure --host=arm-linux-gnueabihf CC=arm-linux-gnueabihf-gcc \
  CFLAGS="-Os -marm -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard" \
  --with-mbedtls=/w/out/mbed --without-zlib --without-brotli --without-zstd \
  --without-libpsl --without-libidn2 --without-nghttp2 --without-libssh2 --without-librtmp \
  --disable-shared --enable-static --disable-ldap --disable-ldaps --disable-rtsp --disable-dict \
  --disable-telnet --disable-tftp --disable-pop3 --disable-imap --disable-smtp --disable-gopher \
  --disable-mqtt --disable-manual --disable-docs --disable-ntlm --disable-unix-sockets \
  --disable-threaded-resolver --disable-ipv6 --disable-websockets --disable-ftp --disable-file \
  --disable-smb --disable-ares --disable-verbose --disable-libcurl-option \
  > /w/curl-conf.log 2>&1
make -j4 > /w/curl-make.log 2>&1
arm-linux-gnueabihf-strip --strip-unneeded -o /w/curl-flip src/curl
ls -l /w/curl-flip'
