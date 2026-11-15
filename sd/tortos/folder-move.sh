#!/bin/sh
# The install folder's rename, TortOS/ -> plorpOS/ (plorpos-nzi). Run by every
# port's launch script before anything else starts, from plorpOS/.
#
#   folder-move.sh CARD [KEEP]
#
# An update is copied over the card, so after it the card holds the new
# plorpOS/ beside the old TortOS/. Everything in TortOS/ that plorpOS/ does not
# have is moved across at the same path - .last, PICO-8's own files, ssh keys,
# Diatom's marker files, a core someone added, anything else nobody listed -
# and the rest, the old release's copies of what plorpOS/ now ships, is
# deleted. KEEP (the GKD's launch.sh, which old drop-ins still start) stays.
# Moves are renames on the one card, so it costs milliseconds. Nothing to do
# once TortOS/ is gone, or holds KEEP alone. One summary line, to stdout.
CARD=$1
KEEP=$2
OLD=$CARD/TortOS
NEW=$CARD/plorpOS
LIST=/tmp/folder-move.$$

[ -d "$OLD" ] && [ -d "$NEW" ] || exit 0
cd "$OLD" || exit 0
find . ! -type d > "$LIST"
[ -s "$LIST" ] && [ "$(cat "$LIST")" != "./$KEEP" ] || { rm -f "$LIST"; exit 0; }

moved=0
gone=0
while IFS= read -r f; do
	f=${f#./}
	[ -n "$KEEP" ] && [ "$f" = "$KEEP" ] && continue
	if [ -e "$NEW/$f" ]; then
		gone=$((gone + 1))
	elif mkdir -p "$NEW/$(dirname "$f")" && mv "$OLD/$f" "$NEW/$f"; then
		moved=$((moved + 1))
	fi
done < "$LIST"
rm -f "$LIST"
cd / || exit 0
if [ -n "$KEEP" ]; then
	for e in "$OLD"/* "$OLD"/.[!.]* "$OLD"/..?*; do
		[ -e "$e" ] || continue
		[ "$e" = "$OLD/$KEEP" ] && continue
		rm -rf "$e"
	done
else
	rm -rf "$OLD"
fi
sync
echo "folder: TortOS/ -> plorpOS/: moved $moved file(s), dropped $gone the new release replaces${KEEP:+, kept $KEEP}"
