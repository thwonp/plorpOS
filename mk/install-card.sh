#!/bin/sh
# Copy the assembled payload onto a mounted FAT32 card.
#
# ONLY ever adds TortOS's own files. It never deletes anything it did not put
# there, and it refuses outright to touch a DO_NOT_TOUCH tree -- on the
# development card that folder holds another firmware install.
set -e
CARD=$1
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$ROOT/out/sd

[ -n "$CARD" ] || { echo "usage: mk/install-card.sh /Volumes/YOURCARD"; exit 1; }
[ -d "$CARD" ]  || { echo "not a directory: $CARD"; exit 1; }
[ -d "$OUT" ]   || { echo "run mk/payload.sh first"; exit 1; }

echo "installing to $CARD"
# COPYFILE_DISABLE stops macOS writing an AppleDouble "._name" sidecar beside
# every file on a FAT volume. They are junk, and they show up in a ROM folder
# as games.
export COPYFILE_DISABLE=1
for item in plorpOS .tmp_update trimui .system Roms Bios Saves; do
	[ -e "$OUT/$item" ] || continue
	# copy the tree in without clearing anything else at the destination
	( cd "$OUT" && tar -cf - "$item" ) | ( cd "$CARD" && tar -xf - )
	# sweep any sidecars from a previous install -- ONLY under our own trees
	find "$CARD/$item" -name '._*' -delete 2> /dev/null || true
	echo "  + $item"
done

# Prove the hands-off tree really was not written to. Do NOT compare an
# "ls -laR" digest: that includes the ".." entry, which reports the CARD ROOT's
# mtime, and the root legitimately changes when the payload lands. Check the
# contents' own timestamps instead.
if [ -d "$CARD/DO_NOT_TOUCH" ]; then
	n=$(find "$CARD/DO_NOT_TOUCH" -newermt '-10 minutes' 2> /dev/null | wc -l | tr -d ' ')
	if [ "$n" = "0" ]; then
		echo "  = DO_NOT_TOUCH untouched (nothing inside modified)"
	else
		echo "  !! $n entries under DO_NOT_TOUCH were modified -- investigate"
		exit 1
	fi
fi
echo "done. $(du -sh "$CARD/plorpOS" | cut -f1) in $CARD/plorpOS"

# EJECT, do not just sync.
#
# On macOS `sync` is advisory for removable media: it does not guarantee FAT
# directory entries have landed. Writing tens of megabytes, calling sync, and
# then physically pulling the card is exactly the pattern that leaves
# half-written directory entries behind, and it has corrupted a card here.
sync
if [ "$(uname)" = "Darwin" ]; then
	echo "ejecting..."
	if diskutil eject "$CARD" > /dev/null 2>&1; then
		echo "EJECTED - safe to remove the card."
	else
		echo "!! EJECT FAILED. Do NOT pull the card yet."
		echo "   Close anything using $CARD, then: diskutil eject $CARD"
		exit 1
	fi
else
	echo "card flushed - unmount before removing."
fi
