#!/bin/sh
# TortOS boot entry. Called from .tmp_update/tg3040.sh; never returns.
#
# The order of things in here is the boot time: the launcher's first frame
# waits on everything above where it starts. Boot timeline and what was cut:
# plorpos-act (2026-11-27).

# Exported for radio.sh: plat_sleep() runs it in a shell of its own, which
# sees only the launcher's environment (TortOS-1jx).
export TORTOS_DIR=/mnt/SDCARD/plorpOS
SDCARD=/mnt/SDCARD
# Check for update's download goes in before anything in plorpOS/ starts, and
# this script starts again as the update has it (plorpos-koo, ota.sh).
[ -f "$SDCARD/.ota/ready" ] && sh "$TORTOS_DIR/ota.sh" apply "$SDCARD" && exec /bin/sh "$0" "$@"

export PLATFORM=tg3040
export DEVICE=brick
export SDCARD_PATH=$SDCARD
export BIOS_PATH=$SDCARD/Bios
export ROMS_PATH=$SDCARD/Roms
export SAVES_PATH=$SDCARD/Saves
export CHEATS_PATH=$SDCARD/Cheats
export SYSTEM_PATH=$TORTOS_DIR
export CORES_PATH=$TORTOS_DIR/cores
export USERDATA_PATH=$SDCARD/.userdata/tg3040
export SHARED_USERDATA_PATH=$SDCARD/.userdata/shared
export LOGS_PATH=$USERDATA_PATH/logs
export HOME=$USERDATA_PATH
# .asoundrc named as a top-level ALSA config file, and not only reached through
# alsa.conf's @hooks load. alsa-lib 1.1.8 re-reads its config at the next open
# when a file in this list changes, and it never checks files a hook loaded -
# so a headset paired after Diatom or Muse started, whose PCM bt_write_asoundrc
# has just added, was `Unknown PCM` to both until they restarted. Measured
# 2026-09-25 with a probe that opens once, waits, and opens a PCM added in
# between: not found without this, found with it, and a missing or empty
# .asoundrc at start is fine either way.
export ALSA_CONFIG_PATH=/usr/share/alsa/alsa.conf:$HOME/.asoundrc
export LD_LIBRARY_PATH=/usr/trimui/lib:$LD_LIBRARY_PATH
export PATH=/usr/trimui/bin:$PATH

mkdir -p "$BIOS_PATH" "$ROMS_PATH" "$SAVES_PATH" "$USERDATA_PATH" "$LOGS_PATH" \
         "$SHARED_USERDATA_PATH"

# Settings live in a database now, and this script cannot read one: there is no
# sqlite3 binary on the device. So the launcher exports the five values needed
# before it exists, and this sources them. One read, no forks - it replaces six
# seds across four files, so the boot path got shorter rather than longer.
#
# The defaults below are for the very first boot of a fresh card, before the
# launcher has ever run. After that boot.env is rewritten on every start and
# whenever one of these changes.
#
# Sourced rather than parsed because parsing costs a fork per value. That trusts
# the file, which is the same trust this script already places in itself: both
# are on the card, and anyone who can edit one can edit the other.
BRIGHTNESS=7
WIFI=0
BLUETOOTH=0
TIMEZONE=America/New_York
BOOT_ENV=$USERDATA_PATH/boot.env
[ -f "$BOOT_ENV" ] && . "$BOOT_ENV"

# radio_off/wifi_on/bt_off/bt_on and their helpers: in a file of its own, same
# reason as bt-alsa.sh below, so plat_sleep() can re-run them on every
# sleep/wake cycle without launch.sh's boot-only side effects. Tested first for
# the same reason as bt-alsa.sh's own guard: `.` on a missing file ends a
# non-interactive shell, which powers the device off (see .tmp_update/updater).
[ -f "$TORTOS_DIR/radio.sh" ] && . "$TORTOS_DIR/radio.sh"

# All LEDs off: TortOS shows no chrome, and the lights are pure battery drain.
# A function, because trimui_inputd re-lights them when it starts.
leds_off() {
	echo 0 > /sys/class/led_anim/effect_enable 2> /dev/null
	for g in l r lr m f1 f2; do echo "000000 " > /sys/class/led_anim/effect_rgb_hex_$g 2> /dev/null; done
	echo 0 > /sys/class/led_anim/max_scale 2> /dev/null
	echo 0 > /sys/class/led_anim/max_scale_lr 2> /dev/null
	echo 0 > /sys/class/led_anim/max_scale_f1f2 2> /dev/null
	# Only lit channels: a burst of raw writes overflows the LED controller, and
	# on the Brick Pro that starves the stick chip's i2c bus until the kernel
	# panics (TortOS-pky.9).
	for f in /sys/class/leds/sunxi_led*/brightness; do [ "$(cat "$f" 2> /dev/null)" = 0 ] || echo 0 > "$f" 2> /dev/null; done
}

# Apply the configured brightness now, so the boot picture is not dimmer than
# everything after it. tortos.elf has not started yet and cannot do it.
#
# This ladder is TortOS's own and is shared verbatim with platform.c and with
# the emulator: twelve geometric rungs, the first being the panel's measured
# floor (0 and 1 are black on this display). A saved level in
# The level the player last chose, which the launcher already resolved against
# the shipped default before exporting it - so there is one value here now
# rather than a saved level and a fallback.
brightness_raw() {
	B=$BRIGHTNESS
	case "$B" in
		0) echo 2;;  1) echo 4;;   2) echo 8;;   3) echo 16;;
		4) echo 32;; 5) echo 48;;  6) echo 72;;  7) echo 96;;
		8) echo 128;; 9) echo 160;; 10) echo 192;; 11) echo 255;;
		*) echo 96;;
	esac
}
[ -x "$TORTOS_DIR/setbright" ] && "$TORTOS_DIR/setbright" "$(brightness_raw)"
leds_off

# The u-boot splash and pic2fb's loading splash: the plorpOS wordmark on black,
# one still from power-on until the launcher's first frame (plorpos-act.5;
# made by mk/brick-bootlogo.py). Stock's pictures are backed up first.
#
# The markers hold BOOT_ART, not just exist: they ride along on reinstalls and
# OTA updates, so a marker's presence once kept new pictures from ever
# reaching a card that had the old ones. Bump BOOT_ART with any new picture.
#
# Ask the image on the device what it is, never a marker, before backing it
# up. The marker is the card's, the image is the device's: reinstalling on a
# freshly formatted card once "backed up the stock logo" - by then OUR logo -
# over the real one, and the genuine stock image was gone for good (found
# 2026-08-30). So a picture is only backed up when there is no backup yet and
# it is not one plorpOS or TortOS ever shipped (shipped_art: their md5s).
BOOT_ART=2
shipped_art() {
	case "$(md5sum "$1" 2> /dev/null | cut -d' ' -f1)" in
	4cb66c6a79d96764049369e350fee809|b3c8d784f68d657766040eaedc826cf5) return 0;; # bootlogo.bmp
	f5adf4613dc9483b189f5916e111b15f|781956faa127ae07f3fb9fbb41d01170) return 0;; # splash.png
	esac
	return 1
}
# apply_art PICTURE ON_DEVICE STOCK_BACKUP MARKER
apply_art() {
	if ! cmp -s "$2" "$1"; then
		if [ -f "$2" ] && [ ! -f "$3" ] && ! shipped_art "$2"; then
			cp "$2" "$3" || return
		fi
		cp "$1" "$2.new" && mv "$2.new" "$2" && sync || return
	fi
	echo "$BOOT_ART" > "$4"
}
if [ -f "$TORTOS_DIR/bootlogo.bmp" ] &&
   [ "$(cat "$TORTOS_DIR/.bootlogo_applied" 2> /dev/null)" != "$BOOT_ART" ]; then
	mkdir -p /mnt/boot
	if mount -t vfat /dev/mmcblk0p1 /mnt/boot 2> /dev/null; then
		apply_art "$TORTOS_DIR/bootlogo.bmp" /mnt/boot/bootlogo.bmp \
		          "$TORTOS_DIR/bootlogo.stock.bmp" "$TORTOS_DIR/.bootlogo_applied"
		umount /mnt/boot
	fi
fi
if [ -f "$TORTOS_DIR/splash.png" ] &&
   [ "$(cat "$TORTOS_DIR/.splash_applied" 2> /dev/null)" != "$BOOT_ART" ]; then
	apply_art "$TORTOS_DIR/splash.png" /etc/splash.png \
	          "$TORTOS_DIR/splash.stock.png" "$TORTOS_DIR/.splash_applied"
fi

# One-time: pic2fb blits that splash at the hardware default brightness, which
# is not ours, so the screen visibly steps partway through the boot. Call the
# brightness helper before it. Reversible: the stock init is backed up.
if [ -x "$TORTOS_DIR/setbright" ] && [ ! -f "$TORTOS_DIR/.brightboot_applied" ]; then
	cp "$TORTOS_DIR/setbright" /usr/trimui/bin/setbright 2> /dev/null && chmod +x /usr/trimui/bin/setbright
	cat > /usr/trimui/bin/tortos-bootbright.sh <<'BB'
#!/bin/sh
BR=$(sed -n "s/^BRIGHTNESS='\\(.*\\)'$/\\1/p" /mnt/SDCARD/.userdata/tg3040/boot.env 2> /dev/null | tail -1)
case "$BR" in
	0) R=1;; 1) R=8;; 2) R=16;; 3) R=32;; 4) R=48;; 5) R=72;;
	6) R=96;; 7) R=128;; 8) R=160;; 9) R=192;; 10) R=255;; *) R=160;;
esac
[ -x /usr/trimui/bin/setbright ] && /usr/trimui/bin/setbright "$R"
BB
	chmod +x /usr/trimui/bin/tortos-bootbright.sh
	if [ -f /etc/init.d/runtrimui ] && ! grep -q tortos-bootbright /etc/init.d/runtrimui; then
		cp /etc/init.d/runtrimui /etc/init.d/runtrimui.tortos-bak
		awk '/pic2fb/ && !d {print "/usr/trimui/bin/tortos-bootbright.sh"; d=1} {print}' \
			/etc/init.d/runtrimui.tortos-bak > /etc/init.d/runtrimui
		sh -n /etc/init.d/runtrimui 2> /dev/null || cp /etc/init.d/runtrimui.tortos-bak /etc/init.d/runtrimui
		chmod +x /etc/init.d/runtrimui
	fi
	sync
	touch "$TORTOS_DIR/.brightboot_applied"
fi

# The kernel prints every boot message to a 115200-baud serial console nobody
# reads (u-boot env loglevel=8), and printk waits for the UART: 2.2 s of the
# kernel's 4.2 s on the Brick (plorpos-act.3, measured 2026-11-27). Level 4
# still prints errors and dmesg keeps everything. Checked by value every boot
# (fw_printenv is ~1 ms), not by a marker: a marker rides along in installs.
# Only stock's 8 is changed; restore with `fw_setenv loglevel 8`.
if [ "$(fw_printenv -n loglevel 2> /dev/null)" = 8 ]; then
	fw_setenv loglevel 4 2> /dev/null
fi

# The boot hook (/usr/trimui/bin/runtrimui.sh) runs before the card is up, so
# a newer one on the card only takes effect from the next boot. Kept current by
# content, not by a marker (plorpos-act.4: it now mounts the card itself), and
# only where the installer has already put a hook in place of stock's. Checked
# with sh -n and moved in whole: a broken hook means no plorpOS and no stock.
HOOK=/usr/trimui/bin/runtrimui.sh
HOOK_SRC=$SDCARD/trimui/app/runtrimui.sh
if [ -f "$HOOK_SRC" ] && [ -f /usr/trimui/bin/runtrimui-original.sh ] &&
   ! cmp -s "$HOOK_SRC" "$HOOK"; then
	cp "$HOOK_SRC" "$HOOK.new" && sh -n "$HOOK.new" && chmod +x "$HOOK.new" &&
		mv "$HOOK.new" "$HOOK" && sync
	rm -f "$HOOK.new"
fi

# No boot animation (plorpos-act.5): the still from bootlogo.bmp/splash.png
# stays up and the launcher presents the moment it is ready. The animation it
# replaced held the shelf back once startup got faster than its 2.4 s.

# Rumble off, mute-switch gpio readable
echo 227 > /sys/class/gpio/export 2> /dev/null
echo -n out > /sys/class/gpio/gpio227/direction 2> /dev/null
echo -n 0 > /sys/class/gpio/gpio227/value 2> /dev/null

# Timezone, before anything that writes a timestamp.
#
# The stock firmware points /tmp/localtime at Asia/Shanghai, which is eight
# hours out for anyone who did not buy the device there, and every date the
# launcher shows - the one under a save slot - is local time. The zoneinfo
# database is already on the device; this only chooses from it.
#
# The clock itself is not set here and does not need to be: /etc/rc.d/S98sysntpd
# runs ntpd, which corrects the time within a minute of Wi-Fi connecting. That
# is also why the device sat in 1970 until Wi-Fi worked - ntpd was running the
# whole time with nothing to reach.
TZNAME=$TIMEZONE
if [ -n "$TZNAME" ] && [ -f "/usr/share/zoneinfo/$TZNAME" ]; then
	ln -sf "/usr/share/zoneinfo/$TZNAME" /tmp/localtime
	rm -f /tmp/TZ
elif [ -n "$TZNAME" ]; then
	echo "tz: no zoneinfo for '$TZNAME', keeping the firmware default" \
	     >> "$LOGS_PATH/tortos.log"
fi

# Radio silence. TortOS has nothing to talk to yet: no downloads, no pairing,
# no achievements. Both radios are battery drain and boot time.
#
# Wi-Fi on in the settings -- or a .devwifi marker -- keeps WiFi up so a
# development unit stays reachable over ssh. Without it there is no way to
# diagnose a problem on hardware except by pulling the card, which is a bad
# place to be when something does not come up.
#
# Stop the supplicant and drop the interface rather than rfkill-blocking, so
# the radio is left in a state the firmware understands.
#
# Both halves of this were dead until 2026-08-29, and measurably so: the
# supplicant was running on a device whose own log said radio silence.
#
# The `on` half called /etc/wifi/wifi_init.sh, which does not exist anywhere
# on the device and never has, so it failed silently and the flag did nothing.
# The stock service is procd-managed, so starting it is what the init script
# is for.
#
# The `off` half lost a race. /etc/rc.d/S96wpa_supplicant is USE_PROCD=1 and
# procd is pid 1, so procd owns the process and respawns a bare kill. The
# init script's `stop` handles that correctly -- but its `start_service`
# retries `ifconfig wlan0 up` five times with usleep 500000 between, so S96
# is still inside that loop when S99 runs us, and it finishes and starts the
# supplicant after we asked for it to be stopped. PIDs told the story:
# launch.sh 1839, tortos.elf 2156, wpa_supplicant 2286.
#
# So the stop is repeated across a window rather than once, and backgrounded,
# because waiting out someone else's usleeps is not worth the boot time.
#
# The window, and not just a retry-until-gone loop, because stopping it once
# is not the same as it staying stopped. Measured on 2026-08-29: a loop that
# exited on the first clear reading left the supplicant gone at 20s and back
# at 37s, restarted after tortos.elf was already up. It then exited on its
# own around 90s, but only because wlan0 was down underneath it -- which is
# luck, not a mechanism. Keep stopping it for the whole window instead.
#
# radio_off() and wifi_on() (the bring-up counterpart) now live in radio.sh,
# sourced above - see that file's header for why.

# What THIS device was doing when it was last shut down, which is what someone
# who turned Wi-Fi on expects to find. The launcher resolved that against the
# shipped default before exporting, so $WIFI is already the answer.
if [ "$WIFI" = "1" ] || [ -f "$TORTOS_DIR/.devwifi" ]; then
	wifi_up=wifi_on
else
	radio_off &
	wifi_up=:
fi

# Bluetooth, off unless asked for, because both radios are battery drain and
# boot time, and the state the player left it in wins over the shipped
# default. bt_off()/bt_on() and their helpers now live in radio.sh, sourced
# above - see that file's header for why, and its comment on bt_on() for the
# bring-up rationale (rfkill power-cycle, xradio attach, hfp-ag) unchanged from
# here.

# With both on, one job, Wi-Fi associated first and then Bluetooth, as after
# a wake: Wi-Fi coming up next to Bluetooth on the same xradio chip froze the
# Brick Pro (plorpos-pky.11; numbers in plat_sleep, src/platform_brick.c).
if [ "$BLUETOOTH" = "1" ]; then
	{ $wifi_up; bt_on; } &
else
	bt_off &
	$wifi_up &
fi

# CPU: interactive scaling
echo interactive > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2> /dev/null

# EVERY button and the d-pad arrive through the stock GPIO input daemon's
# virtual joystick at /dev/input/event3. Without this daemon there is no d-pad
# and no face buttons at all -- only volume and power, which come from real
# kernel devices at event0/event1. It is the single line the whole control
# scheme depends on.
pgrep trimui_inputd > /dev/null || trimui_inputd &

# LEDs off again: trimui_inputd re-enables them when it starts. A second on,
# in the background: in line, the sleep held up Diatom, the launcher and
# everything after them by a full second, 1.05 s of the boot measured
# 2026-10-02. The launcher puts them out itself as it starts, too.
( sleep 1; leds_off ) &

# One log per boot, carrying the launcher AND everything it starts. Without
# this a failure inside a game goes to a console nobody reads.
#
# Ten boots kept: this one and tortos.log.1 to .9, newest first. One was not
# enough - on the first fresh-card test the boots that mattered were gone
# before anyone read the log, and a player's report can come several reboots
# after the fault. Measured on the device 2026-09-29, a boot's log runs 3 to
# 30 KB, so ten is 300 KB at most.
#
# Only the last boot's log is moved out of this one's way here; the older ones
# shift along in the background, where nothing waits on them. In line, the
# renames took 260 ms of the boot on the card, measured 2026-10-02.
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

# The install folder's rename (plorpos-nzi): what is left of TortOS/ moves in.
sh "$TORTOS_DIR/folder-move.sh" /mnt/SDCARD >> "$LOG" 2>&1

# The resident emulator. It holds the GL context and every core between
# games, which takes a launch from ~1100ms to ~200ms. Started here so its ~1s
# of setup happens alongside the launcher's own.
#
# Nothing depends on it: the launcher checks for its fifos and runs a game the
# old way, one process per game, when they are not there. That is what happens
# for a launch in the first second after boot, and if this ever dies.
# The resident emulator is Diatom. It maps every core in cores/ itself as it
# starts, before its socket exists, and keeps each for the life of the
# process (diatom_core_premap), so there is no core list to hand over and
# nothing here changes when a system is added. The fallback for a resident that dies mid-session is the same
# binary run standalone by the launcher - one emulator, held two ways.
export TORTOS_DIATOM_SOCKET=/tmp/diatom.sock
# Lost once, in 1258dd7's move of the radio functions to radio.sh, with both
# calls left behind: sh fails a missing function quietly, so every boot ran its
# first game the old way, where a POWER tap powers off (TortOS-5bu).
start_resident() {
	pgrep -f "plorpOS/diatom --socket" > /dev/null && return
	rm -f "$TORTOS_DIATOM_SOCKET"
	LD_LIBRARY_PATH=/usr/trimui/lib \
		"$TORTOS_DIR/diatom" --socket "$TORTOS_DIATOM_SOCKET" \
		--cores "$CORES_PATH" \
		--save "$SDCARD/Saves" --system "$SDCARD/Bios" >> "$LOG" 2>&1 &
}
# BEFORE start_resident, and that order is the whole point: alsa-lib caches its
# config at the first PCM open, so a definition written afterwards is invisible
# to this process for as long as it lives.
bt_write_asoundrc
start_resident

rm -f /tmp/tortos_poweroff /tmp/tortos_reboot
# Nothing is CONNECTED yet; bt_reconnect writes this when something is. The
# .asoundrc is not removed with it - it describes bonds, which outlive any
# connection, and removing it here would undo the line above.

# AppleDouble litter, swept in the background.
#
# Copying to the card from a Mac leaves ._name beside every file, and the
# project's own install does the same: 76 of them from a 43-file payload on a
# freshly formatted card, measured 2026-09-02. Harmless - lib_scan skips every
# dot-prefixed name in all three of its readdir loops, so they never become
# phantom games - but they double the directory entries on a FAT card and
# "._Contra (USA).zip" sitting beside the real one is confusing to read.
#
# Backgrounded behind a sleep because it is housekeeping and not a
# precondition: the scan already ignores them, so nothing waits on this. A
# card-wide sweep measured 190ms against a boot of about 900, which is too
# much to spend on the critical path and nothing at all once it is off it. The
# sleep also keeps it clear of the library scan's own I/O.
#
# NAMES, never "every dot entry". .media, .cheevos, .tortos, .tmp_update and
# .userdata are ours and the whole library hangs off them. So: ._ files, and
# since 2026-09-30 .DS_Store, and Windows' Thumbs.db and desktop.ini,
# anywhere; and the folders macOS writes at the top of any card it mounts.
# Measured the same day on the device, the pass takes 0.11 s over the card.
#
# .Trashes too, by Eric's choice the same day: it is the Mac's Trash for this
# card, files deleted in Finder and never emptied - 748 MB of them on the test
# card - and on a handheld "deleted" is expected to mean gone. The cost is that
# a file trashed there by mistake cannot be put back once the Brick has booted.
# First, so the find below does not walk it. busybox find has no -delete, and
# -exec is used rather than xargs because these names contain spaces.
( sleep 8
  rm -rf /mnt/SDCARD/.Trashes /mnt/SDCARD/.Spotlight-V100 /mnt/SDCARD/.fseventsd \
         /mnt/SDCARD/.TemporaryItems
  find /mnt/SDCARD \( -name '._*' -o -name '.DS_Store' -o -name 'Thumbs.db' \
       -o -name 'desktop.ini' \) -type f -exec rm -f {} \;
) >/dev/null 2>&1 &

# Whole seconds since boot into NOW, for telling a crash loop from a normal
# exit. The shell's own read rather than $(cut ...), a process start for one
# number.
now_s() {
	local up rest
	read up rest < /proc/uptime
	NOW=${up%%.*}
}

# Restart loop: only ever exits for a power-off.
cd "$TORTOS_DIR"
FAILS=0
while : ; do
	now_s; START=$NOW
	./tortos.elf >> "$LOG" 2>&1
	[ -f /tmp/tortos_poweroff ] && break
	[ -f /tmp/tortos_reboot ] && break     # an installed update (plorpos-koo)
	now_s; END=$NOW
	# A launcher that dies immediately, five times running, is not going to
	# start on the sixth. Stop rather than strobe.
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
	# Before the next start rather than at the top of the loop, where the
	# first pass repeated the two lines just before it - and asked whether
	# Diatom was running the instant after starting it, which a check by name
	# can miss and start a second one (seen on the GKD Pixel 2, 2026-10-01).
	leds_off
	start_resident          # bring it back if it died
done

sync
[ -f /tmp/tortos_reboot ] && { reboot; sleep 10; }
poweroff
sleep 10
