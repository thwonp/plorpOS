#!/bin/sh
# The RG Rotate card image (plorpos-3yv.13), ALPHA: built from a set-up
# device's own SD card, over ssh - its rootfs (Armbian trixie + the runtime
# packages: sway, PipeWire, Mesa, SDL2, FFmpeg), its U-Boot and its /boot
# (whose initramfs carries the WCN/AGDSP firmware Armbian extracted from
# THAT unit's eMMC, which every RG Rotate has the same of). Nothing on the
# device is written. Rootless: the rootfs is unpacked and packed again
# (mke2fs -d) inside a podman container, so its owners survive.
#
#   mk/rotate-image.sh [OUT]   (default ~/projects/misc/rg-rotate/image/plorpos-rotate.img)
#   REUSE=1 mk/rotate-image.sh  repacks from the last read (work/), no device
#
# The image: p1 uboot and p2 bootfs as on the device, p3 the rootfs at a
# fixed ROOT_MB with the device's UUID (extlinux and fstab name it), no p4:
# the first boot makes the exFAT data partition across the rest of the card
# (sd/rotate/image/plorpos-firstboot.sh). The rootfs carries the device's
# root password and ssh keys: an image for its owner, not for handing out.
# Wi-Fi is not in it: the launcher runs wpa_supplicant on the data
# partition's saved networks (plorpos-3yv.17), so a fresh card joins one from
# the Wi-Fi screen.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-$HOME/projects/misc/rg-rotate/image/plorpos-rotate.img}
HOST=${ROTATE_HOST:-root@192.168.0.69}
KEY=${ROTATE_KEY:-$HOME/.ssh/rgrotate}
ROOT_MB=${ROOT_MB:-4608}
W=$(dirname "$OUT")/work
ssh_() { ssh -n -i "$KEY" -o IdentitiesOnly=yes "$HOST" "$@"; }

mkdir -p "$W"
rm -rf "$W/overlay"
# 1. What plorpOS adds to the rootfs: the plorpOS folder to seed the card
#    with, the session and first-boot units (enabled), the data partition's
#    mount, and no getty on tty1.
O=$W/overlay
mkdir -p "$O/usr/share/plorpos" "$O/etc/systemd/system/multi-user.target.wants" \
	"$O/etc/systemd/network" "$O/etc/wireplumber/wireplumber.conf.d"
ROTATE_STAGE="$O/usr/share/plorpos/plorpOS" sh "$ROOT/mk/rotate-deploy.sh" stage
cp "$ROOT/sd/rotate/image/plorpos-firstboot.sh" "$O/usr/share/plorpos/"
cp "$ROOT/sd/rotate/plorpos.service" "$ROOT/sd/rotate/image/plorpos-firstboot.service" \
	"$O/etc/systemd/system/"
ln -s ../plorpos.service "$O/etc/systemd/system/multi-user.target.wants/plorpos.service"
ln -s ../plorpos-firstboot.service \
	"$O/etc/systemd/system/multi-user.target.wants/plorpos-firstboot.service"
cp "$ROOT/sd/rotate/image/20-wlan0.network" "$O/etc/systemd/network/"
cp "$ROOT/sd/rotate/image/99-rotate-streams.conf" "$O/etc/wireplumber/wireplumber.conf.d/"

# 2. The device's partitions 1-2 raw, and its rootfs as a tarball. ~3-5 min
#    over Wi-Fi.
if [ -z "${REUSE:-}" ]; then
echo "image: reading p1, p2 and the rootfs from $HOST"
ssh_ 'dd if=/dev/mmcblk0p1 bs=1M status=none' > "$W/p1.img"
ssh_ 'dd if=/dev/mmcblk0p2 bs=1M status=none' > "$W/p2.img"
ssh_ 'cat /etc/fstab' > "$W/fstab"
UUID=$(awk '$2 == "/" { sub("UUID=", "", $1); print $1 }' "$W/fstab")
ssh_ 'tar -C / --one-file-system --numeric-owner -cf - \
	--exclude=./mnt/SDCARD --exclude=./tmp/* --exclude=./var/tmp/* \
	--exclude=./var/log/journal/* --exclude=./var/cache/apt/archives/*.deb \
	--exclude=./root/.bash_history --exclude=./root/*.py --exclude=./root/*.sh \
	--exclude=./root/*.orig --exclude=./root/k --exclude=./root/wp.env \
	--exclude=./swapfile . | zstd -1 -T0' > "$W/rootfs.tar.zst"
fi
[ -s "$W/rootfs.tar.zst" ] || { echo "image: no rootfs read" >&2; exit 1; }
UUID=$(awk '$2 == "/" { sub("UUID=", "", $1); print $1 }' "$W/fstab")

# 3. The rootfs filesystem, inside a container (root there = this user, so
#    owners are kept). fstab gains the data partition; Armbian's resize stays
#    off (the data partition is what grows).
echo "image: packing the rootfs (${ROOT_MB} MiB, UUID $UUID)"
docker run --rm -v "$W:/w" -e UUID="$UUID" -e ROOT_MB="$ROOT_MB" debian:trixie-slim sh -euc '
	apt-get -qq update > /dev/null && apt-get -qq install -y e2fsprogs zstd > /dev/null
	mkdir /r
	zstd -dc /w/rootfs.tar.zst | tar -C /r --numeric-owner -xpf -
	cp -a /w/overlay/. /r/
	chown -R 0:0 /r/usr/share/plorpos /r/etc/systemd/system/plorpos*.service \
		/r/etc/systemd/network/20-wlan0.network \
		/r/etc/wireplumber/wireplumber.conf.d/99-rotate-streams.conf
	rm -f /r/etc/systemd/system/multi-user.target.wants/rotate-pad2key.service
	ln -sf /dev/null /r/etc/systemd/system/getty@tty1.service
	# Wi-Fi is the launcher'"'"'s (plorpos-3yv.17): not netplan'"'"'s, and not the
	# D-Bus wpa_supplicant either, which owns no interface here.
	rm -f /r/etc/netplan/20-wifi.yaml
	ln -sf /dev/null /r/etc/systemd/system/wpa_supplicant.service
	grep -q PLORPOS /r/etc/fstab || echo "LABEL=PLORPOS /mnt/SDCARD exfat defaults,nofail,noatime,x-systemd.device-timeout=3s 0 0" >> /r/etc/fstab
	mkdir -p /r/mnt/SDCARD
	rm -f /w/p3.img
	mke2fs -q -t ext4 -L armbi_root -U "$UUID" -d /r /w/p3.img "${ROOT_MB}M"
'

# 4. The card: the device's GPT, with p3 cut to the rootfs and nothing after.
echo "image: assembling $OUT"
P3_START=1083392
P3_SECT=$((ROOT_MB * 2048))
TOTAL=$((P3_START + P3_SECT + 2048))
rm -f "$OUT"
truncate -s $((TOTAL * 512)) "$OUT"
sfdisk -q "$OUT" << SFD
label: gpt
first-lba: 2048
start=2048, size=32768, type=8DA63339-0007-60C0-C436-083AC8230908, name="uboot"
start=34816, size=1048576, type=BC13C2FF-59E6-4262-A352-B275FD6F7172, name="bootfs"
start=$P3_START, size=$P3_SECT, type=0FC63DAF-8483-4772-8E79-3D69D8477DE4
SFD
dd if="$W/p1.img" of="$OUT" bs=512 seek=2048 conv=notrunc status=none
dd if="$W/p2.img" of="$OUT" bs=512 seek=34816 conv=notrunc status=none
dd if="$W/p3.img" of="$OUT" bs=512 seek=$P3_START conv=notrunc status=none
sha256sum "$OUT" > "$OUT.sha256"
echo "image: $OUT ($(du -h "$OUT" | cut -f1)), $(cut -c1-16 "$OUT.sha256")"
