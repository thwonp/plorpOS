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

# Where weston_pkg's wrapper (Braid, plorpos-rgds.9) looks for PortMaster:
# it sets XDG_DATA_HOME itself and then tries /opt/tools/PortMaster before
# anything of ours. /opt is ROCKNIX's /storage/.opt, ext4, so a link works
# there (the card is exFAT). Port scripts search the same list, so every
# one of them lands in the same folder.
# /opt dangles until /storage/.opt exists, and mkdir -p does not follow it.
[ -e /opt/tools/PortMaster ] || { mkdir -p "$(readlink -f /opt)/tools" && ln -sfn "$PM" /opt/tools/PortMaster; }
# Every port fullscreen (plorpos-46o.14). ROCKNIX's helper fullscreens the
# window whose app_id is the binary's name, and a port whose window is named
# otherwise ran in a small one in the middle (Hydra Castle Labyrinth, Cave
# Story Evo). Everything plorpOS shows is fullscreen anyway - the launcher,
# Diatom - so the rule is for every window, Wayland and X11 alike. Once per
# boot: sway keeps for_window rules until it restarts.
if [ ! -e /run/plorpos-fullscreen ]; then
	export SWAYSOCK=${SWAYSOCK:-$(ls "$XDG_RUNTIME_DIR"/sway-ipc.*.sock 2>/dev/null | head -1)}
	swaymsg 'for_window [app_id=".*"] fullscreen enable' >/dev/null &&
		swaymsg 'for_window [shell="xwayland"] fullscreen enable' >/dev/null &&
		: > /run/plorpos-fullscreen
fi
# Ren'Py is never resized, on the RG DS (plorpos-rgds.31), as on the Rotate
# (plorpos-3yv.86.10): PortMaster's Ren'Py draws through SDL's window
# surface, sized once to the game's aspect, and a later resize leaves it
# drawing into the old one - black. Its own size, centred, no border; after
# the rule above, so it runs later. Quoted: over IPC sway splits a command at
# its commas and ran the rest on whatever had the focus. Its own marker:
# added once per boot also on a card that already has the one above. And
# portfullscreen.so leaves its window alone (PORTFS_PASS).
if [ "$PLATFORM" = rgds ]; then
	export PORTFS_PASS=startRENPY
	if [ ! -e /run/plorpos-renpy ]; then
		export SWAYSOCK=${SWAYSOCK:-$(ls "$XDG_RUNTIME_DIR"/sway-ipc.*.sock 2>/dev/null | head -1)}
		swaymsg 'for_window [app_id="startRENPY"] "fullscreen disable, floating enable, border none, move position center"' >/dev/null &&
			: > /run/plorpos-renpy
	fi
fi

# Always: 2 KB, and ROCKNIX's BusyBox has no cmp.
# PortMaster's first run on ROCKNIX copies /storage/.config/PortMaster/
# gamecontrollerdb.txt, which EmulationStation's setup leaves there. A system
# that never ran EmulationStation (the RG DS image, plorpos-rgds.9) has none
# and PortMaster stopped with FileNotFoundError: give it ROCKNIX's own database.
if [ -d /storage/.config/PortMaster ] && [ ! -f /storage/.config/PortMaster/gamecontrollerdb.txt ] &&
   [ -f /storage/.config/SDL-GameControllerDB/gamecontrollerdb.txt ]; then
	cp /storage/.config/SDL-GameControllerDB/gamecontrollerdb.txt /storage/.config/PortMaster/
fi
cp "$HERE/control.txt" "$PM/control.txt"

cd "$(dirname "$1")" || exit 1
export LD_PRELOAD="$HERE/portfullscreen.so${LD_PRELOAD:+:$LD_PRELOAD}"
# Westonpack's ROCKNIX mode nests its Weston in sway (XSkat, plorpos-rgds.31):
# the wayland backend needs wl_egl_window_*, which Mesa's libEGL brings and
# libmali's does not - "undefined symbol: wl_egl_window_destroy", no Weston,
# and the port hung. westonwrap.sh preloads $weston_preload into Weston and
# sets it itself for every other backend.
export weston_preload=/usr/lib/libwayland-egl.so.1
exec bash "$@"
