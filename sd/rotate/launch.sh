#!/bin/sh
# plorpOS on the RG Rotate (plorpos-3yv): sway runs this (sway.conf), so the
# launcher and the resident Diatom are Wayland clients, as on the GKD
# (sd/gkd/launch.sh, which this follows).
#
# A launcher failing to start five times running gives the device back to
# the login prompt until the next boot.
SD=/mnt/SDCARD
DIR=$SD/TortOS
export TORTOS_USERDATA=$SD/.userdata/rotate
export HOME=$TORTOS_USERDATA
LOGS=$TORTOS_USERDATA/logs
export TORTOS_DIR=$DIR USERDATA_PATH=$TORTOS_USERDATA LOGS_PATH=$LOGS
mkdir -p "$LOGS" "$SD/Bios" "$SD/Saves" "$SD/Roms" "$SD/Music" "$SD/.userdata/shared"
# Ten boots kept, newest first, as on the Brick and the GKD.
LOG=$LOGS/tortos.log
i=9
while [ $i -gt 1 ]; do
	[ -f "$LOG.$((i - 1))" ] && mv -f "$LOG.$((i - 1))" "$LOG.$i"
	i=$((i - 1))
done
[ -f "$LOG" ] && mv -f "$LOG" "$LOG.1"
: > "$LOG"
echo "plorpOS on the RG Rotate, $(uname -r), uptime $(cut -d' ' -f1 /proc/uptime)" >> "$LOG"

export SDL_VIDEODRIVER=wayland SDL_AUDIODRIVER=pulseaudio
export TORTOS_DIATOM_SOCKET=/tmp/diatom.sock
start_resident() {
	pgrep -f "TortOS/diatom --socket" > /dev/null && return
	[ -x "$DIR/diatom" ] || return
	rm -f "$TORTOS_DIATOM_SOCKET"
	"$DIR/diatom" --socket "$TORTOS_DIATOM_SOCKET" --cores "$DIR/cores" \
		--save "$SD/Saves" --system "$SD/Bios" >> "$LOG" 2>&1 &
}

rm -f /tmp/tortos_poweroff
cd "$DIR" || exit 1
FAILS=0
while : ; do
	start_resident
	START=$(cut -d. -f1 /proc/uptime)
	./tortos.elf >> "$LOG" 2>&1
	if [ -f /tmp/tortos_poweroff ]; then
		sync
		exec systemctl poweroff
	fi
	END=$(cut -d. -f1 /proc/uptime)
	if [ $((END - START)) -lt 5 ]; then
		FAILS=$((FAILS + 1))
		[ $FAILS -ge 5 ] && break
	else
		FAILS=0
	fi
	sleep 1
done

echo "launcher failed $FAILS times; back to the login prompt" >> "$LOG"
pkill -f "TortOS/diatom --socket"
exec systemctl start getty@tty1.service
