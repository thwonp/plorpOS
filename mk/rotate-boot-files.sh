#!/bin/sh
# The RG Rotate's fast-boot files (plorpos-3yv.13.2), from a kernel build and
# a firmware directory. Writes OUT only, never a device; the same inputs give
# the same bytes (fixed timestamps), so a release can be checked against them.
#
#   mk/rotate-boot-files.sh FWDIR [OUT]   (default OUT: build/rotate/boot)
#   KBUILD (~/projects/git/build-ums512), MKIMAGE (u-boot-ums512's) override
#
#   Image-plorpos.uimg  gzip uImage (U-Boot a125ca1+: kernel_addr_r 0x9a200000,
#                       64 MiB gunzip limit), unpacks to 0x80080000
#   initrd-fw.cpio.gz   no /init: dev/console and the firmware the built-in
#                       drivers ask for before root is mounted, after which
#                       the kernel mounts root= itself:
#                       lib/firmware/updates/agdsp.bin - the audio DSP, loaded
#                         at 0.16 s
#                       lib/firmware/sprd/pm_{bootcode,sys}.bin - the CM4
#                         (pm_sys), auto-booted at 0.38 s; PUB deep sleep
#                         needs it running (plorpos-3yv.74, seq 135)
#
# FWDIR holds those three under lib/firmware/ as on the device. Where they
# come from is not this script's business: agdsp.bin is what Armbian
# extracted from the unit's eMMC, pm_* the stock firmware's (redistribution:
# plorpos-3yv.13.2). Install: copy both into /boot (p2); extlinux.conf names
# them (kernel /Image-plorpos.uimg, initrd /initrd-fw.cpio.gz).
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
FW=$(cd "${1:?usage: mk/rotate-boot-files.sh FWDIR [OUT]}" && pwd)
OUT=${2:-$ROOT/build/rotate/boot}
KBUILD=${KBUILD:-$HOME/projects/git/build-ums512}
MKIMAGE=${MKIMAGE:-$HOME/projects/git/u-boot-ums512/out/tools/mkimage}
IMG=$KBUILD/arch/arm64/boot/Image
for f in "$IMG" "$KBUILD/usr/gen_init_cpio" "$MKIMAGE" \
	"$FW/lib/firmware/updates/agdsp.bin" "$FW/lib/firmware/sprd/pm_bootcode.bin" \
	"$FW/lib/firmware/sprd/pm_sys.bin"; do
	[ -e "$f" ] || { echo "missing $f" >&2; exit 1; }
done
# The kernel image's own time, for both: rebuilding from the same build is byte-identical.
EPOCH=$(stat -c %Y "$IMG")
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
mkdir -p "$OUT"

gzip -9 -n -c "$IMG" > "$T/Image.gz"
"$MKIMAGE" -A arm64 -O linux -T kernel -C gzip \
	-a 0x80080000 -e 0x80080000 -n "plorpOS $(cat "$KBUILD/include/config/kernel.release")" \
	-d "$T/Image.gz" "$OUT/Image-plorpos.uimg" >/dev/null
# This mkimage stamps the header with now (no SOURCE_DATE_EPOCH): put the
# image's time in (ih_time, bytes 8-11) and redo the header CRC (bytes 4-7).
python3 - "$OUT/Image-plorpos.uimg" "$EPOCH" <<'EOF'
import struct, sys, zlib
p, t = sys.argv[1], int(sys.argv[2])
with open(p, 'r+b') as f:
    h = bytearray(f.read(64))
    h[4:8] = b'\0' * 4; h[8:12] = struct.pack('>I', t)
    h[4:8] = struct.pack('>I', zlib.crc32(h) & 0xffffffff)
    f.seek(0); f.write(h)
EOF

cat > "$T/list" <<EOF
dir /dev 0755 0 0
nod /dev/console 0600 0 0 c 5 1
dir /lib 0755 0 0
dir /lib/firmware 0755 0 0
dir /lib/firmware/updates 0755 0 0
file /lib/firmware/updates/agdsp.bin $FW/lib/firmware/updates/agdsp.bin 0644 0 0
dir /lib/firmware/sprd 0755 0 0
file /lib/firmware/sprd/pm_bootcode.bin $FW/lib/firmware/sprd/pm_bootcode.bin 0644 0 0
file /lib/firmware/sprd/pm_sys.bin $FW/lib/firmware/sprd/pm_sys.bin 0644 0 0
EOF
"$KBUILD/usr/gen_init_cpio" -t "$EPOCH" "$T/list" | gzip -9 -n > "$OUT/initrd-fw.cpio.gz"

( cd "$OUT" && md5sum Image-plorpos.uimg initrd-fw.cpio.gz && ls -l Image-plorpos.uimg initrd-fw.cpio.gz )
