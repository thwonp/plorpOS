#!/bin/sh
# plorpOS's session on the Miyoo Mini Flip (plorpos-f1p): the resident diatom
# and the launcher in a loop, as sd/h700/launch_frontend.sh runs them on the
# H700. Started by the card's .tmp_update/updater once plorpos-f1p.12 installs
# it; until then by hand, with Allium stopped.
#
# Wi-Fi comes up below, from boot.env's WIFI.
# Not here yet: sleep and power-off (plorpos-f1p.7).
SD=/mnt/SDCARD
DIR=$SD/TortOS
export TORTOS_USERDATA=$SD/.userdata/miyooflip
export HOME=$TORTOS_USERDATA
# Ours first (SDL2, FFmpeg), then the firmware's (libmi_* for MI_AO).
export LD_LIBRARY_PATH=$DIR/lib:/config/lib:/customer/lib:/lib
LOGS=$TORTOS_USERDATA/logs
LOG=$LOGS/tortos.log
export TORTOS_DIR=$DIR USERDATA_PATH=$TORTOS_USERDATA LOGS_PATH=$LOGS
mkdir -p "$LOGS" "$SD/Bios" "$SD/Saves" "$SD/Roms" "$SD/.userdata/shared"
[ -f "$LOG" ] && mv -f "$LOG" "$LOG.1"
echo "plorpOS on the Miyoo Mini Flip, uptime $(cut -d' ' -f1 /proc/uptime)" > "$LOG"

# Full clock: ondemand held 1.0 GHz while mGBA fell behind in play
# (2026-11-04). Until the Flip has a real power policy (plorpos-f1p.7).
echo performance > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2> /dev/null

# Wi-Fi as the player left it (boot.env, which the launcher writes on every
# change; plorpos-f1p.21): join the saved networks in the background, so the
# shelf never waits for the radio. Off, the radio is not even powered. The
# launcher's own wifi_up (src/wifi.c) does the same later from the menu.
# The commands are the boot hook's old ones, unchanged.
WIFI=0
[ -f "$TORTOS_USERDATA/boot.env" ] && . "$TORTOS_USERDATA/boot.env"
WPA_CONF=/appconfigs/wpa_supplicant.conf
if [ "$WIFI" = 1 ] && [ -f "$WPA_CONF" ]; then
	echo "wifi: up at boot (setting on)" >> "$LOG"
	(
		grep -q 8188fu /proc/modules || insmod "$SD/.tmp_update/8188fu.ko"
		/customer/app/axp_test wifion
		sleep 2
		ifconfig wlan0 up
		/customer/app/wpa_supplicant -B -D nl80211 -iwlan0 -c "$WPA_CONF"
		udhcpc -i wlan0 -s /etc/init.d/udhcpc.script
	) > /dev/null 2>&1 &
else
	echo "wifi: off at boot (WIFI=$WIFI)" >> "$LOG"
fi

export TORTOS_DIATOM_SOCKET=/tmp/diatom.sock
start_resident() {
	pidof diatom > /dev/null && return
	[ -x "$DIR/diatom" ] || return
	rm -f "$TORTOS_DIATOM_SOCKET"
	"$DIR/diatom" --socket "$TORTOS_DIATOM_SOCKET" --cores "$DIR/cores" \
		--save "$SD/Saves" --system "$SD/Bios" >> "$LOG" 2>&1 &
}
rm -f /tmp/tortos_poweroff
cd "$DIR" || exit 1
FAILS=0
while :; do
	start_resident
	START=$(cut -d. -f1 /proc/uptime)
	./tortos.elf >> "$LOG" 2>&1
	RC=$?
	# A signal shows as 128 + its number (139 = SIGSEGV); the kernel here
	# logs no user-space faults, so this line is the only record.
	echo "launcher exited $RC" >> "$LOG"
	# The launcher's power-off (a hold, Auto Off, or sleep with no suspend to
	# go to) leaves this flag and exits 0. Without this check the loop just
	# restarted it, and Games Auto-Resume put the game back: the "Quit loop"
	# (plorpos-f1p.20). The same handshake sd/h700/launch_frontend.sh has.
	if [ -f /tmp/tortos_poweroff ]; then
		echo "power off" >> "$LOG"
		killall diatom 2> /dev/null
		sync
		exec poweroff
	fi
	END=$(cut -d. -f1 /proc/uptime)
	if [ $((END - START)) -lt 5 ]; then
		FAILS=$((FAILS + 1))
		if [ $FAILS -ge 5 ]; then
			echo "launcher failed $FAILS times in a row; stopping" >> "$LOG"
			exit 1
		fi
	else
		FAILS=0
	fi
done
