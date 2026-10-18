#!/bin/sh
# Regenerate mk/pgo/melondsds.tgz, the profile mk/build-melondsds.sh builds the
# DS core with (plorpos-3yv.51). Run it after changing the core's source, pins
# or patches - a stale profile still builds, it just stops paying.
#
#   mk/build-melondsds.sh --instrument     # the training core
#   tools/ds-pgo-train.sh                  # ~6 min on the device
#   mk/build-melondsds.sh                  # fails on the sha: verify, re-pin
#
# Trains on the RG Rotate over ssh, unthrottled on the two A75s, through
# tools/dsbench.c: Pokemon Pearl's and Metroid Prime Hunters' 3D attract
# scenes, Advance Wars and Pokemon with scripted input (menus, 2D), save
# states and a reload. Dragon Quest V stays out on purpose: it is the held-out
# check, measured in Diatom (encrypted dump; the bench cannot boot it).
# Needs those ROMs in the card's Roms/Nintendo DS.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=${SRC:-/tmp/tortos-melondsds}
HOST=${ROTATE_HOST:-root@192.168.0.69}
KEY=${ROTATE_KEY:-$HOME/.ssh/rgrotate}
CORE=$SRC/out/melondsds_libretro.pgo.so
D=/root/dspgo
ssh_() { ssh -i "$KEY" -o IdentitiesOnly=yes "$HOST" "$@"; }

[ -f "$CORE" ] || { echo "no $CORE: run mk/build-melondsds.sh --instrument" >&2; exit 1; }
docker run --rm -v "$ROOT/tools:/w:ro" -v "$SRC/out:/o" -w /w tortos-toolchain \
	aarch64-linux-gnu-gcc -O2 -Wall dsbench.c -ldl -o /o/dsbench
ssh_ "rm -rf $D && mkdir -p $D"
ssh_ "cat > $D/core.so" < "$CORE"
ssh_ "cat > $D/dsbench && chmod +x $D/dsbench" < "$SRC/out/dsbench"

ssh_ "cd $D && export GCOV_PREFIX=$D/gcda BENCH_EXIT=1
R='/mnt/SDCARD/Roms/Nintendo DS'
O='melonds_jit_enable=enabled melonds_render_mode=software melonds_threaded_renderer=enabled'
run() { taskset -c 6,7 ./dsbench ./core.so \"\$R/\$1.nds\" \$2 \$3 /tmp/pgo.ppm \$O 2>/dev/null | grep -E '^(ms|roundtrip)' || true; }
BENCH_NOINPUT=1 run 'Pokemon - Pearl Version' 1350 1350
BENCH_NOINPUT=1 run 'Metroid Prime - Hunters' 1350 1350
run 'Advance Wars - Dual Strike' 600 1800
run 'Pokemon - Pearl Version' 600 1800
BENCH_RT=1 BENCH_SER=60 BENCH_RELOAD=1 BENCH_NOINPUT=1 run 'Pokemon - Pearl Version' 600 60
tar czf $D/melondsds.tgz -C $D/gcda/out ."
mkdir -p "$ROOT/mk/pgo"
ssh_ "cat $D/melondsds.tgz" > "$ROOT/mk/pgo/melondsds.tgz"
ssh_ "rm -rf $D"
echo "  profile: mk/pgo/melondsds.tgz ($(tar tzf "$ROOT/mk/pgo/melondsds.tgz" | grep -c gcda) files)"
