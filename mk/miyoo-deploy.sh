#!/bin/sh
# Put a PLATFORM=miyoo build on a Miyoo Mini Flip over ssh (plorpos-f1p). The
# Flip's USB-C is charge-only, so Wi-Fi is the only way in.
#
#   mk/miyoo-deploy.sh all      launcher, Muse, diatom, SDL2/FFmpeg libraries,
#                               fonts, art, systems.cfg and the launch script
#   mk/miyoo-deploy.sh elf      the launcher only
#   mk/miyoo-deploy.sh diatom   the resident emulator only (../diatom-miyoo or DIATOM_ELF)
#   mk/miyoo-deploy.sh package a release zip of what `all` stages, plus the boot hook, a Roms/
#                               skeleton, the cores that passed the speed/memory gate (pulled
#                               from the dev card over ssh), MD5SUMS and the install page.
#                               Writes OUT/plorpOS-flip-v<VERSION>.zip; touches nothing on the card.
#   mk/miyoo-deploy.sh hook     the boot hook (sd/miyoo/updater) into .tmp_update/, the
#                               CFW's own updater kept as updater.allium - plorpOS
#                               falls back to it; TortOS/.boot-allium boots it outright
#
# MIYOO=root@<ip> (default the dev unit). Cores are not shipped yet
# (plorpos-f1p.11); `all` copies the card's Allium cores into TortOS/cores.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/mk/plorpos-files.sh"
MODE=${1:-elf}
MIYOO=${MIYOO:-root@192.168.0.73}
B=$ROOT/build/miyoo
LIB=$ROOT/sysroot-miyoo/usr/lib
DIATOM=${DIATOM_ELF:-$ROOT/../diatom-miyoo/build/miyoo/diatom}
SSH="ssh -o ConnectTimeout=5 $MIYOO"
for f in tortos.elf muse musectl; do
	[ -f "$B/$f" ] || { echo "no $B/$f; run: make PLATFORM=miyoo" >&2; exit 1; }
done
if [ "$MODE" = hook ]; then
	$SSH 'T=/mnt/SDCARD/.tmp_update; [ -f $T/updater.allium ] || mv $T/updater $T/updater.allium' &&
	$SSH 'cat > /mnt/SDCARD/.tmp_update/updater && chmod +x /mnt/SDCARD/.tmp_update/updater && sync' < "$ROOT/sd/miyoo/updater"
	echo "hook installed; it starts plorpOS at the next boot"
	exit 0
fi
[ "$MODE" = elf ] || [ -f "$DIATOM" ] || { echo "no diatom at $DIATOM (set DIATOM_ELF)" >&2; exit 1; }

# One tar over one ssh: the card is FAT, so no permissions or links to keep.
push() { # staging dir
	tar -C "$1" -cf - . | $SSH 'mkdir -p /mnt/SDCARD/TortOS && tar -C /mnt/SDCARD/TortOS -xf - && sync'
}
S=$B/stage
rm -rf "$S"; mkdir -p "$S"
# The Flip's systems.cfg: the shared one with its own cores where the shared
# pick is too heavy for two 1.2 GHz Cortex-A7s - GBA on gpsp, not mGBA
# (user, 2026-11-04: mGBA lagged in play even at 1.2 GHz).
flip_cfg() {
	sed -e 's/^\(sys|Game Boy Advance|[^|]*|\)mgba|/\1gpsp|/' \
	    -e 's/^\(sys|Neo Geo|[^|]*|\)fbneo|/\1fbalpha2012_neogeo|/' \
	    -e '/^sys|Arcade|Arcade|fbneo|/c\
sys|Arcade (FBA 2012)|Arcade (FBA 2012)|fbalpha2012|ARCADE|ARCADE.png|1FB4E0|zip,7z||Arcade\
sys|Arcade (MAME 2000)|Arcade (MAME 2000)|mame2000|MAME|MAME.png|1FB4E0|zip,7z||Arcade' \
	    "$ROOT/config/systems.cfg" > "$1"
}
# The Flip's card art: shrink-only to the focused card's frame area. The classic
# set is 640x820 (2.1 MB a texture) and is drawn at about 485x622, so the rest
# was memory the shelf never showed and a stretch on every frame. 301670 =
# cf_focus_rect for CF_LAYOUT_SYSTEMS at 752x560 (485x622). Smaller PNGs also
# decode faster and spend less transient memory in the loader threads. The
# fancy set is 384 or under already and is left alone (plorpos-f1p.23).
flip_cards() { # staging dir
	command -v magick >/dev/null || { echo "need ImageMagick to size the cards" >&2; exit 1; }
	for f in "$1"/cards/*/*.png "$1"/cards/*.png; do
		[ -f "$f" ] && magick "$f" -resize '301670@>' -strip PNG32:"$f.tmp" && mv "$f.tmp" "$f"
	done
}
fetch_cores() { # copy each systems.cfg core the card lacks from Allium's RetroArch set
	$SSH 'D=/mnt/SDCARD/TortOS; mkdir -p $D/cores
		for c in $(awk -F"|" "\$1==\"sys\"{gsub(/^[ \t]+|[ \t]+$/,\"\",\$4); print \$4}" $D/systems.cfg | sort -u); do
			[ -f $D/cores/${c}_libretro.so ] || cp /mnt/SDCARD/RetroArch/.retroarch/cores/${c}_libretro.so $D/cores/ 2>/dev/null ||
				echo "no Allium core $c"
		done; sync'
}
stage_all() {
	cp "$B/tortos.elf" "$B/muse" "$B/musectl" "$S/"
	cp "$DIATOM" "$S/diatom"
	cp "$ROOT/sd/miyoo/launch.sh" "$S/"
	flip_cfg "$S/systems.cfg"
	mkdir -p "$S/lib"
	for l in libSDL2-2.0.so.0 libSDL2_image-2.0.so.0 libSDL2_ttf-2.0.so.0 \
	         libavformat.so.60 libavcodec.so.60 libavfilter.so.9 libswresample.so.4 libavutil.so.58 \
	         libsqlite3.so.0; do
		cp "$(readlink -f "$LIB/$l")" "$S/lib/$l"
	done
	# Everything else every device ships, less the cores: an empty stand-in
	# per core satisfies plorpos_files, and cores/ is left off the card.
	F=$B/stub-cores; rm -rf "$F"; mkdir -p "$F"
	for c in $(awk -F'|' '$1=="sys"{gsub(/^[ \t]+|[ \t]+$/,"",$4); print $4}' "$S/systems.cfg" | sort -u); do
		: > "$F/${c}_libretro.so"
	done
	plorpos_files "$S" "$F"
	rm -rf "$S/cores" "$S/shaders"   # no GPU, no shaders
	flip_cards "$S"
}
stage_package() {
	command -v zip >/dev/null || { echo "need zip" >&2; exit 1; }
	VER=$(sed -n 's/^VERSION ?= *//p' "$ROOT/Makefile" | head -1)
	OUT=${OUT:-$HOME/projects/misc/coding-team/plorpos-miyoo-mini-flip/dist}
	# Cores to leave out of the zip; none now. Override with SKIP_CORES.
	SKIP=${SKIP_CORES:-}
	R=$B/card; rm -rf "$R"; mkdir -p "$R/.tmp_update" "$R/TortOS/cores"
	cp -R "$S/." "$R/TortOS/"
	cp "$ROOT/sd/miyoo/updater" "$R/.tmp_update/updater"; chmod +x "$R/.tmp_update/updater"
	# The Wi-Fi driver the hook loads from .tmp_update; stock firmware has no copy
	# (it came with Allium), so a card without Allium would have no Wi-Fi.
	$SSH 'cat /mnt/SDCARD/.tmp_update/8188fu.ko' > "$R/.tmp_update/8188fu.ko" ||
		{ echo "no 8188fu.ko on the dev card" >&2; exit 1; }
	for c in $(awk -F'|' '$1=="sys"{gsub(/^[ \t]+|[ \t]+$/,"",$4); print $4}' "$S/systems.cfg" | sort -u); do
		case " $SKIP " in *" $c "*) continue ;; esac
		$SSH "cat /mnt/SDCARD/TortOS/cores/${c}_libretro.so" > "$R/TortOS/cores/${c}_libretro.so" ||
			{ echo "no core $c on the dev card" >&2; exit 1; }
	done
	awk -F'|' '$1=="sys"{gsub(/^[ \t]+|[ \t]+$/,"",$3); print $3}' "$S/systems.cfg" |
		while read -r d; do mkdir -p "$R/Roms/$d"; done
	mkdir -p "$R/Bios" "$R/Saves"
	cp "$ROOT/docs/install-flip.md" "$R/INSTALL-FLIP.md"
	( cd "$R" && find . -type f ! -name MD5SUMS | sort | xargs md5sum > MD5SUMS )
	mkdir -p "$OUT"; Z=$OUT/plorpOS-flip-v$VER.zip; rm -f "$Z"
	( cd "$R" && zip -qr "$Z" . -x '.DS_Store' '._*' )
	echo "package: $Z ($(du -h "$Z" | cut -f1), $(wc -l < "$R/MD5SUMS") files)"
}
case $MODE in
elf)    cp "$B/tortos.elf" "$S/" ;;
diatom) cp "$DIATOM" "$S/diatom" ;;
launch) cp "$ROOT/sd/miyoo/launch.sh" "$S/" ;;
cfg)    flip_cfg "$S/systems.cfg" ;;
libs)   mkdir -p "$S/lib"; for l in "$LIB"/libSDL2-2.0.so.0 "$LIB"/libSDL2_image-2.0.so.0 "$LIB"/libSDL2_ttf-2.0.so.0 \
	        "$LIB"/libavformat.so.60 "$LIB"/libavcodec.so.60 "$LIB"/libavfilter.so.9 "$LIB"/libswresample.so.4 \
	        "$LIB"/libavutil.so.58 "$LIB"/libsqlite3.so.0; do cp "$(readlink -f "$l")" "$S/lib/$(basename "$l")"; done ;;
all)    stage_all ;;
package) stage_all; stage_package ;;
cards)  mkdir -p "$S/cards"; cp -R "$ROOT/res/cards/." "$S/cards/"; rm -rf "$S/cards/src"; flip_cards "$S" ;;
*) echo "mode: all | elf | diatom | launch | libs | cfg | cards | hook | package" >&2; exit 1 ;;
esac
[ "$MODE" = package ] && exit 0
push "$S"
case $MODE in all|cfg) fetch_cores ;; esac
echo "deployed $MODE to $MIYOO:/mnt/SDCARD/TortOS ($(md5sum "$B/tortos.elf" | cut -c1-8) tortos.elf)"
