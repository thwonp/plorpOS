#!/bin/sh
# Runs PortMaster or a port on the GKD 350H Ultra (plorpos-46o.5): pm-run.sh
# SCRIPT [ARGS], or removes one: pm-run.sh --uninstall ZIP. The Rotate's
# runner (sd/rotate/portmaster/pm-run.sh) less what ROCKNIX already is: the
# system PortMaster's device_info names, with its own GPU stack and RetroArch.
# PortMaster lives where the Rotate keeps it, on the card, not in /roms/ports.
#
# Unofficial PortMaster support: if something PortMaster related does not
# work, do not bug the PortMaster devs.
HERE=$(dirname "$0")
CARD=/storage/games-external
PMDATA=$CARD/.userdata/shared/portmaster
PM=$PMDATA/PortMaster

export XDG_DATA_HOME=$PMDATA
export HM_TOOLS_DIR=$PMDATA HM_PORTS_DIR=$CARD/Roms/Ports HM_SCRIPTS_DIR=$CARD/Roms/Ports
# The launcher's own environment has no session variables; sway's.
export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/var/run/0-runtime-dir}
export WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-wayland-1}
# Ports mount runtimes and keep settings in $HOME: theirs, not the launcher's.
export HOME=$PMDATA/home
mkdir -p "$HOME"

if [ "$1" = --uninstall ]; then
	cd "$PM" || exit 1
	exec python3 harbourmaster --quiet --no-check uninstall "$2"
fi

# A port the launcher had to KILL never ran its own cleanup (plorpos-3yv.86.3):
# its runtimes, squashfs under PortMaster's folder, and its bind mounts into
# $HOME. Only those - ROCKNIX itself runs from squashfs, and taking every
# squashfs mount down (as on the Rotate, whose system has none) left / alone
# but tore away the rest, the GL vendor files with them: every program
# started after the first port fell back to Mesa's software GL, 2 fps
# (plorpos-46o.5).
grep -E " squashfs | $HOME/" /proc/mounts | cut -d" " -f2 | grep -E "^($PMDATA|$HOME)/" | sort -r |
	while read -r m; do umount "$m" 2>/dev/null || umount -l "$m"; done

# Unpacked from the payload's zip the first time (plorpos-3yv.86.4).
if [ ! -f "$PM/PortMaster.sh" ] && [ -f "$HERE/PortMaster.zip" ]; then
	rm -rf "$PMDATA/.install"
	python3 -m zipfile -e "$HERE/PortMaster.zip" "$PMDATA/.install" &&
		rm -rf "$PM" && mv "$PMDATA/.install/PortMaster" "$PM" && rm -rf "$PMDATA/.install" ||
		exit 1
fi

# Always: 2 KB, and ROCKNIX's BusyBox has no cmp.
cp "$HERE/control.txt" "$PM/control.txt"

cd "$(dirname "$1")" || exit 1
exec bash "$@"
