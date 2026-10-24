#!/bin/sh
# Runs PortMaster or a port on the RG Rotate (plorpos-3yv.86.2): pm-run.sh SCRIPT [ARGS],
# or removes one: pm-run.sh --uninstall ZIP.
# The launcher starts every port through this, so plorpOS's control.txt is
# back in place before each run even after PortMaster updated itself.
#
# Unofficial PortMaster support: if something PortMaster related does not
# work, do not bug the PortMaster devs.
HERE=$(dirname "$0")
PMDATA=/mnt/SDCARD/.userdata/shared/portmaster
PM=$PMDATA/PortMaster

# Ports find PortMaster in $XDG_DATA_HOME/PortMaster; harbourmaster (the
# GUI's side) takes its folders from HM_*.
export XDG_DATA_HOME=$PMDATA
export HM_TOOLS_DIR=$PMDATA HM_PORTS_DIR=/mnt/SDCARD/Roms/Ports HM_SCRIPTS_DIR=/mnt/SDCARD/Roms/Ports
# X11 ports: sway starts Xwayland on the first one (sway.conf).
export DISPLAY=${DISPLAY:-:0}
# Ports get plorpOS's own Mesa (mk/build-ports-mesa.sh): no LLVM, which
# crashed Mono's ports, and GLES 1 (plorpos-3yv.86.13). A port's own libs
# (gl4es, libs/) still go in front of it.
[ -d /usr/lib/plorpos-mesa/lib ] &&
	export LD_LIBRARY_PATH=/usr/lib/plorpos-mesa/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
# Ports mount runtimes and keep settings in $HOME: theirs, not the launcher's.
export HOME=$PMDATA/home
mkdir -p "$HOME"

# pm-run.sh --uninstall ZIP: Delete Game on the Ports shelf (plorpos-3yv.86.12).
# PortMaster's own uninstall removes what the port's port.json lists - its
# script and folder, saves inside included - and keeps anything another
# installed port also lists.
if [ "$1" = --uninstall ]; then
	cd "$PM" || exit 1
	exec python3 harbourmaster --quiet --no-check uninstall "$2"
fi

# A port the launcher had to KILL never ran its own cleanup: its runtimes are
# still mounted (squashfs) and renpy's conf bind mount still sits in $HOME.
# Nothing else on the system mounts either, and nothing of PortMaster's runs
# now (plorpos-3yv.86.3).
grep -E " squashfs | $HOME/" /proc/mounts | cut -d" " -f2 | sort -r |
	while read -r m; do umount "$m" 2>/dev/null || umount -l "$m"; done

# PortMaster comes in the payload as its release zip, unpacked the first time
# it is wanted (plorpos-3yv.86.4) - beside, then moved in, so a run cut short
# leaves no half a PortMaster. After that it updates itself.
if [ ! -f "$PM/PortMaster.sh" ] && [ -f "$HERE/PortMaster.zip" ]; then
	rm -rf "$PMDATA/.install"
	python3 -m zipfile -e "$HERE/PortMaster.zip" "$PMDATA/.install" &&
		rm -rf "$PM" && mv "$PMDATA/.install/PortMaster" "$PM" && rm -rf "$PMDATA/.install" ||
		exit 1
fi

cmp -s "$HERE/control.txt" "$PM/control.txt" || cp "$HERE/control.txt" "$PM/control.txt"

# RetroArch ports call /usr/bin/retroarch: Diatom stands in (plorpos-3yv.86.6).
# Only where nothing is: a real RetroArch keeps its place.
[ -e /usr/bin/retroarch ] || [ -L /usr/bin/retroarch ] || ln -s "$HERE/retroarch" /usr/bin/retroarch

# PortMaster's GUI is not a port: control.txt names the system to it
# as it is, not as ROCKNIX.
[ "$(basename "$1")" = PortMaster.sh ] && export PLORPOS_PM_GUI=1

cd "$(dirname "$1")" || exit 1
[ -n "${PLORPOS_PM_GUI:-}" ] && exec bash "$@"

# Older port scripts source device_info.txt again after control.txt, and its
# probe names the system anew ("Debian GNU/Linux"): Stardew then took the
# default GL path, gl4es on the display card, and found no GPU
# (plorpos-3yv.86.18). PortMaster rewrites its files when it updates, so
# none of them is patched: the script is sourced with a RETURN trap, which
# names ROCKNIX again each time a file it sources finishes.
exec bash -c 'trap "export CFW_NAME=ROCKNIX" RETURN; . "$0" "$@"' "$@"
