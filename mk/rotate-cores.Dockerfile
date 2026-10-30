# Cross toolchain for the RG Rotate's own-built cores (mk/build-melondsds.sh).
# The Rotate runs Armbian trixie, so trixie's cross gcc links against exactly
# the glibc and libstdc++ the card has. The launcher's toolchain image is
# bullseye's g++-10 and cmake 3.18: melonDS DS needs C++20 and cmake 3.19.
FROM docker.io/library/debian:trixie-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
    g++-aarch64-linux-gnu gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu \
    cmake ninja-build git python3 python3-venv pkg-config ca-certificates file \
 && rm -rf /var/lib/apt/lists/*
