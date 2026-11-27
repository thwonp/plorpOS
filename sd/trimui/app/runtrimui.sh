#!/bin/sh
# Installed to /usr/trimui/bin/runtrimui.sh by the first-boot installer, and
# refreshed by launch.sh whenever the card's copy differs.
# Mounts the SD card, then runs its updater (plorpOS) if present, else stock.

# Mount the card here rather than wait for stock to: the kernel has the card
# at ~2 s, but stock's `block mount` (S40fstab) only gets to it ~1.4 s after
# this hook starts (plorpos-act.4, measured 2026-11-27). Only vfat and exfat,
# with the options stock's mount ends up with, so the card looks the same to
# everything after; any other filesystem (NTFS goes through ntfs-3g in
# /etc/hotplug.d/block/10-mount) is left to stock, as before. If stock gets
# there first, or later finds it mounted, either mount just fails.
card_mounted() { grep -q ' /mnt/SDCARD ' /proc/mounts; }
mount_card() {
	[ -b /dev/mmcblk1p1 ] || return 1
	mkdir -p /mnt/SDCARD
	mount -t vfat -o rw,sync,umask=0000,iocharset=utf8,errors=continue \
	      /dev/mmcblk1p1 /mnt/SDCARD 2> /dev/null ||
	mount -t exfat -o rw,iocharset=utf8 /dev/mmcblk1p1 /mnt/SDCARD 2> /dev/null
}

cnt=0
while ! card_mounted && [ $cnt -lt 30 ]; do
	mount_card && break
	sleep 0.1
	cnt=$((cnt + 1))
done

UPDATER=/mnt/SDCARD/.tmp_update/updater
if [ -f "$UPDATER" ]; then
	"$UPDATER"
else
	/usr/trimui/bin/runtrimui-original.sh
fi
