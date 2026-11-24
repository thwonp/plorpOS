#!/bin/sh
# Push a Rotate build to the device over ssh (plorpos-3yv), for development.
# The card image is mk/payload.sh's business (.13); this only replaces files.
#
#   mk/rotate-deploy.sh [elf|diatom|all|boot|stage|package]   (ROTATE_HOST, ROTATE_KEY, DIATOM_ELF override)
#
# elf: the launcher alone. diatom: the resident emulator alone (../diatom,
# PORT=rotate tools/brick-make.sh). all: both, the cores, Muse's tools, the session
# scripts (sd/rotate), the files every device's plorpOS/ holds (mk/plorpos-files.sh)
# and a ROM folder per shelf. boot: also installs
# plorpos.service into the rootfs and enables it in place of tty1's getty.
# stage: the plorpOS folder into $ROTATE_STAGE, no device (mk/rotate-image.sh).
# package: the release's update zip, out/plorpOS-v<VERSION>-rotate-update.zip -
# plorpOS/ alone, copied over the PLORPOS partition by hand or by the update
# check (plorpos-koo). No device.
# The runtime libraries are Debian's: apt, not this script.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/mk/plorpos-files.sh"
MODE=${1:-elf}
HOST=${ROTATE_HOST:-root@192.168.0.69}
KEY=${ROTATE_KEY:-$HOME/.ssh/rgrotate}
B=$ROOT/build/rotate
DIATOM=${DIATOM_ELF:-$ROOT/../diatom/build/rotate/diatom}
P=/mnt/SDCARD/plorpOS
ssh_() { ssh -i "$KEY" -o IdentitiesOnly=yes "$HOST" "$@"; }

[ -f "$B/tortos.elf" ] || { echo "no $B/tortos.elf; run: make PLATFORM=rotate" >&2; exit 1; }
# Beside the old one and renamed over it, so a running process keeps its inode.
put() { ssh_ "cat > $P/$2.new && chmod +x $P/$2.new && mv -f $P/$2.new $P/$2" < "$1"; }
if [ "$MODE" = diatom ]; then
	[ -f "$DIATOM" ] || { echo "no diatom at $DIATOM (set DIATOM_ELF)" >&2; exit 1; }
	put "$DIATOM" diatom
	ssh_ 'pkill -f "[p]lorpOS/diatom --socket"; true' < /dev/null   # [T]: not this shell
	echo "deployed diatom $(md5sum "$DIATOM" | cut -c1-8); the launcher restarts it"
	exit 0
fi
# The plorpOS folder, as the card holds it, into $1: the launcher, Diatom,
# Muse's tools and the session (sd/rotate), then what every device's plorpOS/
# holds (mk/plorpos-files.sh: systems.cfg's cores, cards, fonts, web, CA bundle,
# shaders, licences, arcade titles). No pico8sdl.so or wget shim: the Rotate's
# PICO-8 needs neither.
stage() {
	S=$1
	mkdir -p "$S/rotate/portmaster"
	cp "$B/tortos.elf" "$S/"
	[ -f "$DIATOM" ] && cp "$DIATOM" "$S/diatom"
	cp "$ROOT/sd/rotate/session.sh" "$ROOT/sd/rotate/launch.sh" "$ROOT/sd/rotate/sway.conf" "$S/rotate/"
	# base618's device tree, which session.sh puts on /boot over the one every
	# card so far shipped (plorpos-5eq.25: the swivel wakes from deep sleep).
	mkdir -p "$S/rotate/boot"
	cp "$ROOT/sd/rotate/boot/ums512-rg-rotate.dtb" "$S/rotate/boot/"
	cp "$ROOT/sd/rotate/portmaster/"* "$S/rotate/portmaster/"
	# PortMaster itself, unpacked on first use (plorpos-3yv.86.4; mk/fetch-vendor.sh).
	[ -f "$ROOT/vendor/portmaster/PortMaster.zip" ] ||
		{ echo "no vendor/portmaster/PortMaster.zip; run mk/fetch-vendor.sh" >&2; exit 1; }
	cp "$ROOT/vendor/portmaster/PortMaster.zip" "$S/rotate/portmaster/"
	for f in muse musectl btplayer; do cp "$B/$f" "$S/"; done
	# The Rotate's own shelves (config/systems-rotate.cfg) after everyone's.
	cat "$ROOT/config/systems.cfg" "$ROOT/config/systems-rotate.cfg" > "$S/systems.cfg"
	# Its own cores first (vendor/cores-rotate). PokeDaisy DS is no shelf's core
	# but the Nintendo DS shelf's Core row switches to it (main.c ALT_CORES).
	plorpos_files "$S" "$ROOT/vendor/cores-rotate $ROOT/vendor/cores $ROOT/vendor/cores-brick" "pokedaisy_gba pokedaisy_gbc pokedaisy_ds mgba"
	plorpos_tailscale "$S" "$B"
	plorpos_syncthing "$S"
	plorpos_unzip "$S"    # the update check's (plorpos-koo): Debian's image has none
}
if [ "$MODE" = stage ]; then
	[ -n "${ROTATE_STAGE:-}" ] || { echo "stage: set ROTATE_STAGE" >&2; exit 1; }
	[ -f "$DIATOM" ] || { echo "no diatom at $DIATOM (set DIATOM_ELF)" >&2; exit 1; }
	mkdir -p "$ROTATE_STAGE"
	stage "$ROTATE_STAGE"
	echo "staged plorpOS/ in $ROTATE_STAGE"
	exit 0
fi
if [ "$MODE" = package ]; then
	[ -f "$DIATOM" ] || { echo "no diatom at $DIATOM (set DIATOM_ELF)" >&2; exit 1; }
	VER=$(cat "$B/version")
	Z=$ROOT/out/plorpOS-v$VER-rotate-update.zip
	T=$(mktemp -d)
	trap 'rm -rf "$T"' EXIT
	mkdir -p "$T/plorpOS" "$ROOT/out"
	stage "$T/plorpOS"
	rm -f "$Z"
	( cd "$T" && zip -qr "$Z" plorpOS -x '.DS_Store' '._*' )
	echo "release zip:  $Z  ($(du -h "$Z" | cut -f1))"
	exit 0
fi
ssh_ "mkdir -p $P" < /dev/null
if [ "$MODE" = all ] || [ "$MODE" = boot ]; then
	T=$(mktemp -d)
	trap 'rm -rf "$T"' EXIT
	stage "$T"
	# The two that may be running go by put, below, not over their inodes.
	tar -C "$T" --exclude=./tortos.elf --exclude=./diatom -cf - . | ssh_ "tar -C $P --no-same-owner -xf -"
	[ -f "$DIATOM" ] && put "$DIATOM" diatom
	ssh_ "mkdir -p /mnt/SDCARD/Roms /mnt/SDCARD/Music /mnt/SDCARD/Bios /mnt/SDCARD/Saves" < /dev/null
	# A ROM folder per shelf, as payload.sh makes them - a shelf added since the
	# card was set up has one too (plorpos-3yv.89).
	plorpos_rom_folders "$T" | ssh_ 'while IFS= read -r f; do mkdir -p "/mnt/SDCARD/Roms/$f/.media"; done'
	plorpos_save_folders "$T" | ssh_ 'while IFS= read -r f; do mkdir -p "/mnt/SDCARD/Saves/$f"; done'
fi
if [ "$MODE" = boot ]; then
	ssh_ "cat > /etc/systemd/system/plorpos.service && systemctl daemon-reload && \
	      systemctl disable rotate-pad2key.service && systemctl enable plorpos.service" \
		< "$ROOT/sd/rotate/plorpos.service"
fi
put "$B/tortos.elf" tortos.elf
# Tailscale (plorpos-8ns.7), which "all" brings with the rest of the folder.
if [ "$MODE" = elf ] && [ -f "$B/tailscaled" ]; then
	put "$B/tailscaled" tailscaled
	put "$ROOT/sd/tortos/tailscale.sh" tailscale.sh
	put "$B/LICENSE-tailscale.md" LICENSE-tailscale.md
fi
# Syncthing (plorpos-5jh), the same way.
ST=$ROOT/build/syncthing-arm64
if [ "$MODE" = elf ] && [ -f "$ST/syncthing" ]; then
	put "$ST/syncthing" syncthing
	put "$ROOT/sd/tortos/syncthing.sh" syncthing.sh
	put "$ST/LICENSE-syncthing.md" LICENSE-syncthing.md
fi
echo "deployed ($MODE) to $HOST:$P"
