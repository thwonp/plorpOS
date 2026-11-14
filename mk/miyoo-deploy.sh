#!/bin/sh
# Put a PLATFORM=miyoo build on a Miyoo Mini Flip over ssh (plorpos-f1p). The
# Flip's USB-C is charge-only, so Wi-Fi is the only way in.
#
#   mk/miyoo-deploy.sh all      launcher, Muse, diatom, SDL2/FFmpeg libraries,
#                               fonts, art, systems.cfg, the launch and ssh
#                               scripts, dropbear and the Wi-Fi driver
#   mk/miyoo-deploy.sh elf      the launcher (and native PICO-8's preload) only
#   mk/miyoo-deploy.sh diatom   the resident emulator only (../diatom-miyoo or DIATOM_ELF)
#   mk/miyoo-deploy.sh cores    the cores systems.cfg names, from FLIP_CORES
#   mk/miyoo-deploy.sh hook     the boot hook (sd/miyoo/updater) into .tmp_update/
#   mk/miyoo-deploy.sh package  a release zip of what `all` stages, plus the boot hook,
#                               a Roms/ skeleton, the cores, MD5SUMS and the install
#                               page, all from this tree and its build outputs (never
#                               from a card). Writes OUT/plorpOS-flip-v<VERSION>.zip.
#   mk/miyoo-deploy.sh verify   unzip that zip into an empty folder and check it
#
# MIYOO=root@<ip> (default the dev unit). FLIP_CORES = the from-source cores
# (mk/flip-cores/build-all.sh, pinned in mk/flip-cores/cores.lock).
# FLIP_INPUTS = the folder holding the pinned Wi-Fi driver, 8188fu.ko.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/mk/plorpos-files.sh"
MODE=${1:-elf}
MIYOO=${MIYOO:-root@192.168.0.73}
B=$ROOT/build/miyoo
LIB=$ROOT/sysroot-miyoo/usr/lib
DIATOM=${DIATOM_ELF:-$ROOT/../diatom-miyoo/build/miyoo/diatom}
FLIP_CURL=${FLIP_CURL:-$HOME/projects/misc/coding-team/plorpos-miyoo-mini-flip/curl-build/curl-flip}
FLIP_CORES=${FLIP_CORES:-$ROOT/build/flip-cores/out}
FLIP_INPUTS=${FLIP_INPUTS:-$HOME/projects/misc/coding-team/plorpos-miyoo-mini-flip/dist-inputs}
OUT=${OUT:-$HOME/projects/misc/coding-team/plorpos-miyoo-mini-flip/dist}
VER=$(sed -n 's/^FLIP_VERSION ?= *//p' "$ROOT/Makefile" | head -1)   # the Flip's own numbering
# Miyoo's own RTL8188FU driver for the Flip's 4.9.84 kernel (plorpos-f1p.62.4,
# docs/third-party/8188fu.md): the stock firmware has no copy of its own.
DRIVER_MD5=085ee00a031c401ea7d5a6a6b2ed2b0c
SSH="ssh -o ConnectTimeout=5 $MIYOO"
cfg_cores() { # systems.cfg -> the cores it names
	awk -F'|' '$1=="sys"{gsub(/^[ \t]+|[ \t]+$/,"",$4); print $4}' "$1" | sort -u
}
if [ "$MODE" = verify ]; then
	Z=$OUT/plorpOS-flip-v$VER.zip
	exec sh "$ROOT/mk/flip-verify.sh" "$Z" "$ROOT/mk/flip-cores/cores.lock" "$DRIVER_MD5"
fi
for f in tortos.elf muse musectl flip-pico8.so; do
	[ -f "$B/$f" ] || { echo "no $B/$f; run: make PLATFORM=miyoo" >&2; exit 1; }
done
if [ "$MODE" = hook ]; then
	$SSH 'mkdir -p /mnt/SDCARD/.tmp_update && cat > /mnt/SDCARD/.tmp_update/updater && chmod +x /mnt/SDCARD/.tmp_update/updater && sync' < "$ROOT/sd/miyoo/updater"
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
# pick is too heavy for two 1.2 GHz Cortex-A7s - GBA on gpsp, not mGBA (Game Boy and Color are on Gambatte
# everywhere now, plorpos-ssa)
# (user, 2026-11-04: mGBA lagged in play even at 1.2 GHz); both Neo Geo Pockets on
# RACE (2.6-2.9x cheaper per frame than Beetle NeoPop; user, 2026-11-10, accepting
# that one prototype, Cool Cool Jam, does not draw on it - plorpos-f1p.62.3.11).
flip_cfg() {
	sed -e 's/^\(sys|Game Boy Advance|[^|]*|\)armgba|/\1gpsp|/' \
	    -e 's/^\(sys|Neo Geo|[^|]*|\)fbneo|/\1fbalpha2012_neogeo|/' \
	    -e 's/^\(sys|Neo Geo Pocket[^|]*|[^|]*|\)mednafen_ngp|/\1race|/' \
	    -e 's/^\(sys|SNES|[^|]*|\)snes9x2010|/\1snes9x2005_plus|/' \
	    -e '/^sys|Arcade|Arcade|fbneo|/c\
sys|Arcade (FBA 2012)|Arcade (FBA 2012)|fbalpha2012|ARCADE|FBA2012.png|1FB4E0|zip,7z||Arcade\
sys|Arcade (MAME 2000)|Arcade (MAME 2000)|mame2000|MAME|MAME2000.png|129C50|zip,7z||Arcade' \
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
# Per-game alternatives on the shelf's Core row (src/main.c ALT_CORES, ALT_ON_MIYOO):
# systems.cfg names only each shelf's default core (plorpos-f1p.84).
FLIP_ALT_CORES="mednafen_ngp mednafen_supafaust"
stage_cores() { # systems.cfg dir: every core it names and the alternatives, from FLIP_CORES
	mkdir -p "$2"
	for c in $(cfg_cores "$1") $FLIP_ALT_CORES; do
		[ -f "$FLIP_CORES/${c}_libretro.so" ] ||
			{ echo "no core $c in $FLIP_CORES (mk/flip-cores/build-all.sh)" >&2; exit 1; }
		cp "$FLIP_CORES/${c}_libretro.so" "$2/"
	done
}
stage_all() {
	cp "$B/tortos.elf" "$B/muse" "$B/musectl" "$B/flip-pico8.so" "$S/"
	cp "$DIATOM" "$S/diatom"
	# The firmware has no curl (plorpos-f1p.36): ours, built by mk/flip-cores/build-curl.sh.
	[ -f "$FLIP_CURL" ] || { echo "no curl at $FLIP_CURL (run mk/flip-cores/build-curl.sh, or set FLIP_CURL)" >&2; exit 1; }
	cp "$FLIP_CURL" "$S/curl"; chmod +x "$S/curl"
	cp "$ROOT/sd/miyoo/launch.sh" "$ROOT/sd/miyoo/ssh.sh" "$S/"
	mkdir -p "$S/pico8"; cp "$ROOT/sd/tortos/pico8/wget" "$S/pico8/"   # Splore's downloads over HTTPS (the shim says why)
	flip_cfg "$S/systems.cfg"
	# ssh, off until switched on in Network Services (plorpos-f1p.62.2).
	mkdir -p "$S/bin"
	[ -f "$FLIP_CORES/bin/dropbearmulti" ] || { echo "no $FLIP_CORES/bin/dropbearmulti (mk/flip-cores/build-dropbear.sh)" >&2; exit 1; }
	cp "$FLIP_CORES/bin/dropbearmulti" "$S/bin/"; chmod +x "$S/bin/dropbearmulti"
	[ "$(md5sum < "$FLIP_INPUTS/8188fu.ko" | cut -c1-32)" = "$DRIVER_MD5" ] ||
		{ echo "$FLIP_INPUTS/8188fu.ko is missing or not the pinned driver" >&2; exit 1; }
	cp "$FLIP_INPUTS/8188fu.ko" "$S/"
	mkdir -p "$S/lib"
	for l in libSDL2-2.0.so.0 libSDL2_image-2.0.so.0 libSDL2_ttf-2.0.so.0 \
	         libavformat.so.60 libavcodec.so.60 libavfilter.so.9 libswresample.so.4 libavutil.so.58 \
	         libsqlite3.so.0; do
		cp "$(readlink -f "$LIB/$l")" "$S/lib/$l"
	done
	# Everything else every device ships, less the cores: an empty stand-in
	# per core satisfies plorpos_files, and cores/ is left off the card.
	F=$B/stub-cores; rm -rf "$F"; mkdir -p "$F"
	for c in $(cfg_cores "$S/systems.cfg"); do
		: > "$F/${c}_libretro.so"
	done
	plorpos_files "$S" "$F"
	# The mame2000 and fbalpha2012 shelves are named, and their art asked for,
	# by those cores' own romset maps (src/setmap.c), the Nano port's.
	mkdir -p "$S/res/maps"; cp "$ROOT/res/nano/maps/"*.txt "$S/res/maps/"
	rm -rf "$S/cores" "$S/shaders"   # no GPU, no shaders: just the two display filters' list
	mkdir -p "$S/shaders"; cp "$ROOT/sd/miyoo/shaders.cfg" "$S/shaders/"
	flip_cards "$S"
}
stage_package() {
	command -v zip >/dev/null || { echo "need zip" >&2; exit 1; }
	R=$B/card; rm -rf "$R"; mkdir -p "$R/.tmp_update"
	cp -R "$S/." "$R/TortOS/"
	cp "$ROOT/sd/miyoo/updater" "$R/.tmp_update/updater"; chmod +x "$R/.tmp_update/updater"
	stage_cores "$S/systems.cfg" "$R/TortOS/cores"
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
elf)    cp "$B/tortos.elf" "$B/flip-pico8.so" "$S/"; mkdir -p "$S/pico8"; cp "$ROOT/sd/tortos/pico8/wget" "$S/pico8/" ;;   # the launcher and native PICO-8's files
diatom) cp "$DIATOM" "$S/diatom" ;;
launch) cp "$ROOT/sd/miyoo/launch.sh" "$ROOT/sd/miyoo/ssh.sh" "$S/" ;;
cfg)    flip_cfg "$S/systems.cfg" ;;
cores)  flip_cfg "$B/systems.cfg.cores"; stage_cores "$B/systems.cfg.cores" "$S/cores" ;;
libs)   mkdir -p "$S/lib"; for l in "$LIB"/libSDL2-2.0.so.0 "$LIB"/libSDL2_image-2.0.so.0 "$LIB"/libSDL2_ttf-2.0.so.0 \
	        "$LIB"/libavformat.so.60 "$LIB"/libavcodec.so.60 "$LIB"/libavfilter.so.9 "$LIB"/libswresample.so.4 \
	        "$LIB"/libavutil.so.58 "$LIB"/libsqlite3.so.0; do cp "$(readlink -f "$l")" "$S/lib/$(basename "$l")"; done ;;
all)    stage_all ;;
package) stage_all; stage_package ;;
cards)  mkdir -p "$S/cards"; cp -R "$ROOT/res/cards/." "$S/cards/"; rm -rf "$S/cards/src"; flip_cards "$S" ;;
*) echo "mode: all | elf | diatom | cores | launch | libs | cfg | cards | hook | package | verify" >&2; exit 1 ;;
esac
[ "$MODE" = package ] && exit 0
push "$S"
echo "deployed $MODE to $MIYOO:/mnt/SDCARD/TortOS ($(md5sum "$B/tortos.elf" | cut -c1-8) tortos.elf)"
