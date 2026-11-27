#!/bin/sh
# Faster-boot update image for the Miyoo Mini Flip (plorpos-f1p.38, .97), made
# from the user's own stock firmware:
#  - /customer: lean-customer.patch (main: no 1 s settle, no splash draws, SD
#    card polled every 0.1 s, settings read with sed instead of jsonval;
#    demo.sh: modules plorpOS never uses are not loaded)
#  - miservice: mi_sys.ko waits 0.1 s, not 2 s, for its first client after
#    Miyoo's LCD helper (one byte: movs r5,#200 -> #10; found by tenlevels)
#  - U-Boot env: bootcmd without `sleepms 1000;` (mkfw.py)
#
#   mk/flip-fw/build-lean.sh <stock miyoo285_fw.img> <out miyoo285_fw.img> [build id]
#   mk/flip-fw/package-fast-boot.sh <out miyoo285_fw.img>   the release download
#
# Needs unsquashfs, mksquashfs (squashfs-tools), git and python3. U-Boot, kernel
# and rootfs are not written by the result.
set -eu
STOCK=${1:?stock miyoo285_fw.img}
OUT=${2:?output image}
BUILD=${3:-202511240001}
HERE=$(cd "$(dirname "$0")" && pwd)
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
for P in customer miservice; do
	python3 -I "$HERE/mkfw.py" --stock "$STOCK" --extract $P "$W/$P.sqfs"
	( umask 000; unsquashfs -no-xattrs -d "$W/$P" "$W/$P.sqfs" > /dev/null )
done

C=$W/customer
MODE=$(stat -c %a "$C/main"); touch -r "$C" "$W/dirtime"
( cd "$C" && git apply -p1 "$HERE/lean-customer.patch" ) || { echo "lean-customer.patch did not apply (a different stock build, or no git)" >&2; exit 1; }
chmod "$MODE" "$C/main"; touch -r "$W/dirtime" "$C"   # git apply resets both

K=$W/miservice/modules/4.9.84/mi_sys.ko
touch -r "$K" "$W/kotime"
python3 -I - "$K" <<'EOF' || { echo "mi_sys.ko: the wait's bytes are not there exactly once (a different stock build?)" >&2; exit 1; }
import sys
p = sys.argv[1]; d = open(p, "rb").read()
old, new = bytes.fromhex("c825012201a9"), bytes.fromhex("0a25012201a9")
if d.count(old) != 1: sys.exit(1)
open(p, "wb").write(d.replace(old, new))
EOF
touch -r "$W/kotime" "$K"

for P in customer miservice; do
	mksquashfs "$W/$P" "$W/$P.lean" -comp xz -b 131072 -all-root -noappend -no-xattrs -quiet > /dev/null
done
python3 -I "$HERE/mkfw.py" --stock "$STOCK" --customer "$W/customer.lean" --miservice "$W/miservice.lean" --build "$BUILD" --out "$OUT"
