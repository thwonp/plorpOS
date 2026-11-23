#!/bin/sh
# plorpOS boot entry on the MagicX Zero 40 (plorpos-auv). Called from
# .tmp_update/updater, which oakMOSS's runmagicx.sh runs; never returns.
#
# oakMOSS has already mounted the cards (/mnt/SDCARD is the host card) and
# shown its boot screen. What it leaves to the launcher, done here: the touch
# panel's module, the radio, the CPU governor.

export TORTOS_DIR=/mnt/SDCARD/plorpOS
SDCARD=/mnt/SDCARD

export PLATFORM=zero40
export DEVICE=zero40
export SDCARD_PATH=$SDCARD
export BIOS_PATH=$SDCARD/Bios
export ROMS_PATH=$SDCARD/Roms
export SAVES_PATH=$SDCARD/Saves
export CHEATS_PATH=$SDCARD/Cheats
export SYSTEM_PATH=$TORTOS_DIR
export CORES_PATH=$TORTOS_DIR/cores
export USERDATA_PATH=$SDCARD/.userdata/zero40
export SHARED_USERDATA_PATH=$SDCARD/.userdata/shared
export LOGS_PATH=$USERDATA_PATH/logs
export HOME=$USERDATA_PATH
# oakMOSS's SDL2 (TrimUI's 2.26.1, with image, mixer and ttf) lives here and
# must come first (oakMOSS hardware-notes); then plorpOS/lib, Muse's FFmpeg.
export LD_LIBRARY_PATH=/usr/magicx/lib:$TORTOS_DIR/lib:$LD_LIBRARY_PATH
export PATH=/usr/magicx/bin:$PATH
# For SDL_GameController readers (native PICO-8): SDL's automatic mapping
# drops the four-button d-pad and MENU. Positional, as SDL names them: a is
# the bottom button. The stick's axes are swapped, the horizontal negated.
export SDL_GAMECONTROLLERCONFIG="1900000012b400006666000000010000,magicx-input,a:b0,b:b1,x:b2,y:b3,back:b8,start:b9,guide:b17,leftshoulder:b4,rightshoulder:b5,lefttrigger:b6,righttrigger:b7,leftstick:b10,dpup:b11,dpleft:b12,dpright:b13,dpdown:b14,leftx:a1~,lefty:a0,platform:Linux,"

# USB Debugging (plorpos-auv.12, System Settings): the OTG port reads its ID
# pin low on a computer's cable too and would make itself a host, so with the
# setting on it is forced to device mode for adb (src/platform_brick.c).
if [ -f "$USERDATA_PATH/adb" ]; then
	( echo usb_device > /sys/devices/platform/soc/usbc0/otg_role &&
	  : > /tmp/plorpos-usb-device ) 2> /dev/null &
fi

mkdir -p "$BIOS_PATH" "$ROMS_PATH" "$SAVES_PATH" "$USERDATA_PATH" "$LOGS_PATH" \
         "$SHARED_USERDATA_PATH"

# What the launcher saved for the next boot (db_write_boot_env).
WIFI=0
SSH=0
TIMEZONE=America/New_York
BOOT_ENV=$USERDATA_PATH/boot.env
[ -f "$BOOT_ENV" ] && . "$BOOT_ENV"
. "$TORTOS_DIR/radio.sh"

# The touch panel. oakMOSS ships the module but does not load it: built in,
# its probe stalled boots at the logo, and loaded late it registers after the
# pad (oakMOSS hardware-notes, "Touch drivers stalled the boot"). In the
# background, as spruce does; the launcher finds the node when it appears.
grep -q '^axs15205 ' /proc/modules ||
	( insmod "/lib/modules/$(uname -r)/axs15205.ko" 2> /dev/null ) &

TZNAME=$TIMEZONE
if [ -n "$TZNAME" ] && [ -f "/usr/share/zoneinfo/$TZNAME" ]; then
	ln -sf "/usr/share/zoneinfo/$TZNAME" /tmp/localtime
elif [ -n "$TZRULE" ]; then
	# oakMOSS ships only Shanghai's zone file: the zone's rule from boot.env,
	# for Diatom and everything else started here (plorpos-5tm.11).
	export TZ="$TZRULE"
fi

if [ "$WIFI" = "1" ] || [ -f "$TORTOS_DIR/.devwifi" ]; then
	wifi_on &
fi

# oakMOSS boots on performance; interactive as on the Brick. Diatom pins it
# for a vsync-locked game and gives it back.
echo interactive > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2> /dev/null

LOG=$LOGS_PATH/tortos.log
[ -f "$LOG" ] && mv -f "$LOG" "$LOG.prev"
: > "$LOG"
(
	i=9
	while [ $i -gt 1 ]; do
		[ -f "$LOG.$((i - 1))" ] && mv -f "$LOG.$((i - 1))" "$LOG.$i"
		i=$((i - 1))
	done
	[ -f "$LOG.prev" ] && mv -f "$LOG.prev" "$LOG.1"
) &

[ -x "$TORTOS_DIR/ssh.sh" ] && [ "$SSH" = "1" ] && sh "$TORTOS_DIR/ssh.sh" start &

export TORTOS_DIATOM_SOCKET=/tmp/diatom.sock
start_resident() {
	pgrep -f "plorpOS/diatom --socket" > /dev/null && return
	rm -f "$TORTOS_DIATOM_SOCKET"
	"$TORTOS_DIR/diatom" --socket "$TORTOS_DIATOM_SOCKET" \
		--cores "$CORES_PATH" \
		--save "$SDCARD/Saves" --system "$SDCARD/Bios" >> "$LOG" 2>&1 &
}
start_resident

rm -f /tmp/tortos_poweroff

# Restart loop: only ever exits for a power-off. A launcher that dies within
# 5 s five times running is not going to start on the sixth.
now_s() {
	local up rest
	read up rest < /proc/uptime
	NOW=${up%%.*}
}
cd "$TORTOS_DIR"
FAILS=0
while : ; do
	now_s; START=$NOW
	./tortos.elf >> "$LOG" 2>&1
	[ -f /tmp/tortos_poweroff ] && break
	now_s; END=$NOW
	if [ $((END - START)) -lt 5 ]; then
		FAILS=$((FAILS + 1))
		[ $FAILS -ge 5 ] && break
	else
		FAILS=0
	fi
	sleep 1
	start_resident
done

sync
poweroff
sleep 10
