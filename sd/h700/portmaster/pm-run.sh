#!/bin/sh
# Runs PortMaster or a port on the H700 handhelds (plorpos-2wl.10): pm-run.sh
# SCRIPT [ARGS], or removes one: pm-run.sh --uninstall ZIP. The Rotate's
# runner (sd/rotate/portmaster/pm-run.sh) on BaseOS, which has no bash and no
# python3: pmtools.squashfs carries both, loop-mounted for the run.
#
# Unofficial PortMaster support: if something PortMaster related does not
# work, do not bug the PortMaster devs.
HERE=$(dirname "$0")
CARD=/mnt/SDCARD
PMDATA=$CARD/.userdata/shared/portmaster
PM=$PMDATA/PortMaster
TOOLS=/run/plorpos-pmtools

export XDG_DATA_HOME=$PMDATA
export HM_TOOLS_DIR=$PMDATA HM_PORTS_DIR=$CARD/Roms/Ports HM_SCRIPTS_DIR=$CARD/Roms/Ports
# Ports mount runtimes and keep settings in $HOME: theirs, not the launcher's.
export HOME=$PMDATA/home
mkdir -p "$HOME"

# A port the launcher had to KILL never ran its own cleanup: its runtimes
# (squashfs under PortMaster's folder) and bind mounts into $HOME or beside
# PortMaster (Shovel Knight's saves) are still there. Only those: BaseOS itself
# runs /usr/lib from squashfs. /proc/mounts writes a space in a path as \040, and
# the card as /mnt/sdcard (-i).
grep -iE " squashfs | $PMDATA/" /proc/mounts | cut -d" " -f2 | sed 's/\\040/ /g' | grep -iE "^($PMDATA|$HOME|$TOOLS)" | sort -r |
	while read -r m; do umount "$m" 2>/dev/null || umount -l "$m"; done

# Port scripts tee their log through bash's process substitution, which wants
# /dev/fd; BaseOS's /dev (devtmpfs) has none.
[ -e /dev/fd ] || ln -s /proc/self/fd /dev/fd

mkdir -p "$TOOLS"
mount -t squashfs -o loop,ro "$HERE/pmtools.squashfs" "$TOOLS" || exit 1
export PATH=$TOOLS/bin:$TOOLS/python/bin:$PATH
# Our SDL2 (BaseOS has no aarch64 one) and what a port may ask of it.
# lib32: our SDL2 for 32-bit ports, ahead of the firmware's (mk/build-sdl2-armhf.sh).
export LD_LIBRARY_PATH=$CARD/TortOS/lib:$CARD/TortOS/lib32:$TOOLS/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export SSL_CERT_FILE=$CARD/TortOS/cacert.pem

if [ "$1" = --uninstall ]; then
	cd "$PM" || exit 1
	python3 harbourmaster --quiet --no-check uninstall "$2"
	r=$?
	umount "$TOOLS"
	exit $r
fi

# Unpacked from the payload's zip the first time - beside, then moved in, so
# a run cut short leaves no half a PortMaster.
if [ ! -f "$PM/PortMaster.sh" ] && [ -f "$HERE/PortMaster.zip" ]; then
	rm -rf "$PMDATA/.install"
	python3 -m zipfile -e "$HERE/PortMaster.zip" "$PMDATA/.install" &&
		rm -rf "$PM" && mv "$PMDATA/.install/PortMaster" "$PM" && rm -rf "$PMDATA/.install" ||
		exit 1
fi

for f in control.txt libgl_ROCKNIX.txt; do
	cmp -s "$HERE/$f" "$PM/$f" || cp "$HERE/$f" "$PM/$f"
done

# RetroArch ports call /usr/bin/retroarch (raloc is /usr/bin for ROCKNIX):
# Diatom stands in, where nothing is already (plorpos-3yv.86.6).
[ -e /usr/bin/retroarch ] || [ -L /usr/bin/retroarch ] || ln -s "$HERE/retroarch" /usr/bin/retroarch

# PortMaster's GUI is not a port: control.txt names the system to it
# as it is, not as ROCKNIX.
[ "$(basename "$1")" = PortMaster.sh ] && export PLORPOS_PM_GUI=1
# and our hooks give pysdl2 the SDL libraries' path (launch_hooks.txt).
if [ -n "${PLORPOS_PM_GUI:-}" ]; then
	mkdir -p "$PM/plorpos"
	cmp -s "$HERE/launch_hooks.txt" "$PM/plorpos/launch_hooks.txt" || cp "$HERE/launch_hooks.txt" "$PM/plorpos/launch_hooks.txt"
fi

cd "$(dirname "$1")" || exit 1
if [ -n "${PLORPOS_PM_GUI:-}" ]; then
	bash "$@" plorpos
else
	# What BaseOS lacks that a port takes for granted (tools/portcompat.c).
	export LD_PRELOAD=$HERE/portcompat.so${LD_PRELOAD:+:$LD_PRELOAD}
	# Older port scripts source device_info.txt again after control.txt, and
	# its probe names the system anew: the script is sourced with a RETURN
	# trap that names ROCKNIX again each time a file it sources finishes
	# (plorpos-3yv.86.18).
	bash -c 'trap "export CFW_NAME=ROCKNIX" RETURN; . "$0" "$@"' "$@"
fi
r=$?
umount "$TOOLS" 2>/dev/null || umount -l "$TOOLS"
exit $r
