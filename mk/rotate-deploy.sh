#!/bin/sh
# Push a Rotate build to the device over ssh (plorpos-3yv), for development.
# The card image is mk/payload.sh's business (.13); this only replaces files.
#
#   mk/rotate-deploy.sh [elf|diatom|all|boot|stage]   (ROTATE_HOST, ROTATE_KEY, DIATOM_ELF override)
#
# elf: the launcher alone. diatom: the resident emulator alone (../diatom,
# PORT=rotate tools/brick-make.sh). all: both, the cores, Muse's tools, the session
# scripts (sd/rotate) and the shared files payload.sh puts in plorpOS/
# (systems.cfg, cards, font, web, CA bundle, shaders). boot: also installs
# plorpos.service into the rootfs and enables it in place of tty1's getty.
# stage: the plorpOS folder into $ROTATE_STAGE, no device (mk/rotate-image.sh).
# The runtime libraries are Debian's: apt, not this script.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
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
# The plorpOS folder, as the card holds it, into $1: the launcher, Diatom
# and its cores, Muse's tools, the session (sd/rotate) and the shared files
# payload.sh puts in plorpOS/ (systems.cfg, cards, font, web, CA bundle,
# shaders). No pico8sdl.so or wget shim: the Rotate's PICO-8 needs neither.
stage() {
	S=$1
	mkdir -p "$S/cards" "$S/res/web" "$S/shaders" "$S/rotate" "$S/cores"
	cp "$B/tortos.elf" "$S/"
	[ -f "$DIATOM" ] && cp "$DIATOM" "$S/diatom"
	cp "$ROOT"/vendor/cores/*_libretro.so "$S/cores/"
	# The Rotate's own cores and shelves (config/systems-rotate.cfg).
	cp "$ROOT"/vendor/cores-rotate/*_libretro.so "$S/cores/"
	cp "$ROOT/sd/rotate/session.sh" "$ROOT/sd/rotate/launch.sh" "$ROOT/sd/rotate/sway.conf" "$S/rotate/"
	for f in muse musectl btplayer; do cp "$B/$f" "$S/"; done
	cp "$ROOT/res/fonts/menu.ttf" "$ROOT/res/singles.png" \
	   "$ROOT/res/ssl/cacert.pem" "$S/"
	cat "$ROOT/config/systems.cfg" "$ROOT/config/systems-rotate.cfg" > "$S/systems.cfg"
	cp -R "$ROOT/res/cards/." "$S/cards/"
	cp "$ROOT/res/web/"* "$S/res/web/"
	cp "$ROOT/res/fonts/menu.ttf" "$S/res/web/menu.ttf"
	cp "$ROOT/res/shaders/"* "$S/shaders/"
}
if [ "$MODE" = stage ]; then
	[ -n "${ROTATE_STAGE:-}" ] || { echo "stage: set ROTATE_STAGE" >&2; exit 1; }
	[ -f "$DIATOM" ] || { echo "no diatom at $DIATOM (set DIATOM_ELF)" >&2; exit 1; }
	mkdir -p "$ROTATE_STAGE"
	stage "$ROTATE_STAGE"
	echo "staged plorpOS/ in $ROTATE_STAGE"
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
fi
if [ "$MODE" = boot ]; then
	ssh_ "cat > /etc/systemd/system/plorpos.service && systemctl daemon-reload && \
	      systemctl disable rotate-pad2key.service && systemctl enable plorpos.service" \
		< "$ROOT/sd/rotate/plorpos.service"
fi
put "$B/tortos.elf" tortos.elf
echo "deployed ($MODE) to $HOST:$P"
