#!/bin/sh
# Updating over Wi-Fi (plorpos-koo): the card side. The launcher finds the
# release and downloads its zip; this unpacks it, and the next boot swaps it in.
#
#   ota.sh stage CARD ZIP SHA256   check the zip, its room on the card, unpack it
#                                  into CARD/.ota/new; the zip is deleted after
#   ota.sh apply CARD              at boot, before anything in plorpOS/ starts:
#                                  plorpOS/ -> plorpOS.prev/, the new one in
#   ota.sh ok CARD                 the new launcher started: drop plorpOS.prev/
#   ota.sh rollback CARD           the new launcher will not start: put the old
#                                  one back, leave plorpOS/update-failed.log
#   ota.sh cancel CARD             forget a staged update
#
# The swap is renames on the one card, so it costs milliseconds, and a crash in
# the middle of it leaves both folders whole. Files in plorpOS/ the release
# does not ship (.last, the saved stock boot logo, a core someone added) are
# moved into the new folder, the rule folder-move.sh follows. The zip's other
# top-level items (.tmp_update/, System/, trimui/, Roms/'s folders) are merged
# in file by file, each written beside its old self and renamed over it: one of
# them can be the boot script running this. The GKD's zip keeps the card under
# copy_to_sd/; its system.d/ is ROCKNIX's storage and stays as it is.
#
# Progress for the launcher: one line in /tmp/plorpos-ota.state -
#   verify | unpack DONE TOTAL | ready VERSION | error REASON
# One summary line per command to stdout; the launch scripts add it to the log.
CMD=$1
CARD=${2%/}
[ -n "$CARD" ] && [ -d "$CARD" ] || { echo "ota: usage: ota.sh stage|apply|ok|rollback|cancel CARD" >&2; exit 2; }
W=$CARD/.ota
NEW=$CARD/plorpOS
PREV=$CARD/plorpOS.prev
STATE=/tmp/plorpos-ota.state
MARGIN_KB=${OTA_MARGIN_KB:-32768}   # room left over after unpacking

state() { echo "$*" > "$STATE.tmp" && mv -f "$STATE.tmp" "$STATE"; }
fail() { state "error $1"; rm -rf "$W/new" "$ZIP"; echo "ota: stage failed: $1"; exit 1; }
free_kb() { df -kP "$CARD" | awk 'NR == 2 { print $4 }'; }

# Every file under $1 that $2 does not have, moved to the same path in $2.
carry() {
	( cd "$1" && find . ! -type d ) | while IFS= read -r f; do
		f=${f#./}
		[ -e "$2/$f" ] || [ -L "$2/$f" ] && continue
		mkdir -p "$2/$(dirname "$f")" && mv "$1/$f" "$2/$f" && echo x
	done | wc -l
}

case $CMD in
stage)
	ZIP=$3 SUM=$4
	exec 9> /tmp/plorpos-ota.lock
	flock -n 9 || { echo "ota: already staging"; exit 1; }
	[ -f "$ZIP" ] || fail "no download"
	UNZIP=$(command -v unzip) || UNZIP=$NEW/unzip
	[ -x "$UNZIP" ] || fail "no unzip"
	state "verify"
	[ "$(sha256sum "$ZIP" | cut -c1-64)" = "$SUM" ] || fail "checksum"
	# Its unpacked size, from its own listing: the last line is "BYTES N files".
	set -- $("$UNZIP" -l "$ZIP" | tail -1)
	BYTES=$1 TOTAL=$2
	case $BYTES$TOTAL in *[!0-9]*|'') fail "not a zip" ;; esac
	[ $(( $(free_kb) - BYTES / 1024 )) -ge "$MARGIN_KB" ] || fail "space"
	rm -rf "$W/new"
	state "unpack 0 $TOTAL"
	mkdir -p "$W/new"
	# Without -q both unzips print a line per entry: that is the progress. Its
	# exit status comes down the same pipe, last.
	{ "$UNZIP" -o "$ZIP" -d "$W/new" 2>&1; echo "ota-rc $?"; } | {
		n=0 rc=1
		while IFS= read -r l; do
			case $l in "ota-rc "*) rc=${l#ota-rc }; continue ;; esac
			n=$((n + 1))
			[ $((n % 8)) = 0 ] && state "unpack $n $TOTAL"
		done
		exit "$rc"
	} || fail "unpack"
	rm -f "$ZIP"
	ROOT=new
	[ -d "$W/new/copy_to_sd" ] && ROOT=new/copy_to_sd
	[ -f "$W/$ROOT/plorpOS/tortos.elf" ] || fail "not a plorpOS zip"
	VER=$(basename "$ZIP" .zip | sed -n 's/^plorpOS-v\([^-]*\)-.*/\1/p')
	printf '%s\n%s\n' "$ROOT" "$VER" > "$W/ready.tmp" && mv -f "$W/ready.tmp" "$W/ready" && sync
	state "ready $VER"
	echo "ota: staged v$VER ($((BYTES / 1048576)) MB, $TOTAL entries)"
	;;
apply)
	[ -f "$W/ready" ] || exit 0
	ROOT=$(sed -n 1p "$W/ready")
	VER=$(sed -n 2p "$W/ready")
	S=$W/$ROOT
	[ -f "$S/plorpOS/tortos.elf" ] || { echo "ota: staged update incomplete, dropped"; rm -rf "$W"; exit 0; }
	# Left from an update that started and was never confirmed, or a rollback
	# cut short: the folder in use now is the one that boots.
	rm -rf "$PREV" "$CARD/plorpOS.failed"
	mv "$NEW" "$PREV" || { echo "ota: cannot move plorpOS aside"; exit 1; }
	if ! mv "$S/plorpOS" "$NEW"; then
		mv "$PREV" "$NEW"
		echo "ota: cannot move the update in; kept the old one"
		exit 1
	fi
	n=$(carry "$PREV" "$NEW")
	( cd "$S" && find . ! -path ./plorpOS ! -path './plorpOS/*' ) | while IFS= read -r f; do
		f=${f#./}
		[ "$f" = . ] && continue
		if [ -d "$S/$f" ]; then
			mkdir -p "$CARD/$f"
		else
			cp "$S/$f" "$CARD/$f.ota-new" && mv -f "$CARD/$f.ota-new" "$CARD/$f"
		fi
	done
	rm -rf "$W"
	sync
	echo "ota: installed v$VER ($n files kept from the old plorpOS/)"
	;;
ok)
	[ -d "$PREV" ] || exit 0
	rm -rf "$PREV"
	echo "ota: update confirmed, old version removed"
	;;
rollback)
	[ -d "$PREV" ] || exit 1
	mv "$NEW" "$CARD/plorpOS.failed" || exit 1
	mv "$PREV" "$NEW"
	n=$(carry "$CARD/plorpOS.failed" "$NEW")
	rm -rf "$CARD/plorpOS.failed"
	: > "$NEW/.ota-rolledback"
	echo "$(date) the update would not start; plorpOS went back to the version before it ($n files kept)" \
		>> "$NEW/update-failed.log"
	sync
	echo "ota: rolled back"
	;;
cancel)
	rm -rf "$W"
	rm -f "$STATE"
	;;
*)
	echo "ota: unknown command $CMD" >&2
	exit 2
	;;
esac
