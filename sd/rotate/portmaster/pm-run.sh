#!/bin/sh
# Runs PortMaster or a port on the RG Rotate (plorpos-3yv.86.2): pm-run.sh SCRIPT [ARGS].
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
# Ports mount runtimes and keep settings in $HOME: theirs, not the launcher's.
export HOME=$PMDATA/home
mkdir -p "$HOME"

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
exec bash "$@"
