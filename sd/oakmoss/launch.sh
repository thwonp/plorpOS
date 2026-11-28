#!/bin/sh
# plorpOS boot entry on the MagicX boards under oakMOSS: the Zero 40
# (plorpos-auv) and the Zero 28 (plorpos-08c). Called from .tmp_update/updater,
# which oakMOSS's runmagicx.sh runs, and which has checked that this card's
# build is for this board; never returns.
#
# oakMOSS has already mounted the cards (/mnt/SDCARD is the host card) and
# shown its boot screen. What it leaves to the launcher, done here: the Zero
# 40's touch panel module, the radio, the CPU governor.

export TORTOS_DIR=/mnt/SDCARD/plorpOS
SDCARD=/mnt/SDCARD
# Check for update's download goes in before anything in plorpOS/ starts, and
# this script starts again as the update has it (plorpos-koo, ota.sh).
[ -f "$SDCARD/.ota/ready" ] && sh "$TORTOS_DIR/ota.sh" apply "$SDCARD" && exec /bin/sh "$0" "$@"

BOARD=$(cat /usr/magicx/device)
export PLATFORM=$BOARD
export DEVICE=$BOARD
export SDCARD_PATH=$SDCARD
export BIOS_PATH=$SDCARD/Bios
export ROMS_PATH=$SDCARD/Roms
export SAVES_PATH=$SDCARD/Saves
export CHEATS_PATH=$SDCARD/Cheats
export SYSTEM_PATH=$TORTOS_DIR
export CORES_PATH=$TORTOS_DIR/cores
export USERDATA_PATH=$SDCARD/.userdata/$BOARD
export SHARED_USERDATA_PATH=$SDCARD/.userdata/shared
export LOGS_PATH=$USERDATA_PATH/logs
export HOME=$USERDATA_PATH
# .asoundrc as a top-level ALSA file, so a headset paired after Diatom or Muse
# started is found at their next open (the Brick's, sd/tortos/launch.sh).
export ALSA_CONFIG_PATH=/usr/share/alsa/alsa.conf:$HOME/.asoundrc
# oakMOSS's SDL2 (TrimUI's 2.26.1, with image, mixer and ttf) lives here and
# must come first (oakMOSS hardware-notes); then plorpOS/lib, Muse's FFmpeg.
export LD_LIBRARY_PATH=/usr/magicx/lib:$TORTOS_DIR/lib:$LD_LIBRARY_PATH
export PATH=/usr/magicx/bin:$PATH
# The Zero 40 image boots Dedicated OS's chain (oakMOSS make-hybrid-zero40.sh),
# whose device tree numbers the pad its own way: A 304 plus 353, B 305, X 307,
# Y 308 (ours: A 305, B 304, X 308, Y 307). The extra 353 is the tell: SDL
# then counts 21 buttons, and every index from it on is one higher. The
# launcher, Diatom, native PICO-8 and PortMaster all read MAGICX_PAD.
MAGICX_PAD=$(awk '/^N: Name="magicx-input"/ { p = 1 }
	p && /^B: KEY=/ { sub(/^B: KEY=/, ""); n = split($0, w, " ")
		x = n > 5 ? w[n - 5] : ""; while (length(x) < 16) x = "0" x
		d = index("0123456789abcdef", substr(x, 8, 1)) - 1
		print (int(d / 2) % 2 ? "dedicated" : "oakmoss"); exit }' /proc/bus/input/devices)
export MAGICX_PAD
# For SDL_GameController readers (native PICO-8): SDL's automatic mapping
# drops the four-button d-pad and MENU. Positional, as SDL names them: a is
# the bottom button. The Zero 40's one stick has its axes swapped, the
# horizontal negated; the Zero 28 adds R3 after L3 (every later button one
# up) and a second stick, both plain.
PAD="1900000012b400006666000000010000,magicx-input,a:b0,b:b1,x:b2,y:b3,back:b8,start:b9,leftshoulder:b4,rightshoulder:b5,lefttrigger:b6,righttrigger:b7,leftstick:b10,"
case $BOARD in
zero28) PAD="${PAD}rightstick:b11,dpup:b12,dpleft:b13,dpright:b14,dpdown:b15,guide:b18,leftx:a0,lefty:a1,rightx:a2,righty:a3," ;;
*) PAD="${PAD}dpup:b11,dpleft:b12,dpright:b13,dpdown:b14,guide:b17,leftx:a1~,lefty:a0," ;;
esac
[ "$MAGICX_PAD" = dedicated ] && PAD="1900000012b400006666000000010000,magicx-input,a:b1,b:b0,x:b3,y:b2,back:b8,start:b9,leftshoulder:b4,rightshoulder:b5,lefttrigger:b6,righttrigger:b7,leftstick:b10,dpup:b12,dpleft:b13,dpright:b14,dpdown:b15,guide:b18,leftx:a1~,lefty:a0,"
export SDL_GAMECONTROLLERCONFIG="${PAD}platform:Linux,"

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
BLUETOOTH=0
SSH=0
TIMEZONE=America/New_York
BOOT_ENV=$USERDATA_PATH/boot.env
[ -f "$BOOT_ENV" ] && . "$BOOT_ENV"
. "$TORTOS_DIR/radio.sh"

# The Zero 40's touch panel. oakMOSS ships the module but does not load it:
# built in, its probe stalled boots at the logo, and loaded late it registers
# after the pad (oakMOSS hardware-notes, "Touch drivers stalled the boot"). In
# the background, as spruce does; the launcher finds the node when it appears.
# The Zero 28 has no touch panel.
[ "$BOARD" = zero40 ] && { grep -q '^axs15205 ' /proc/modules ||
	( insmod "/lib/modules/$(uname -r)/axs15205.ko" 2> /dev/null ) & }

# The Zero 40's vibration motor (plorpos-auv.24): GPIO 227, high is on, as on
# the Brick - but the hybrid's device tree hands it to a fixed regulator,
# io-vibrator, that nothing uses and the kernel switches off at boot. Free the
# line and export it parked low; the launcher and Diatom find it as the
# Brick's (plat_has_rumble, MOTOR_PATH).
if [ "$BOARD" = zero40 ]; then
	R=/sys/bus/platform/drivers/reg-fixed-voltage
	[ -e $R/io-vibrator ] && echo io-vibrator > $R/unbind 2> /dev/null
	[ -d /sys/class/gpio/gpio227 ] || echo 227 > /sys/class/gpio/export 2> /dev/null
	echo out > /sys/class/gpio/gpio227/direction 2> /dev/null
	echo 0 > /sys/class/gpio/gpio227/value 2> /dev/null
fi

# USB DACs (plorpos-czp): oakMOSS's kernel has the USB audio driver as modules
# but its image ships none, so a DAC enumerates and gets no sound card. These
# are built from MagicX's own 4.9.191 tree and match only that build; insmod
# refuses any other. In the background: a DAC already plugged in is probed when
# the module arrives. If oakMOSS ever ships its own, this does nothing.
if [ ! -d /sys/module/snd_usb_audio ] && [ "$(uname -r)" = 4.9.191 ]; then
	(
		for m in snd-hwdep snd-rawmidi snd-usbmidi-lib snd-usb-audio; do
			insmod "$TORTOS_DIR/usb-audio/$m.ko" || { echo "usb audio: $m did not load"; exit; }
		done
		echo "usb audio: modules loaded"
	) >> "$LOGS_PATH/tortos.log" 2>&1 &
fi

TZNAME=$TIMEZONE
if [ -n "$TZNAME" ] && [ -f "/usr/share/zoneinfo/$TZNAME" ]; then
	ln -sf "/usr/share/zoneinfo/$TZNAME" /tmp/localtime
elif [ -n "$TZRULE" ]; then
	# oakMOSS ships only Shanghai's zone file: the zone's rule from boot.env,
	# for Diatom and everything else started here (plorpos-5tm.11).
	export TZ="$TZRULE"
fi

wifi_up=:
if [ "$WIFI" = "1" ] || [ -f "$TORTOS_DIR/.devwifi" ]; then
	wifi_up=wifi_on
fi
# Bluetooth after Wi-Fi in one job, as on the Brick: both radios are the one
# XR829 (plorpos-pky.11). The Zero 28's bt_on is a no-op.
if [ "$BLUETOOTH" = "1" ]; then
	{ $wifi_up; bt_on; } &
else
	$wifi_up &
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
# Before the resident's first PCM open, which caches ALSA's config (the Brick's
# order). No bt-alsa.sh on the Zero 28.
command -v bt_write_asoundrc > /dev/null && bt_write_asoundrc
start_resident

rm -f /tmp/tortos_poweroff /tmp/tortos_reboot

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
	[ -f /tmp/tortos_reboot ] && break     # an installed update (plorpos-koo)
	now_s; END=$NOW
	if [ $((END - START)) -lt 5 ]; then
		FAILS=$((FAILS + 1))
		# An update that never got as far as its first frame, five times: back to
		# the version before it, and a restart into that (plorpos-koo).
		[ $FAILS -ge 5 ] && sh "$TORTOS_DIR/ota.sh" rollback "$SDCARD" >> "$LOG" 2>&1 &&
			: > /tmp/tortos_reboot
		[ $FAILS -ge 5 ] && break
	else
		FAILS=0
	fi
	sleep 1
	start_resident
done

sync
[ -f /tmp/tortos_reboot ] && { reboot; sleep 10; }
poweroff
sleep 10
