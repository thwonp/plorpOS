#!/bin/sh
# Check a Miyoo Mini Flip release zip stands on its own (plorpos-f1p.62.9):
#   flip-verify.sh ZIP CORES_LOCK DRIVER_MD5 [OLD_PREBUILT_MD5S]
# Unzips into a fresh folder and fails on: any "allium" or "/mnt/SDCARD/RetroArch"
# in any file (text or binary strings), a bad MD5SUMS line, a core whose md5 is
# not its cores.lock entry or IS one of the old prebuilts' (OLD_PREBUILT_MD5S, an
# md5sum listing), a hook that hands over to anything outside the zip, or a
# Wi-Fi driver that is not the pinned one.
set -u
Z=$1 LOCK=$2 DRV=$3 OLD=${4:-$HOME/projects/misc/coding-team/plorpos-miyoo-mini-flip/dist-inputs/backup-2026-11/cores.md5}
[ -f "$Z" ] || { echo "no zip $Z" >&2; exit 1; }
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
unzip -q "$Z" -d "$W" || { echo "FAIL unzip"; exit 1; }
F=0
fail() { echo "FAIL $*"; F=1; }

cd "$W"
md5sum -c --quiet MD5SUMS || fail "MD5SUMS"
# Every file, text or not, by its printable strings (case-insensitive).
find . -type f | while read -r f; do
	strings -n 4 "$f" | grep -qiE '(^|[^g])allium|/mnt/SDCARD/RetroArch' && echo "$f"
done > "$W.hits"
[ -s "$W.hits" ] && fail "Allium/RetroArch strings in: $(tr '\n' ' ' < "$W.hits")"
rm -f "$W.hits"

for so in TortOS/cores/*_libretro.so; do
	c=$(basename "$so" _libretro.so); m=$(md5sum < "$so" | cut -c1-32)
	want=$(awk -F'\t' -v c="$c" '$1==c{print $7}' "$LOCK")
	[ "$m" = "$want" ] || fail "core $c md5 $m, cores.lock says ${want:-nothing}"
	[ -f "$OLD" ] && grep -q "^$m " "$OLD" && fail "core $c is an old prebuilt ($m)"
done
[ -n "$(ls TortOS/cores 2>/dev/null)" ] || fail "no cores"

[ "$(md5sum < TortOS/8188fu.ko | cut -c1-32)" = "$DRV" ] || fail "8188fu.ko is not the pinned driver"
grep -qE '^[^#]*exec ' .tmp_update/updater && fail "the hook execs something"
grep -E '^[^#]*(/mnt/SDCARD|\$ROOT)/[^ "]*' -o .tmp_update/updater | sort -u |
	grep -vE 'TortOS/(launch\.sh|\.boot-idle|\.fallback-test)$|\.tmp_update(\.off)?$' > "$W.ref"
[ -s "$W.ref" ] && fail "the hook refers outside the zip: $(tr '\n' ' ' < "$W.ref")"
rm -f "$W.ref"
for b in TortOS/bin/dropbearmulti TortOS/ssh.sh TortOS/launch.sh; do
	[ -f "$b" ] || fail "missing $b"
done
grep -qiE '(^|[^g])allium' INSTALL-FLIP.md && fail "install page mentions Allium"

echo "zip $(basename "$Z"): $(du -h "$Z" | cut -f1), $(find . -type f | wc -l) files, $(ls TortOS/cores | wc -l) cores"
[ $F = 0 ] && echo "verify: PASS" || echo "verify: FAIL"
exit $F
