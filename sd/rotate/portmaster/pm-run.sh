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

cmp -s "$HERE/control.txt" "$PM/control.txt" || cp "$HERE/control.txt" "$PM/control.txt"

cd "$(dirname "$1")" || exit 1
exec bash "$@"
