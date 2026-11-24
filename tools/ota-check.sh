#!/bin/sh
# sd/tortos/ota.sh on a fake card (plorpos-koo): stage, apply, carry-over,
# rollback, confirm, the GKD's copy_to_sd/ layout, and each way staging fails.
# Small synthetic zips, so it runs in a moment; device runs are koo.5's.
set -u
R=$(cd "$(dirname "$0")/.." && pwd)
OTA="sh $R/sd/tortos/ota.sh"
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
export TMPDIR="$T"
STATE=/tmp/plorpos-ota.state
fails=0
ok() { if eval "$2"; then echo "ok   $1"; else echo "FAIL $1"; fails=$((fails + 1)); fi; }
sum() { sha256sum "$1" | cut -c1-64; }

# A release zip: CARD-ROOT layout (brick), or the GKD's copy_to_sd/ + system.d/.
mkzip() {   # mkzip NAME LAYOUT
	rm -rf "$T/z"; mkdir -p "$T/z"
	b=$T/z; [ "$2" = gkd ] && { b=$T/z/copy_to_sd; mkdir -p "$T/z/system.d"; echo conf > "$T/z/system.d/x.conf"; }
	mkdir -p "$b/plorpOS/cores" "$b/.tmp_update" "$b/Roms/GB/.media" "$b/Saves/GB"
	echo new-elf > "$b/plorpOS/tortos.elf"
	echo new-core > "$b/plorpOS/cores/a.so"
	echo new-updater > "$b/.tmp_update/updater"
	echo readme > "$b/INSTALL.md"
	( cd "$T/z" && zip -qr "$T/$1" . )
}
# A card that has run the old version for a while.
mkcard() {
	rm -rf "$T/card"; C=$T/card
	mkdir -p "$C/plorpOS/cores" "$C/.tmp_update" "$C/Roms/GB" "$C/Saves/GB"
	echo old-elf > "$C/plorpOS/tortos.elf"
	echo old-core > "$C/plorpOS/cores/a.so"
	echo mine > "$C/plorpOS/cores/added.so"
	echo last > "$C/plorpOS/.last"
	echo logo > "$C/plorpOS/bootlogo.stock.bmp"
	echo old-updater > "$C/.tmp_update/updater"
	echo rom > "$C/Roms/GB/game.gb"
	echo save > "$C/Saves/GB/game.sav"
	mkdir -p "$C/.ota"
}
stage() { $OTA stage "$C" "$C/.ota/$1" "$2" > "$T/out" 2>&1; }

# --- stage + apply + rollback
mkcard; mkzip plorpOS-v1.10-brick.zip brick
cp "$T/plorpOS-v1.10-brick.zip" "$C/.ota/"
stage plorpOS-v1.10-brick.zip 0000; rc=$?
ok "a wrong checksum fails"            '[ $rc -ne 0 ] && grep -q "error checksum" $STATE'
ok "  and leaves nothing staged"       '[ ! -e "$C/.ota/new" ] && [ ! -e "$C/.ota/ready" ] && [ ! -e "$C/.ota/plorpOS-v1.10-brick.zip" ]'
cp "$T/plorpOS-v1.10-brick.zip" "$C/.ota/"
stage plorpOS-v1.10-brick.zip "$(sum "$T/plorpOS-v1.10-brick.zip")"; rc=$?
ok "a good zip stages"                 '[ $rc -eq 0 ] && grep -q "^ready 1.10$" $STATE && [ -f "$C/.ota/ready" ]'
ok "  and its download is deleted"     '[ ! -e "$C/.ota/plorpOS-v1.10-brick.zip" ]'
ok "  nothing installed before boot"   'grep -q old-elf "$C/plorpOS/tortos.elf"'
$OTA apply "$C" > "$T/out" 2>&1
ok "apply installs it"                 'grep -q new-elf "$C/plorpOS/tortos.elf" && grep -q new-core "$C/plorpOS/cores/a.so"'
ok "  keeps the old one aside"         'grep -q old-elf "$C/plorpOS.prev/tortos.elf"'
ok "  carries what it does not ship"   '[ -f "$C/plorpOS/.last" ] && [ -f "$C/plorpOS/bootlogo.stock.bmp" ] && [ -f "$C/plorpOS/cores/added.so" ]'
ok "  replaces the boot hook"          'grep -q new-updater "$C/.tmp_update/updater" && [ ! -e "$C/.tmp_update/updater.ota-new" ]'
ok "  leaves games and saves"          'grep -q rom "$C/Roms/GB/game.gb" && grep -q save "$C/Saves/GB/game.sav" && [ -d "$C/Roms/GB/.media" ]'
ok "  clears the staging area"         '[ ! -e "$C/.ota" ]'
ok "  says so"                         'grep -q "installed v1.10 (3 files kept" "$T/out"'
$OTA apply "$C" > "$T/out" 2>&1
ok "a second apply does nothing"       'grep -q new-elf "$C/plorpOS/tortos.elf" && [ ! -s "$T/out" ]'
echo made-by-new > "$C/plorpOS/.new-state"
$OTA rollback "$C" > "$T/out" 2>&1; rc=$?
ok "rollback restores the old one"     '[ $rc -eq 0 ] && grep -q old-elf "$C/plorpOS/tortos.elf" && [ ! -e "$C/plorpOS.prev" ] && [ ! -e "$C/plorpOS.failed" ]'
ok "  with what was carried"           '[ -f "$C/plorpOS/.last" ] && [ -f "$C/plorpOS/bootlogo.stock.bmp" ] && [ -f "$C/plorpOS/cores/added.so" ] && [ -f "$C/plorpOS/.new-state" ]'
ok "  and says why"                    '[ -s "$C/plorpOS/update-failed.log" ]'
$OTA rollback "$C" > /dev/null 2>&1; rc=$?
ok "nothing to roll back fails"        '[ $rc -ne 0 ] && grep -q old-elf "$C/plorpOS/tortos.elf"'

# --- stage + apply + ok
mkcard; cp "$T/plorpOS-v1.10-brick.zip" "$C/.ota/"
stage plorpOS-v1.10-brick.zip "$(sum "$T/plorpOS-v1.10-brick.zip")"
$OTA apply "$C" > /dev/null 2>&1
$OTA ok "$C" > /dev/null 2>&1
ok "ok drops the old version"          '[ ! -e "$C/plorpOS.prev" ] && grep -q new-elf "$C/plorpOS/tortos.elf"'
$OTA rollback "$C" > /dev/null 2>&1; rc=$?
ok "  and then cannot roll back"       '[ $rc -ne 0 ] && grep -q new-elf "$C/plorpOS/tortos.elf"'

# --- the GKD: copy_to_sd/ is the card; system.d/ is not touched
mkcard; mkzip plorpOS-v1.10-gkd.zip gkd; cp "$T/plorpOS-v1.10-gkd.zip" "$C/.ota/"
stage plorpOS-v1.10-gkd.zip "$(sum "$T/plorpOS-v1.10-gkd.zip")"; rc=$?
$OTA apply "$C" > /dev/null 2>&1
ok "gkd: copy_to_sd/ installs"         '[ $rc -eq 0 ] && grep -q new-elf "$C/plorpOS/tortos.elf" && grep -q new-updater "$C/.tmp_update/updater"'
ok "gkd: no system.d/ or copy_to_sd/"  '[ ! -e "$C/system.d" ] && [ ! -e "$C/copy_to_sd" ]'

# --- staging refuses
mkcard; cp "$T/plorpOS-v1.10-brick.zip" "$C/.ota/"
OTA_MARGIN_KB=999999999 $OTA stage "$C" "$C/.ota/plorpOS-v1.10-brick.zip" "$(sum "$T/plorpOS-v1.10-brick.zip")" > /dev/null 2>&1; rc=$?
ok "no room fails before unpacking"    '[ $rc -ne 0 ] && grep -q "error space" $STATE && [ ! -e "$C/.ota/new" ]'
rm -rf "$T/z"; mkdir -p "$T/z/stuff"; echo x > "$T/z/stuff/f"; ( cd "$T/z" && zip -qr "$C/.ota/plorpOS-v9-brick.zip" . )
stage plorpOS-v9-brick.zip "$(sum "$C/.ota/plorpOS-v9-brick.zip")"; rc=$?
ok "a zip without plorpOS/ fails"      '[ $rc -ne 0 ] && grep -q "error not a plorpOS zip" $STATE && [ ! -e "$C/.ota/new" ]'
head -c $(( $(wc -c < "$T/plorpOS-v1.10-brick.zip") / 2 )) "$T/plorpOS-v1.10-brick.zip" > "$C/.ota/plorpOS-v1.10-brick.zip"
stage plorpOS-v1.10-brick.zip "$(sum "$C/.ota/plorpOS-v1.10-brick.zip")"; rc=$?
ok "a cut-short zip fails"             '[ $rc -ne 0 ] && grep -q "^error" $STATE && [ ! -e "$C/.ota/ready" ]'
$OTA apply "$C" > /dev/null 2>&1
ok "  and apply leaves the card alone" 'grep -q old-elf "$C/plorpOS/tortos.elf" && [ ! -e "$C/plorpOS.prev" ]'
rm -f "$STATE"

[ $fails -eq 0 ] && echo "ota: all passed" || { echo "ota: $fails FAILED"; exit 1; }
