#!/bin/sh
# Runs PortMaster or a port on the GKD 350H Ultra under the BasedOS fork
# (plorpos-gkd.94.15): pm-run.sh SCRIPT [ARGS], or removes one: pm-run.sh
# --uninstall ZIP. The H700's runner (sd/h700/portmaster/pm-run.sh): BasedOS
# has bash and SDL2 (64- and 32-bit) but no python3, which pmtools.squashfs
# carries, and its root is read-only.
#
# Unofficial PortMaster support: if something PortMaster related does not
# work, do not bug the PortMaster devs.

# Each run in a mount namespace of its own: RetroArch ports call
# /usr/bin/retroarch (raloc), which a read-only root cannot hold, so Diatom's
# stand-in is laid over /usr/bin for this run alone - and whatever the run
# mounts (the tools, a port's runtimes) goes when its last process does, even
# when the launcher has to KILL it. BusyBox's unshare execs in place: the
# launcher's process group and its freeze reach the port as before.
if [ -z "$PLORPOS_PM_NS" ]; then
	export PLORPOS_PM_NS=1
	exec unshare -m --propagation private "$0" "$@"
fi

HERE=$(dirname "$0")
CARD=/mnt/SDCARD
PMDATA=$CARD/.userdata/shared/portmaster
PM=$PMDATA/PortMaster
TOOLS=/run/plorpos-pmtools
RABIN=/run/plorpos-pmbin

export XDG_DATA_HOME=$PMDATA
export HM_TOOLS_DIR=$PMDATA HM_PORTS_DIR=$CARD/Roms/Ports HM_SCRIPTS_DIR=$CARD/Roms/Ports
# Ports mount runtimes and keep settings in $HOME: theirs, not the launcher's.
export HOME=$PMDATA/home
mkdir -p "$HOME"

# Port scripts tee their log through bash's process substitution, which wants
# /dev/fd (BasedOS links it; kept in case a build does not).
[ -e /dev/fd ] || ln -s /proc/self/fd /dev/fd

mkdir -p "$TOOLS" "$RABIN"
mount -t squashfs -o loop,ro "$HERE/pmtools.squashfs" "$TOOLS" || exit 1
cp "$HERE/retroarch" "$RABIN/retroarch" && chmod +x "$RABIN/retroarch" &&
	mount -t overlay overlay -o "lowerdir=$RABIN:/usr/bin" /usr/bin
export PATH=$TOOLS/bin:$TOOLS/python/bin:$PATH
# The system's SDL2 after the tools' (SDL2_mixer for PortMaster's GUI); the
# GUI's pysdl2 looks only on this path (launch_hooks.txt).
export LD_LIBRARY_PATH=$TOOLS/lib:/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export SSL_CERT_FILE=$CARD/plorpOS/cacert.pem
# dotnet (PortMaster's GameMaker patcher, dotnet ports) aborts without ICU,
# which BasedOS does not have: the invariant culture instead.
export DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1
# PipeWire's socket where it is, whatever XDG_RUNTIME_DIR a port's wrapper
# sets for itself (Westonpack's is its own folder): those win over it.
export PIPEWIRE_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/run/user/0}
export PULSE_RUNTIME_PATH=${XDG_RUNTIME_DIR:-/run/user/0}/pulse

if [ "$1" = --uninstall ]; then
	cd "$PM" || exit 1
	exec python3 harbourmaster --quiet --no-check uninstall "$2"
fi

# Unpacked from the payload's zip the first time - beside, then moved in, so
# a run cut short leaves no half a PortMaster.
if [ ! -f "$PM/PortMaster.sh" ] && [ -f "$HERE/PortMaster.zip" ]; then
	rm -rf "$PMDATA/.install"
	python3 -m zipfile -e "$HERE/PortMaster.zip" "$PMDATA/.install" &&
		rm -rf "$PM" && mv "$PMDATA/.install/PortMaster" "$PM" && rm -rf "$PMDATA/.install" ||
		exit 1
fi

# Westonpack's wrapper looks for PortMaster in fixed places only, the
# ROCKNIX build's /opt/tools link among them, which a read-only root cannot
# hold: $HOME's is one of them too, bound for this run. And XWayland, which
# it starts, wants the X11 socket folder every other system has.
mkdir -p "$HOME/.local/share/PortMaster" /tmp/.X11-unix && chmod 1777 /tmp/.X11-unix
mount --bind "$PM" "$HOME/.local/share/PortMaster"

for f in control.txt libgl_ROCKNIX.txt; do
	cmp -s "$HERE/$f" "$PM/$f" || cp "$HERE/$f" "$PM/$f"
done

# PortMaster's GUI is not a port: control.txt names the system to it as it
# is, not as ROCKNIX, and our hooks give pysdl2 the libraries' path.
[ "$(basename "$1")" = PortMaster.sh ] && export PLORPOS_PM_GUI=1
if [ -n "${PLORPOS_PM_GUI:-}" ]; then
	mkdir -p "$PM/plorpos"
	cmp -s "$HERE/launch_hooks.txt" "$PM/plorpos/launch_hooks.txt" || cp "$HERE/launch_hooks.txt" "$PM/plorpos/launch_hooks.txt"
fi

cd "$(dirname "$1")" || exit 1
if [ -n "${PLORPOS_PM_GUI:-}" ]; then
	exec bash "$@" plorpos
fi
# What BasedOS lacks that a port takes for granted (tools/portcompat.c), and
# a fixed-size game scaled to the panel, which KMSDRM will not do (tools/portscale.c).
export LD_PRELOAD=$HERE/portcompat.so:$HERE/portscale.so${LD_PRELOAD:+:$LD_PRELOAD}
# Older port scripts source device_info.txt again after control.txt, and its
# probe names the system anew: a RETURN trap names ROCKNIX again each time a
# file the script sources finishes (plorpos-3yv.86.18).
exec bash -c 'trap "export CFW_NAME=ROCKNIX" RETURN; . "$0" "$@"' "$@"
