#!/bin/sh
# The RG Rotate's CM4 boot code (plorpos-3yv.13.2.1), from source: the 31
# words of sp_loader[] in Unisoc's GPL U-Boot (board/spreadtrum/
# ums512_1h10_go/cp_boot.h), little-endian - what the stock U-Boot copies to
# the CM4's IRAM before it starts pm_sys. The kernel's pmsys remoteproc loads
# it as sprd/pm_bootcode.bin. pm_sys itself is the unit's own, taken off its
# eMMC at first boot (plorpos-3yv.13.2.2); this one ships.
#
#   mk/rotate-pm-bootcode.sh [OUT]   (default build/rotate/pm_bootcode.bin)
#   UBOOT (~/projects/git/u-boot-ums512) overrides
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-$ROOT/build/rotate/pm_bootcode.bin}
UBOOT=${UBOOT:-$HOME/projects/git/u-boot-ums512}
SRC=$UBOOT/board/spreadtrum/ums512_1h10_go/cp_boot.h
[ -r "$SRC" ] || { echo "missing $SRC" >&2; exit 1; }
mkdir -p "$(dirname "$OUT")"
python3 - "$SRC" "$OUT" <<'EOF'
import re, struct, sys
src, out = sys.argv[1], sys.argv[2]
body = re.search(r'sp_loader\[\]\s*=\s*\{([^}]*)\}', open(src).read()).group(1)
words = [int(w, 16) for w in re.findall(r'0x[0-9a-fA-F]+', body)]
if len(words) != 31:
    sys.exit(f'sp_loader[]: {len(words)} words, expected 31')
open(out, 'wb').write(struct.pack('<31I', *words))
EOF
md5sum "$OUT"
