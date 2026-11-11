#!/bin/sh
# Train a core's profile on the Flip (see common.sh, PGO):
#   PGO=gen sh mk/flip-cores/build-<core>.sh      # instrumented build in build/flip-cores/out
#   sh mk/flip-cores/pgo-train.sh <core> FRAMES "Roms/rel/path" ...
# Runs lrbench (unthrottled, headless; the launcher must be stopped) on each
# ROM with the instrumented core (120 s cap per ROM: the Flip has no timeout), then pulls /tmp/pgo/<core> into pgo/<core>/.
# lrbench: plorpos/evidence-seq193/tools/lrbench.c, at /tmp/lrbench on the Flip.
set -e
cd "$(dirname "$0")"
C=$1 N=$2; shift 2
DEV=${MIYOO:-root@192.168.0.73}
SO=../../build/flip-cores/out/${C}_libretro.so
T=/mnt/SDCARD/TortOS/cores-pgo
ssh -n "$DEV" "mkdir -p $T; rm -rf /tmp/pgo/$C; echo 0 > /sys/class/gpio/gpio44/value"   # speaker amp off: runs are muted
ssh "$DEV" "cat > $T/${C}_libretro.so" < "$SO"
for rom in "$@"; do
	r=$(printf %s "$rom" | sed "s/'/'\\\\''/g")
	ssh -n "$DEV" "rm -rf /tmp/lsv; mkdir -p /tmp/lsv; /tmp/lrbench -c $T/${C}_libretro.so -r '/mnt/SDCARD/Roms/$r' -s /tmp/lsv -y /mnt/SDCARD/Bios -n $N > /tmp/lrb.out 2>&1 & P=\$!; i=0; while kill -0 \$P 2>/dev/null; do i=\$((i+1)); [ \$i -gt 120 ] && { kill -9 \$P; echo 'TIMEOUT (no profile from this run)'; }; sleep 1; done; grep -a RESULT /tmp/lrb.out | cut -c1-80; rm -rf /tmp/lsv /tmp/lrb.out"
done
rm -rf "pgo/$C"; mkdir -p "pgo/$C"
ssh -n "$DEV" "cd /tmp/pgo/$C && tar cf - . && rm -rf /tmp/pgo/$C $T/${C}_libretro.so" | tar -C "pgo/$C" -xf -
echo "pgo/$C: $(find "pgo/$C" -name '*.gcda' | wc -l) profiles, $(du -sh "pgo/$C" | cut -f1)"
