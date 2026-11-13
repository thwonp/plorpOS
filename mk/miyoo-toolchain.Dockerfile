# The Miyoo Mini Flip's toolchain (plorpos-f1p): Debian buster's armhf
# cross-compiler, because buster's glibc is 2.28 - exactly the Flip's
# (/lib/libc-2.28.so, Miyoo's stock firmware). tortos-toolchain is bullseye
# (2.31): a binary linked there may need symbols the Flip does not have.
# Buster is archived, so it comes from archive.debian.org; cmake from
# buster-backports (3.18), since SDL2_image/SDL2_ttf need newer than 3.13.
#
#   docker build -t miyoo-toolchain -f mk/miyoo-toolchain.Dockerfile mk
FROM docker.io/library/debian:buster-slim
RUN printf '%s\n' \
        "deb http://archive.debian.org/debian buster main" \
        "deb http://archive.debian.org/debian buster-updates main" \
        "deb http://archive.debian.org/debian-security buster/updates main" \
        "deb http://archive.debian.org/debian buster-backports main" \
        > /etc/apt/sources.list
RUN apt-get -o Acquire::Check-Valid-Until=false update && apt-get install -y --no-install-recommends \
        gcc-arm-linux-gnueabihf \
        g++-arm-linux-gnueabihf \
        libc6-dev-armhf-cross \
        binutils-arm-linux-gnueabihf \
        make pkg-config ca-certificates \
    && apt-get -o Acquire::Check-Valid-Until=false install -y --no-install-recommends -t buster-backports cmake \
    && rm -rf /var/lib/apt/lists/*
ENV CC=arm-linux-gnueabihf-gcc
WORKDIR /work
