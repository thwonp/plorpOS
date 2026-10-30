# Cross toolchain for the RG Rotate's ports Mesa (mk/build-ports-mesa.sh):
# trixie's cross gcc plus the arm64 headers Mesa builds against, so it links
# against exactly the glibc, libdrm, Wayland and X11 libraries the card has.
FROM docker.io/library/debian:trixie-slim
RUN dpkg --add-architecture arm64 && apt-get update && apt-get install -y --no-install-recommends \
    gcc-aarch64-linux-gnu g++-aarch64-linux-gnu binutils-aarch64-linux-gnu pkgconf \
    meson ninja-build python3-mako python3-yaml python3-packaging bison flex \
    wayland-protocols libwayland-bin xz-utils file \
    libdrm-dev:arm64 libexpat1-dev:arm64 zlib1g-dev:arm64 libzstd-dev:arm64 \
    libwayland-dev:arm64 libwayland-egl-backend-dev:arm64 libx11-dev:arm64 libxext-dev:arm64 libxfixes-dev:arm64 \
    libx11-xcb-dev:arm64 libxcb1-dev:arm64 libxcb-glx0-dev:arm64 libxcb-shm0-dev:arm64 \
    libxcb-dri2-0-dev:arm64 libxcb-dri3-dev:arm64 libxcb-present-dev:arm64 \
    libxcb-sync-dev:arm64 libxcb-xfixes0-dev:arm64 libxcb-randr0-dev:arm64 \
    libxshmfence-dev:arm64 libxxf86vm-dev:arm64 libxrandr-dev:arm64 \
 && rm -rf /var/lib/apt/lists/*
