#!/bin/sh
# The Anbernic RG DS's sysroot, for `make PLATFORM=rgds`. The RG DS runs
# ROCKNIX like the GKD, so this is mk/fetch-gkd-sysroot.sh pointed at the RG DS
# with the FFmpeg its newer ROCKNIX ships (9.0: libavformat.so.63 and friends).
# SDL2, SDL2_image and SDL2_ttf have the GKD's ABI, so its pinned headers serve.
#
# Usage: mk/fetch-rgds-sysroot.sh [out-dir]   (default sysroot-rgds/)
#        RGDS=root@<ip> SSH_OPTS="-i <key>" to reach the device
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
export GKD=${RGDS:-root@192.168.0.74}
export FF_VER=9.0
export FF_SHA=1b12b07e0a6c39fc0025104ed1e9b6096fd1a11628daad8ac98a68d8cfd4ebed
export FF_SOS="libavformat.so.63 libavcodec.so.63 libavutil.so.61 libswresample.so.7 libavfilter.so.12"
exec "$ROOT/mk/fetch-gkd-sysroot.sh" "${1:-$ROOT/sysroot-rgds}"
