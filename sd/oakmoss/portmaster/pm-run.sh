#!/bin/sh
# Unofficial PortMaster on the MagicX boards (Zero 40 plorpos-auv.7, Zero 28
# plorpos-08c.10): the Brick's runner (sd/brick/portmaster/pm-run.sh) on
# oakMOSS - same A133P, GE8300 and firmware SDL, here in /usr/magicx/lib. pm-run.sh SCRIPT [ARGS], or
# pm-run.sh --uninstall ZIP.
#
# If something PortMaster related does not work, do not bug the PortMaster devs.
# SPDX-License-Identifier: MIT
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
BOARD=$(cat /usr/magicx/device 2> /dev/null)
CARD=/mnt/SDCARD
PMDATA=$CARD/.userdata/shared/portmaster
PM=$PMDATA/PortMaster
# oakMOSS has no /run: /tmp is its tmpfs.
TOOLS=/tmp/plorpos-pmtools
PORTS=/tmp/plorpos-roms/ports
LIB32=/tmp/plorpos-lib32
export XDG_DATA_HOME=$PMDATA
export HM_TOOLS_DIR=$PMDATA HM_PORTS_DIR=$PORTS HM_SCRIPTS_DIR=$PORTS
export HOME=$PMDATA/home
mkdir -p "$HOME" "$TOOLS"
# What a port mounted and left: only ours go (oakMOSS's root is itself a
# squashfs, /rom) - every loop mount of a file in PortMaster's folder or of
# our tools, wherever a script put it (Westonpack's and Godot's in /tmp), and
# every mount inside PortMaster's folder. At the start for a port the launcher
# had to kill, and at the end: the Zero 40 has eight loop devices, and
# GMToolkit's patcher left its .NET mounted.
unmount_ours() {
    grep -q " /bin/bash " /proc/mounts && umount /bin/bash
    while read -r dev mnt rest; do
        case $dev in /dev/loop*)
            case $(cat "/sys/block/${dev#/dev/}/loop/backing_file" 2>/dev/null) in
            "$PMDATA"/*|"$HERE"/*) echo "$mnt" ;;
            esac ;;
        esac
        case $mnt in "$PMDATA"/*|"$PORTS") echo "$mnt" ;; esac
    done < /proc/mounts | sed 's/\\040/ /g' | sort -ru |
        while read -r m; do umount "$m" 2>/dev/null || umount -l "$m"; done
}
unmount_ours
[ -e /dev/fd ] || ln -s /proc/self/fd /dev/fd
mount -t squashfs -o loop,ro "$HERE/pmtools.squashfs" "$TOOLS" || exit 1
# The card is FAT, like the Brick's: port scripts look in a lowercase /ports.
# The alias is on tmpfs; the files stay in the user's Roms/Ports folder.
mkdir -p "$PORTS" "$CARD/Roms/Ports"
umount "$PORTS" 2>/dev/null || true
mount --bind "$CARD/Roms/Ports" "$PORTS" || exit 1
# oakMOSS's /bin/bash is 4.3.42 (2015). Port scripts and the helpers they
# start by "#!/bin/bash" expect a current one: GMToolkit's patcher hung in
# bash 4.3 on its first line (exec > >(tee ...)) and ran in 5.2. Ours stands
# in for the run only.
mount --bind "$TOOLS/bash" /bin/bash || exit 1
trap unmount_ours EXIT
export PATH=$TOOLS/bin:$TOOLS/python/bin:$PATH
# 32-bit ports (oakMOSS is 64-bit only; the GE8300 has no 32-bit driver):
# lib32.squashfs is a Debian armhf userland with Mesa's software GL, and its
# fbpresent32.so copies each frame to the panel (mk/build-lib32-zero40.sh).
# Linked in as a multiarch system has it - the loader, its own library
# folder, and /usr/lib32 for ports written for ROCKNIX - and the shim named
# in the one preload file only this loader reads.
mkdir -p "$LIB32"
if mount -t squashfs -o loop,ro "$HERE/lib32.squashfs" "$LIB32"; then
    for l in /lib/ld-linux-armhf.so.3:lib/arm-linux-gnueabihf/ld-linux-armhf.so.3 \
             /usr/lib/arm-linux-gnueabihf:lib/arm-linux-gnueabihf /usr/lib32:lib/arm-linux-gnueabihf; do
        [ "$(readlink "${l%%:*}")" = "$LIB32/${l#*:}" ] || ln -sfn "$LIB32/${l#*:}" "${l%%:*}"
    done
    echo "$LIB32/lib/arm-linux-gnueabihf/fbpresent32.so" > /tmp/plorpos32.pre
fi
export LD_LIBRARY_PATH=$TOOLS/lib:/usr/magicx/lib:$CARD/plorpOS/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export SSL_CERT_FILE=$CARD/plorpOS/cacert.pem
export SDL_AUDIODRIVER=alsa
# oakMOSS sets none, and Weston (Westonpack's ports) will not start without it.
# Emptied first: a Weston port's sockets outlive it, and a later Qt port took
# one for a running compositor (QT_QPA_PLATFORM=wayland: no plugin, no game).
export XDG_RUNTIME_DIR=/tmp/plorpos-xdg
rm -rf /tmp/plorpos-xdg
mkdir -p -m 0700 "$XDG_RUNTIME_DIR"
# Qt's EGLFS takes /dev/dri/card0 for a display and goes for KMS, but on the
# Zero 40 that node is the GPU's alone: plain EGL on the framebuffer instead.
export QT_QPA_EGLFS_INTEGRATION=none
# XWayland, which Westonpack starts, wants the X11 socket folder every other
# system has.
mkdir -p /tmp/.X11-unix && chmod 1777 /tmp/.X11-unix
# oakMOSS has no ICU, and .NET aborts without it (GMToolkit's patcher).
export DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1
# launch.sh's pad map is the launcher's, by position; PortMaster and its ports
# take control.txt's, by label - SDL would let the inherited one win.
unset SDL_GAMECONTROLLERCONFIG
# The Zero 40's stick rests about 2.5 % off centre (ABS_Y -819 to -901), and
# gptokeyb2 with a port's small dead zone sent Right over and over. A dead
# zone on the axes (the kernel's "flat"), which SDL honours only when asked
# to; the launcher and Diatom read the pad raw and are unaffected. The Zero
# 28's sticks rest at exactly 0 (its driver filters them).
[ "$BOARD" = zero40 ] && python3 - << 'PY'
import fcntl, os, struct
try:
    fd = os.open('/dev/input/event2', os.O_RDWR)
    for code in (0, 1):
        b = bytearray(24); fcntl.ioctl(fd, 0x80184540 + code, b)
        v = list(struct.unpack('6i', b)); v[4] = 3000
        fcntl.ioctl(fd, 0x401845c0 + code, struct.pack('6i', *v))
except OSError:
    pass
PY
export SDL_LINUX_JOYSTICK_DEADZONES=1
if [ ! -f "$PM/PortMaster.sh" ] && [ -f "$HERE/PortMaster.zip" ]; then
    rm -rf "$PMDATA/.install"
    python3 -m zipfile -e "$HERE/PortMaster.zip" "$PMDATA/.install" || exit 1
    rm -rf "$PM"
    mv "$PMDATA/.install/PortMaster" "$PM" || exit 1
    rm -rf "$PMDATA/.install"
fi
cmp -s "$HERE/control.txt" "$PM/control.txt" || cp "$HERE/control.txt" "$PM/control.txt" || exit 1
if [ "${1:-}" = --uninstall ]; then
    cd "$PM" || exit 1
    python3 harbourmaster --quiet --no-check uninstall "$2"
    exit $?
fi
[ -e /usr/bin/retroarch ] || [ -L /usr/bin/retroarch ] || ln -s "$HERE/retroarch" /usr/bin/retroarch
# oakMOSS keeps its runtime files in /var/run, a link to /tmp/run, and has no
# /run: the libudev a port carries looks for udev's data there, and Qt's
# libinput skipped every device as unconfigured (Dust Racing took no input).
[ -e /run ] || ln -s /tmp/run /run
# oakMOSS tags only Powkiddy pads for udev (its 61-oakmoss-pads.rules), and
# has no input_id rules: Godot found no pad, and libinput (Westonpack's
# Weston) skipped gptokeyb's keyboard, so a Weston port took no input. The
# pad is a joystick by name - input_id calls it a mouse - and every virtual
# device a port makes (gptokeyb's keyboard, a fake pad) is classified.
RULES=/etc/udev/rules.d/61-plorpos-$BOARD-pad.rules
if ! cmp -s "$HERE/udev.rules" "$RULES"; then
    cp "$HERE/udev.rules" "$RULES" && udevadm control --reload &&
        udevadm trigger --action=change --subsystem-match=input
fi
# Westonpack's wrapper looks for PortMaster in fixed places only, $HOME's
# among them: bound there for the run (as on the GKD), or it sources no
# control.txt and keeps the ports' ROCKNIX name.
mkdir -p "$HOME/.local/share/PortMaster"
mount --bind "$PM" "$HOME/.local/share/PortMaster" || exit 1
if [ "$(basename "$1")" = PortMaster.sh ]; then
    export PLORPOS_PM_GUI=1
    mkdir -p "$PM/plorpos"
    cmp -s "$HERE/launch_hooks.txt" "$PM/plorpos/launch_hooks.txt" ||
        cp "$HERE/launch_hooks.txt" "$PM/plorpos/launch_hooks.txt" || exit 1
    cd "$(dirname "$1")" || exit 1
    bash "$@" plorpos
else
    cd "$(dirname "$1")" || exit 1
    # Older scripts source device_info.txt again after control.txt: keep the
    # name and the display control.txt gave (plorpos-3yv.86.18).
    case $BOARD in
    zero28) export PLORPOS_PM_DISPLAY="DISPLAY_WIDTH=640 DISPLAY_HEIGHT=480 DISPLAY_ORIENTATION=0 ASPECT_X=4 ASPECT_Y=3" ;;
    *) export PLORPOS_PM_DISPLAY="DISPLAY_WIDTH=480 DISPLAY_HEIGHT=800 DISPLAY_ORIENTATION=0 ASPECT_X=3 ASPECT_Y=5" ;;
    esac
    bash -c 'trap "export CFW_NAME=ROCKNIX $PLORPOS_PM_DISPLAY" RETURN; . "$0" "$@"' "$@"
fi
