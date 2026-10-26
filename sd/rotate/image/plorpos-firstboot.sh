#!/bin/sh
# The RG Rotate card's first boot (plorpos-3yv.7): the data partition, made
# here and not in the image so it fills whatever card the image went onto.
# plorpos-firstboot.service runs this whenever the data partition has no
# plorpOS on it - so also after the player wiped it - before plorpos.service.
#
#   p1 uboot, p2 bootfs, p3 the rootfs (fixed size), p4 exFAT "PLORPOS",
#   mounted at /mnt/SDCARD: Roms, Saves, Bios, Music and plorpOS/.
#
# plorpOS/ is seeded from the copy in the rootfs (/usr/share/plorpos/plorpOS);
# an update is a newer plorpOS folder copied over it, as on the other devices.
set -eu
# The image has no ssh host keys: they would be every card's (plorpos-3yv.13.4).
# Made here once; ssh itself stays off until Network Services turns it on.
ssh-keygen -A
ROOT=$(findmnt -no SOURCE /)
DISK=/dev/$(lsblk -no PKNAME "$ROOT")
DATA=${DISK}p4

if [ ! -b "$DATA" ]; then
	echo "firstboot: making the data partition on $DISK"
	# The image's backup GPT sits where the image ended, not the card.
	sfdisk --relocate gpt-bak-std "$DISK"
	echo 'type=EBD0A0A2-B9E5-4433-87C0-68B6B72699C7, name="plorpos"' |
		sfdisk --append --no-reread --no-tell-kernel "$DISK"
	partx -a --nr 4 "$DISK"
	udevadm settle
	mkfs.exfat -L PLORPOS "$DATA"
fi
mkdir -p /mnt/SDCARD
mountpoint -q /mnt/SDCARD || mount /mnt/SDCARD
if [ ! -f /mnt/SDCARD/plorpOS/rotate/session.sh ]; then
	echo "firstboot: putting plorpOS on the data partition"
	cp -R /usr/share/plorpos/plorpOS /mnt/SDCARD/
fi
mkdir -p /mnt/SDCARD/Roms /mnt/SDCARD/Saves /mnt/SDCARD/Bios /mnt/SDCARD/Music
# A ROM folder per shelf, with the .media folder box art goes in, as the other
# devices' cards come (mk/plorpos-files.sh plorpos_rom_folders; plorpos-3yv.89).
awk -F'|' '$1=="sys"{gsub(/^[ \t]+|[ \t]+$/,"",$3); print $3}' /mnt/SDCARD/plorpOS/systems.cfg |
while IFS= read -r f; do mkdir -p "/mnt/SDCARD/Roms/$f/.media"; done
# A Saves folder per shelf too, but Ports (plorpos_save_folders, plorpos-3yv.96).
awk -F'|' '$1=="sys"{gsub(/^[ \t]+|[ \t]+$/,"",$3); gsub(/^[ \t]+|[ \t]+$/,"",$4)
	if ($4 != "portmaster") print $3}' /mnt/SDCARD/plorpOS/systems.cfg |
while IFS= read -r f; do mkdir -p "/mnt/SDCARD/Saves/$f"; done
sync
