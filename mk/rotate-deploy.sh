#!/bin/sh
# Push a Rotate build to the device over ssh (plorpos-3yv), for development.
# The card image is mk/payload.sh's business (.13); this only replaces files.
#
#   mk/rotate-deploy.sh [elf|diatom|all|boot]   (ROTATE_HOST, ROTATE_KEY, DIATOM_ELF override)
#
# elf: the launcher alone. diatom: the resident emulator alone (../diatom,
# PORT=rotate tools/brick-make.sh). all: both, the cores, Muse's tools, the session
# scripts (sd/rotate) and the shared files payload.sh puts in TortOS/
# (systems.cfg, cards, font, web, CA bundle, shaders). boot: also installs
# plorpos.service into the rootfs and enables it in place of tty1's getty.
# The runtime libraries are Debian's: apt, not this script.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
MODE=${1:-elf}
HOST=${ROTATE_HOST:-root@192.168.0.69}
KEY=${ROTATE_KEY:-$HOME/.ssh/rgrotate}
B=$ROOT/build/rotate
DIATOM=${DIATOM_ELF:-$ROOT/../diatom/build/rotate/diatom}
P=/mnt/SDCARD/TortOS
ssh_() { ssh -i "$KEY" -o IdentitiesOnly=yes "$HOST" "$@"; }

[ -f "$B/tortos.elf" ] || { echo "no $B/tortos.elf; run: make PLATFORM=rotate" >&2; exit 1; }
# Beside the old one and renamed over it, so a running process keeps its inode.
put() { ssh_ "cat > $P/$2.new && chmod +x $P/$2.new && mv -f $P/$2.new $P/$2" < "$1"; }
if [ "$MODE" = diatom ]; then
	[ -f "$DIATOM" ] || { echo "no diatom at $DIATOM (set DIATOM_ELF)" >&2; exit 1; }
	put "$DIATOM" diatom
	ssh_ 'pkill -f "[T]ortOS/diatom --socket"; true' < /dev/null   # [T]: not this shell
	echo "deployed diatom $(md5sum "$DIATOM" | cut -c1-8); the launcher restarts it"
	exit 0
fi
ssh_ "mkdir -p $P" < /dev/null
if [ "$MODE" = all ] || [ "$MODE" = boot ]; then
	S=$(mktemp -d)
	trap 'rm -rf "$S"' EXIT
	mkdir -p "$S/cards" "$S/res/web" "$S/shaders" "$S/rotate" "$S/cores" "$S/pico8"
	cp "$ROOT"/vendor/cores/*_libretro.so "$S/cores/"
	cp "$ROOT/sd/tortos/pico8/wget" "$S/pico8/"
	[ -f "$DIATOM" ] && cp "$DIATOM" "$S/diatom"
	cp "$ROOT/sd/rotate/session.sh" "$ROOT/sd/rotate/launch.sh" "$ROOT/sd/rotate/sway.conf" "$S/rotate/"
	for f in muse musectl btplayer pico8sdl.so; do cp "$B/$f" "$S/"; done
	cp "$ROOT/config/systems.cfg" "$ROOT/res/fonts/menu.ttf" "$ROOT/res/singles.png" \
	   "$ROOT/res/ssl/cacert.pem" "$S/"
	cp -R "$ROOT/res/cards/." "$S/cards/"
	cp "$ROOT/res/web/"* "$S/res/web/"
	cp "$ROOT/res/fonts/menu.ttf" "$S/res/web/menu.ttf"
	cp "$ROOT/res/shaders/"* "$S/shaders/"
	tar -C "$S" -cf - . | ssh_ "tar -C $P -xf -"
	ssh_ "mkdir -p /mnt/SDCARD/Roms /mnt/SDCARD/Music /mnt/SDCARD/Bios /mnt/SDCARD/Saves" < /dev/null
fi
if [ "$MODE" = boot ]; then
	ssh_ "cat > /etc/systemd/system/plorpos.service && systemctl daemon-reload && \
	      systemctl disable rotate-pad2key.service && systemctl enable plorpos.service" \
		< "$ROOT/sd/rotate/plorpos.service"
fi
put "$B/tortos.elf" tortos.elf
echo "deployed ($MODE) to $HOST:$P"
