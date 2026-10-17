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

# SDL's own PipeWire driver: its pulseaudio one, through pipewire-pulse,
# dropped 7037 frames at a game's start where this dropped none, and once
# stalled for good (2026-10-17). pactl still lists its streams (child_quiet).
export SDL_VIDEODRIVER=wayland SDL_AUDIODRIVER=pipewire
export TORTOS_DIATOM_SOCKET=/tmp/diatom.sock

# The first PCM open after PipeWire starts never runs: the sc2730's DMA
# stalls at hw_ptr 360 with the stream "RUNNING", every time (2026-10-17,
# 4 of 4 session starts), and every later open works (7 of 7). A stream
# that is open then - Diatom's - stays silent all session. So that first
# open is spent here on 0.1 s of silence, the sink is suspended to close
# it, and a second one checks the codec now plays. ~2.5 s, before Diatom
# and in the background: the shelf does not wait.
warm_codec() {
	silence() { head -c 19200 /dev/zero | timeout 2 pw-cat --playback --raw \
		--format s16 --rate 48000 --channels 2 - 2> /dev/null; }
	silence
	pactl suspend-sink @DEFAULT_SINK@ 1
	pactl suspend-sink @DEFAULT_SINK@ 0
	silence || echo "codec: still stalled after the warm-up; no sound this session"
}
# Bonded headsets connected at the session's start (plorpos-3yv.18.4), as
# the Brick's bt_reconnect: one already out of its case when the Rotate starts
# does not reach for it itself. Bonded = trusted with a link key, bt.c's
# rule. Stops at the first link; a few rounds, as WirePlumber registers its
# A2DP endpoints a moment after it starts. After warm_codec: a headset there
# first would be the default sink its suspend went to.
bt_reconnect() {
	for round in 1 2 3 4; do
		bonds=
		for d in /var/lib/bluetooth/*/*:*; do
			grep -q '^Trusted=true' "$d/info" 2> /dev/null || continue
			grep -q '^\[LinkKey\]' "$d/info" 2> /dev/null || continue
			bonds="$bonds ${d##*/}"
		done
		[ -n "$bonds" ] || return 0
		for mac in $bonds; do
			timeout 5 bluetoothctl info "$mac" 2> /dev/null | grep -q 'Connected: yes' && return 0
		done
		for mac in $bonds; do
			timeout 10 bluetoothctl connect "$mac" > /dev/null 2>&1
			# By info, never by connect's return (the Brick's rule).
			sleep 1
			if timeout 5 bluetoothctl info "$mac" 2> /dev/null | grep -q 'Connected: yes'; then
				echo "bluetooth: reconnected $mac (round $round)"
				return 0
			fi
		done
		sleep 5
	done
	echo "bluetooth: no bonded headset answered"
}
start_resident() {
	pgrep -f "TortOS/diatom --socket" > /dev/null && return
	[ -x "$DIR/diatom" ] || return
	rm -f "$TORTOS_DIATOM_SOCKET"
	# Not before the speaker's sink exists: session.sh starts PipeWire beside
	# sway, and a Diatom that got there first ran silent all session
	# ("Could not connect PulseAudio stream", 2026-10-17). In the background,
	# bounded at 5 s, so the shelf never waits for it; the launcher gives a
	# starting Diatom 8 s at a game's launch.
	(
		i=0
		until pw-cli ls Node 2> /dev/null | grep -q 'Speaker__sink'; do
			[ $i -ge 50 ] && { echo "no speaker sink after 5 s; Diatom starts anyway"; break; }
			sleep 0.1
			i=$((i + 1))
		done
		warm_codec
		bt_reconnect &
		exec "$DIR/diatom" --socket "$TORTOS_DIATOM_SOCKET" --cores "$DIR/cores" \
			--save "$SD/Saves" --system "$SD/Bios"
	) >> "$LOG" 2>&1 &
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
