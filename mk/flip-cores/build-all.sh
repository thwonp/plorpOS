#!/bin/sh
# Build every Flip core + dropbear from pinned sources (see cores.lock).
# Continues past failures; prints a summary of the RESULT lines at the end.
cd "$(dirname "$0")"
W=../../build/flip-cores
mkdir -p "$W/logs"
: > "$W/logs/all.out"
for c in fbalpha2012 fbalpha2012_neogeo mednafen_pce_fast gambatte \
         picodrive snes9x2005_plus fceumm gpsp fake08 race mednafen_ngp mednafen_supafaust mame2000 pcsx dropbear; do
  s=$(date +%s)
  ./build-$c.sh > "$W/logs/all-$c.out" 2>&1 && st=ok || st=FAILED
  echo "$c $st $(( $(date +%s) - s ))s" >> "$W/logs/all.out"
  grep '^RESULT' "$W/logs/all-$c.out" >> "$W/logs/all.out" || true
  [ $st = ok ] || tail -5 "$W/logs/all-$c.out" | sed 's/^/    /' >> "$W/logs/all.out"
done
echo "== summary"
cat "$W/logs/all.out"
! grep -q ' FAILED' "$W/logs/all.out"
