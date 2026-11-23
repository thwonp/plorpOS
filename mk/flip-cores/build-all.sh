#!/bin/sh
# Build every Flip core + dropbear from pinned sources (see cores.lock).
# Continues past failures; prints a summary of the RESULT lines at the end.
cd "$(dirname "$0")"
W=../../build/flip-cores
mkdir -p "$W/logs"
: > "$W/logs/all.out"
for c in mednafen_pce_fast gambatte \
         picodrive snes9x2005_plus fceumm gpsp fake08 race mednafen_ngp mednafen_supafaust pcsx dropbear \
         fbneo fbneo:cps12 fbneo:neogeo; do
  s=$(date +%s)
  # name:arg = one script, one argument (FBNeo's family cores)
  ./build-${c%%:*}.sh $(case $c in *:*) echo "${c#*:}";; esac) > "$W/logs/all-$c.out" 2>&1 && st=ok || st=FAILED
  echo "$c $st $(( $(date +%s) - s ))s" >> "$W/logs/all.out"
  grep '^RESULT' "$W/logs/all-$c.out" >> "$W/logs/all.out" || true
  [ $st = ok ] || tail -5 "$W/logs/all-$c.out" | sed 's/^/    /' >> "$W/logs/all.out"
done
echo "== summary"
cat "$W/logs/all.out"
! grep -q ' FAILED' "$W/logs/all.out"
