#!/bin/sh
# The RG Rotate card's firmware setup (plorpos-3yv.13.2.2): the card ships
# no Unisoc firmware, so the first boot takes this unit's own off its eMMC
# and powers off; from the next boot on, audio and deep sleep work.
#
# The audio DSP (agdsp) and the CM4 (pm_sys) drivers are built in and load
# their firmware before the rootfs is mounted, so both go into
# /boot/initrd-fw.cpio.gz (with dev/console and the shipped pm_bootcode).
# Wi-Fi/BT's wcnmodem.bin is Armbian's rotate-extract-firmware's (rootfs,
# runs before this): checked here, not taken. Setup counts as done only with
# all three; then /boot/plorpos-fw.done. Anything missing or not the image
# we expect: /boot/plorpos-fw.failed, a message, and boot goes on - once,
# not every boot (delete the marker to try again).
#
# BOOT, PARTS, TTY and POWEROFF override /boot, the partition links, the
# console and the power-off, for a dry run on a set-up unit.
set -u
BOOT=${BOOT:-/boot}
PARTS=${PARTS:-/dev/disk/by-partlabel}
POWEROFF=${POWEROFF:-systemctl --no-block poweroff}
WCN=/lib/firmware/updates/wcnmodem.bin
BOOTCODE=/usr/lib/firmware/sprd/pm_bootcode.bin
TTY=${TTY:-/dev/tty1}

log() { echo "plorpos-firmware: $*"; }
# Big text on the console; the launcher is not up yet.
screen() {
	{
		printf '\033[2J\033[H\n'
		for l; do printf '  %s\n' "$l"; done
	} > "$TTY" 2>/dev/null
}
magic() { [ "$(od -An -tx1 -N"$2" "$1" 2>/dev/null | tr -d ' \n')" = "$3" ]; }
agdsp_ok() { [ "$(dd "if=$1" bs=14 count=1 status=none 2>/dev/null)" = "SharkL5_AUDCP_" ]; }
pm_sys_ok() { magic "$1" 4 dff80cd0; }
wcn_ok() { magic "$1" 4 a00a1a00; }

# take LABEL BYTES CHECK OUT: slot a, else b, whichever passes CHECK.
take() {
	for p in "$PARTS/$1_a" "$PARTS/$1_b"; do
		[ -r "$p" ] || continue
		dd "if=$p" "of=$4" bs=512 count=$(($2 / 512)) status=none && "$3" "$4" &&
			{ log "$4 from $p"; return 0; }
	done
	return 1
}

fail() {
	log "$1"
	echo "$1" > "$BOOT/plorpos-fw.failed"
	sync
	screen "plorpOS: could not read the audio and" "sleep firmware from this device's" \
		"internal storage ($2)." "" "Audio and deep sleep will not work." \
		"Starting anyway..."
	sleep 15
	exit 0
}

setfont -C "$TTY" /usr/share/consolefonts/Uni2-Terminus32x16.psf.gz 2>/dev/null
screen "plorpOS: setting up audio and sleep" "firmware from this device..." "" \
	"Please wait, do not turn it off."
udevadm settle -t 30

T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
mkdir -p "$T/dev" "$T/lib/firmware/updates" "$T/lib/firmware/sprd"
mknod -m 600 "$T/dev/console" c 5 1
wcn_ok "$WCN" || fail "no valid $WCN (rotate-extract-firmware)" wcnmodem
take l_agdsp 5242880 agdsp_ok "$T/lib/firmware/updates/agdsp.bin" || fail "no valid l_agdsp_a|b" agdsp
take pm_sys 1048576 pm_sys_ok "$T/lib/firmware/sprd/pm_sys.bin" || fail "no valid pm_sys_a|b" pm_sys
cp "$BOOTCODE" "$T/lib/firmware/sprd/" || fail "no $BOOTCODE" pm_bootcode
chmod 644 "$T"/lib/firmware/*/*.bin
# The rootfs gets pm_sys too: the firmware cache re-reads every loaded
# firmware before each suspend, from there.
cp "$T/lib/firmware/sprd/pm_sys.bin" /usr/lib/firmware/sprd/pm_sys.bin.new &&
	mv -f /usr/lib/firmware/sprd/pm_sys.bin.new /usr/lib/firmware/sprd/pm_sys.bin ||
	fail "could not write /usr/lib/firmware/sprd/pm_sys.bin" rootfs

( cd "$T" && find . -mindepth 1 | cpio -o -H newc -R 0:0 --quiet ) | gzip -9 > "$BOOT/initrd-fw.cpio.gz.new" &&
	mv -f "$BOOT/initrd-fw.cpio.gz.new" "$BOOT/initrd-fw.cpio.gz" &&
	sync || fail "could not write $BOOT/initrd-fw.cpio.gz" "boot partition"
touch "$BOOT/plorpos-fw.done"
sync
log "initrd written, powering off"
screen "plorpOS: setup complete." "" "The device will now power off." \
	"Turn it on again to start plorpOS."
sleep 8
$POWEROFF
