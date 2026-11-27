#!/bin/sh
# plorpOS on the GKD 350H Ultra under the BasedOS fork (plorpos-gkd.94). Copied
# to the card as System/launch_frontend.sh, which BasedOS's frontend-session
# runs through /bin/sh, with every /etc/baseos-release key in the environment.
# The GKD's counterpart of sd/h700/launch_frontend.sh.
#
# A launcher that cannot start ends on BasedOS's own screen: frontend-session
# stops restarting a frontend that exits within 15 s five times running, says
# so on the panel and brings up its own Wi-Fi and SSH. So after five failed
# starts this script leaves a mark and exits, and exits at once while the mark
# is there. /run is tmpfs: the next boot tries plorpOS again.
GAVE_UP=/run/plorpos-gave-up
[ -e $GAVE_UP ] && exit 1

SD=/mnt/SDCARD
DIR=$SD/plorpOS
# Check for update's download goes in before anything in plorpOS/ starts, and
# this script starts again as the update has it (plorpos-koo, ota.sh).
[ -f "$SD/.ota/ready" ] && sh "$DIR/ota.sh" apply "$SD" && exec /bin/sh "$0" "$@"

# The ROCKNIX build's userdata folder: the same device, and a card that moves
# over keeps its settings.
export TORTOS_USERDATA=$SD/.userdata/gkd
export HOME=$TORTOS_USERDATA
LOGS=$TORTOS_USERDATA/logs
export TORTOS_DIR=$DIR USERDATA_PATH=$TORTOS_USERDATA LOGS_PATH=$LOGS
mkdir -p "$LOGS" "$SD/Bios" "$SD/Saves" "$SD/Roms" "$SD/.userdata/shared"
# Ten boots kept, newest first, as on ROCKNIX (sd/gkd/launch.sh).
LOG=$LOGS/tortos.log
i=9
while [ $i -gt 1 ]; do
	[ -f "$LOG.$((i - 1))" ] && mv -f "$LOG.$((i - 1))" "$LOG.$i"
	i=$((i - 1))
done
[ -f "$LOG" ] && mv -f "$LOG" "$LOG.1"
: > "$LOG"
echo "plorpOS on $BASEOS_MODEL, BasedOS $BASEOS_VERSION, uptime $(cut -d' ' -f1 /proc/uptime)" >> "$LOG"

# The launcher's jack watch finds the amp and detect lines in debugfs's gpio
# list, as on ROCKNIX; BasedOS doesn't mount debugfs.
grep -q ' /sys/kernel/debug ' /proc/mounts || mount -t debugfs debugfs /sys/kernel/debug

# What the player left (boot.env, which the launcher writes on every change).
WIFI=0 BLUETOOTH=0 SSH=0 TZRULE=
[ -f "$TORTOS_USERDATA/boot.env" ] && . "$TORTOS_USERDATA/boot.env"
# BasedOS has no zone files: the zone's rule, for Diatom and everything else
# started here. The launcher sets its own.
[ -n "$TZRULE" ] && export TZ="$TZRULE"

# Sound is PipeWire's, as on ROCKNIX, but nothing starts it at boot here: the
# fork's basedos-pipewire does, system-wide. Clients find it through
# XDG_RUNTIME_DIR, and ALSA's "default" reaches it through gkdb/asound.conf
# (BasedOS leaves the default on the bare card). Before anything opens sound.
export XDG_RUNTIME_DIR=/run/user/0
export ALSA_CONFIG_PATH=/usr/share/alsa/alsa.conf:$DIR/gkdb/asound.conf
basedos-pipewire start >> "$LOG" 2>&1 ||
	echo "PipeWire did not start: no sound (/run/basedos/pipewire.log)" >> "$LOG"

# Wi-Fi as the player left it: BasedOS loads the driver and leaves the rest to
# the frontend. In the background, so the shelf never waits for the radio; the
# lease only once the link is up, then the clock (no time daemon here: one
# ntpd -q, and the RTC keeps it). The launcher does the same from then on
# (src/wifi.c).
WPA_CONF=$TORTOS_USERDATA/wpa_supplicant.conf
if [ "$WIFI" = 1 ] && [ -f "$WPA_CONF" ]; then
	(
		i=0
		while [ ! -e /sys/class/net/wlan0 ] && [ $i -lt 25 ]; do sleep 1; i=$((i + 1)); done
		wpa_supplicant -B -D nl80211,wext -i wlan0 -C /run/wpa_supplicant -c "$WPA_CONF" || exit
		i=0
		until wpa_cli -p /run/wpa_supplicant -i wlan0 status 2> /dev/null | grep -q '^wpa_state=COMPLETED'; do
			[ $i -ge 20 ] && exit
			sleep 1; i=$((i + 1))
		done
		udhcpc -i wlan0 -S -t 5 -T 7 -b -q
		ntpd -n -q -p pool.ntp.org && hwclock -w -u
	) > /dev/null 2>&1 &
fi
# Stock ROCKNIX (Start held at power-on) runs plorpOS's ROCKNIX build from
# plorpOS/launch.sh, or TortOS/launch.sh on a GKD set up before 2.0, through
# the system.d drop-ins that build installed. This build ships neither, so on a
# card moved over from it they started nothing and stock stayed black, retrying
# every 2 s (plorpos-gkd.94.16). Without them stock starts EmulationStation.
grep -qs plorpos-nzi "$SD/TortOS/launch.sh" && rm -f "$SD/TortOS/launch.sh" && rmdir "$SD/TortOS" 2> /dev/null
[ -f "$DIR/launch.sh" ] && rm -f "$DIR/launch.sh" && echo "removed the ROCKNIX build's plorpOS/launch.sh" >> "$LOG"
# SSH is the frontend's too, off unless switched on in Network Settings. It
# signs in as root / plorpos, as on the Rotate (plorpos-gkd.94.22): BasedOS
# leaves root's password empty, so it is set while it still is - one changed
# with passwd stays. In /data, so BasedOS's maintenance mode asks for it too.
grep -q '^root::' /etc/shadow && echo root:plorpos | chpasswd -c sha512 >> "$LOG" 2>&1
[ "$SSH" = 1 ] && [ ! -f /run/sshd.pid ] && /etc/init.d/sshd

# Bluetooth as the player left it, off unless asked for (battery), in the
# background: the attach and bluetoothd take seconds.
[ "$BLUETOOTH" = 1 ] && basedos-bluetooth start >> "$LOG" 2>&1 &

# The boot logos are plorpOS's (plorpos-gkd.94.26): U-Boot's from power-on,
# gkdb/bootlogo.bmp.gz in place of stock's GameKiddy logo, which is kept beside
# it as .stock (the device's copy, never the card's: a fresh card must not take
# our logo for stock's, the Brick's lesson); and BasedOS's, drawn once the
# kernel has blanked the screen until the launcher's first frame, with the boot
# keys labelled (gkdb/bootlogo-keys.bmp.gz). Asks the files themselves each
# boot; in the background, so the shelf never waits on it.
LOGO=/boot/.backup/logo_mini_plus.bmp.gz KEYS_LOGO=/boot/basedos/boot-logo.bmp.gz
if [ -f "$DIR/gkdb/bootlogo.bmp.gz" ] && [ -f "$DIR/gkdb/bootlogo-keys.bmp.gz" ] && [ -f $LOGO ] &&
	! { cmp -s $LOGO "$DIR/gkdb/bootlogo.bmp.gz" && cmp -s $KEYS_LOGO "$DIR/gkdb/bootlogo-keys.bmp.gz"; }; then
	(
		mount -o remount,rw /boot || exit
		{ [ -f $LOGO.stock ] || cp $LOGO $LOGO.stock; } &&
			cp "$DIR/gkdb/bootlogo.bmp.gz" $LOGO.new && cp "$DIR/gkdb/bootlogo-keys.bmp.gz" $KEYS_LOGO.new &&
			sync && mv $LOGO.new $LOGO && mv $KEYS_LOGO.new $KEYS_LOGO && sync &&
			echo "boot logos: plorpOS's in place (stock's kept as $LOGO.stock)"
		mount -o remount,ro /boot
	) >> "$LOG" 2>&1 &
fi

# The resident emulator. A child of this script, so it lives through launcher
# restarts.
export TORTOS_DIATOM_SOCKET=/tmp/diatom.sock
start_resident() {
	pidof diatom > /dev/null && return
	[ -x "$DIR/diatom" ] || return
	rm -f "$TORTOS_DIATOM_SOCKET"
	"$DIR/diatom" --socket "$TORTOS_DIATOM_SOCKET" --cores "$DIR/cores" \
		--save "$SD/Saves" --system "$SD/Bios" >> "$LOG" 2>&1 &
}

# poweroff and reboot only ask init and return: wait here, or frontend-session
# would start this script again on the way down.
leave() {
	sync
	"$1"
	while :; do sleep 3600; done
}

rm -f /tmp/tortos_poweroff /tmp/tortos_reboot
cd "$DIR" || { echo "no $DIR" >> "$LOG"; touch $GAVE_UP; exit 1; }
FAILS=0
while :; do
	start_resident
	START=$(cut -d. -f1 /proc/uptime)
	./tortos.elf >> "$LOG" 2>&1
	[ -f /tmp/tortos_poweroff ] && leave poweroff
	[ -f /tmp/tortos_reboot ] && leave reboot   # an installed update (plorpos-koo)
	END=$(cut -d. -f1 /proc/uptime)
	if [ $((END - START)) -lt 5 ]; then
		FAILS=$((FAILS + 1))
		# An update that never got as far as its first frame, five times: back to
		# the version before it, and a restart into that (plorpos-koo).
		[ $FAILS -ge 5 ] && sh "$DIR/ota.sh" rollback "$SD" >> "$LOG" 2>&1 && leave reboot
		[ $FAILS -ge 5 ] && break
	else
		FAILS=0
	fi
	sleep 1
done

echo "launcher failed $FAILS times in a row; leaving it to BasedOS" >> "$LOG"
touch $GAVE_UP
killall diatom 2> /dev/null
exit 1
