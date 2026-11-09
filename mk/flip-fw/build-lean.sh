#!/bin/sh
# Faster-boot update image for the Miyoo Mini Flip (plorpos-f1p.38), made from
# the user's own stock firmware: unpack its /customer, apply lean-main.patch (the
# stock /customer/main spends ~4 s on a 1 s sleep and two splash draws before the
# SD card's updater can start), repack, wrap as a customer-only update image.
#
#   mk/flip-fw/build-lean.sh <stock miyoo285_fw.img> <out miyoo285_fw.img> [build id]
#
# Needs unsquashfs, mksquashfs (squashfs-tools), git and python3. Only /customer is
# written by the result; U-Boot, kernel and rootfs are not touched.
set -eu
STOCK=${1:?stock miyoo285_fw.img}
OUT=${2:?output image}
BUILD=${3:-202511090002}
HERE=$(cd "$(dirname "$0")" && pwd)
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
python3 -I "$HERE/mkfw.py" --stock "$STOCK" --extract-customer "$W/customer.sqfs"
( umask 000; unsquashfs -no-xattrs -d "$W/root" "$W/customer.sqfs" > /dev/null )
MODE=$(stat -c %a "$W/root/main"); touch -r "$W/root" "$W/dirtime"
( cd "$W/root" && git apply -p1 "$HERE/lean-main.patch" ) || { echo "lean-main.patch did not apply (a different stock build, or no git)" >&2; exit 1; }
chmod "$MODE" "$W/root/main"; touch -r "$W/dirtime" "$W/root"   # git apply resets both
mksquashfs "$W/root" "$W/lean.sqfs" -comp xz -b 131072 -all-root -noappend -no-xattrs -quiet > /dev/null
python3 -I "$HERE/mkfw.py" --customer "$W/lean.sqfs" --build "$BUILD" --out "$OUT"
