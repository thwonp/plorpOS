#!/bin/sh
# The Flip's optional fast-boot download (plorpos-wzn5): the image build-lean.sh
# wrote, as miyoo285_fw.img at the zip root (the stock SD update reads it from
# the card root), and docs/install-flip.md as INSTALL-FLIP.md, as in the Flip zip.
#
#   mk/flip-fw/package-fast-boot.sh <lean miyoo285_fw.img> [out zip]
#
# The zip defaults to out/plorpOS-miyoominiflip-fast-boot.zip.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
IMG=${1:?lean miyoo285_fw.img (from build-lean.sh)}
Z=${2:-$ROOT/out/plorpOS-miyoominiflip-fast-boot.zip}
command -v zip >/dev/null || { echo "need zip" >&2; exit 1; }
[ -f "$IMG" ] || { echo "no image at $IMG" >&2; exit 1; }
mkdir -p "$(dirname "$Z")"; Z=$(cd "$(dirname "$Z")" && pwd)/$(basename "$Z")
# Staged next to the zip, not in /tmp: the image is 11 MB and /tmp is RAM.
R=$Z.stage; rm -rf "$R"; mkdir -p "$R"
trap 'rm -rf "$R"' EXIT
ln "$IMG" "$R/miyoo285_fw.img" 2>/dev/null || cp "$IMG" "$R/miyoo285_fw.img"
cp "$ROOT/docs/install-flip.md" "$R/INSTALL-FLIP.md"
rm -f "$Z"
( cd "$R" && zip -q "$Z" miyoo285_fw.img INSTALL-FLIP.md )
echo "fast-boot: $Z ($(md5sum "$IMG" | cut -c1-8) miyoo285_fw.img)"
