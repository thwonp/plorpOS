#!/bin/sh
# Unofficial PortMaster on the plain Brick (plorpos-reo.17).
# SPDX-License-Identifier: MIT
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
CARD=/mnt/SDCARD
PMDATA=$CARD/.userdata/shared/portmaster
PM=$PMDATA/PortMaster
TOOLS=/run/plorpos-pmtools
PORTS=/run/plorpos-roms/ports
export XDG_DATA_HOME=$PMDATA
export HM_TOOLS_DIR=$PMDATA HM_PORTS_DIR=$PORTS HM_SCRIPTS_DIR=$PORTS
export HOME=$PMDATA/home
mkdir -p "$HOME" "$TOOLS"
# Clean only our stale port mounts, never the firmware's mounts.
grep -iE " squashfs | $PMDATA/" /proc/mounts | cut -d' ' -f2 | sed 's/\\040/ /g' |
    grep -iE "^($PMDATA/|$TOOLS$)" | sort -r |
    while read -r m; do umount "$m" 2>/dev/null || umount -l "$m"; done
[ -e /dev/fd ] || ln -s /proc/self/fd /dev/fd
mount -t squashfs -o loop,ro "$HERE/pmtools.squashfs" "$TOOLS" || exit 1
trap 'umount "$PORTS" 2>/dev/null || true; umount "$TOOLS" 2>/dev/null || umount -l "$TOOLS"' EXIT
# Stock FAT lookups can reject the lowercase /ports used by port scripts.
# The alias is on tmpfs; the files stay in the user's Roms/Ports folder.
mkdir -p "$PORTS" "$CARD/Roms/Ports"
umount "$PORTS" 2>/dev/null || true
mount --bind "$CARD/Roms/Ports" "$PORTS" || exit 1
trap 'umount "$PORTS" 2>/dev/null || umount -l "$PORTS"; umount "$TOOLS" 2>/dev/null || umount -l "$TOOLS"' EXIT
export PATH=$TOOLS/bin:$TOOLS/python/bin:$PATH
export LD_LIBRARY_PATH=$TOOLS/lib:/usr/trimui/lib:$CARD/TortOS/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export SSL_CERT_FILE=$CARD/TortOS/cacert.pem
export SDL_AUDIODRIVER=alsa
if [ ! -f "$PM/PortMaster.sh" ] && [ -f "$HERE/PortMaster.zip" ]; then
    rm -rf "$PMDATA/.install"
    python3 -m zipfile -e "$HERE/PortMaster.zip" "$PMDATA/.install" || exit 1
    rm -rf "$PM"
    mv "$PMDATA/.install/PortMaster" "$PM" || exit 1
    rm -rf "$PMDATA/.install"
fi
cp "$HERE/control.txt" "$PM/control.txt" || exit 1
if [ "${1:-}" = --uninstall ]; then
    cd "$PM" || exit 1
    python3 harbourmaster --quiet --no-check uninstall "$2"
    exit $?
fi
[ -e /usr/bin/retroarch ] || [ -L /usr/bin/retroarch ] || ln -s "$HERE/retroarch" /usr/bin/retroarch
if [ "$(basename "$1")" = PortMaster.sh ]; then
    export PLORPOS_PM_GUI=1
    mkdir -p "$PM/plorpos"
    cp "$HERE/launch_hooks.txt" "$PM/plorpos/launch_hooks.txt" || exit 1
    cd "$(dirname "$1")" || exit 1
    bash "$@" plorpos
else
    cd "$(dirname "$1")" || exit 1
    bash -c 'trap "export CFW_NAME=ROCKNIX" RETURN; . "$0" "$@"' "$@"
fi
