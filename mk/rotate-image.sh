#!/bin/sh
# The RG Rotate card image (plorpos-3yv.13), ALPHA: built from a set-up
# device's own SD card, over ssh - its rootfs (Armbian trixie + the runtime
# packages: sway, PipeWire, Mesa, SDL2, FFmpeg), its U-Boot and its /boot.
# No Unisoc firmware goes in (plorpos-3yv.13.2.3): agdsp, wcnmodem and
# pm_sys are left out, and the card's first boot takes the unit's own off
# its eMMC (Armbian's rotate-extract-firmware, plorpos-firmware.sh), then
# powers off. Nothing on the device is written. Rootless: the rootfs is unpacked and packed again
# (mke2fs -d) inside a podman container, so its owners survive.
#
#   mk/rotate-image.sh [OUT]   (default ~/projects/misc/rg-rotate/image/plorpos-rotate.img)
#   REUSE=1 mk/rotate-image.sh  repacks from the last read (work/), no device
#
# The image: p1 uboot and p2 bootfs as on the device, p3 the rootfs at a
# fixed ROOT_MB with the device's UUID (extlinux and fstab name it), no p4:
# the first boot makes the exFAT data partition across the rest of the card
# (sd/rotate/image/plorpos-firstboot.sh). None of the device's owner goes in
# (plorpos-3yv.13.4): its users, /root, ssh host keys, logs, Bluetooth bonds
# and machine-id are dropped; the login is root / plorpos (the install guide);
# ssh is off until Network Services turns it on, with host keys made on the
# first boot. Wi-Fi is not in it either: the launcher runs wpa_supplicant on
# the data partition's saved networks (plorpos-3yv.17), so a fresh card joins
# one from the Wi-Fi screen.
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
#    with, the session, first-boot and firmware units (enabled), the data
#    partition's mount, no getty on tty1, and the CM4 boot code built from
#    source (mk/rotate-pm-bootcode.sh).
O=$W/overlay
mkdir -p "$O/usr/share/plorpos" "$O/etc/systemd/system/multi-user.target.wants" \
	"$O/etc/systemd/network" "$O/etc/wireplumber/wireplumber.conf.d" "$O/usr/lib/firmware/sprd"
sh "$ROOT/mk/rotate-pm-bootcode.sh" "$O/usr/lib/firmware/sprd/pm_bootcode.bin" > /dev/null
ROTATE_STAGE="$O/usr/share/plorpos/plorpOS" sh "$ROOT/mk/rotate-deploy.sh" stage
cp "$ROOT/sd/rotate/image/plorpos-firstboot.sh" "$ROOT/sd/rotate/image/plorpos-firmware.sh" \
	"$O/usr/share/plorpos/"
cp "$ROOT/sd/rotate/plorpos.service" "$ROOT/sd/rotate/image/plorpos-firstboot.service" \
	"$ROOT/sd/rotate/image/plorpos-firmware.service" "$O/etc/systemd/system/"
ln -s ../plorpos.service "$O/etc/systemd/system/multi-user.target.wants/plorpos.service"
ln -s ../plorpos-firstboot.service \
	"$O/etc/systemd/system/multi-user.target.wants/plorpos-firstboot.service"
ln -s ../plorpos-firmware.service \
	"$O/etc/systemd/system/multi-user.target.wants/plorpos-firmware.service"
cp "$ROOT/sd/rotate/image/20-wlan0.network" "$O/etc/systemd/network/"
cp "$ROOT/sd/rotate/image/99-rotate-streams.conf" "$ROOT/sd/rotate/image/99-rotate-volume.conf" \
	"$O/etc/wireplumber/wireplumber.conf.d/"
# The ports' own Mesa (plorpos-3yv.86.13), built by mk/build-ports-mesa.sh.
[ -d "$ROOT/vendor/rotate-mesa/lib" ] ||
	{ echo "image: no ports Mesa; run mk/build-ports-mesa.sh" >&2; exit 1; }
mkdir -p "$O/usr/lib"
cp -a "$ROOT/vendor/rotate-mesa" "$O/usr/lib/plorpos-mesa"

# 2. The device's partitions 1-2 raw, and its rootfs as a tarball. ~3-5 min
#    over Wi-Fi.
if [ -z "${REUSE:-}" ]; then
# The rootfs is the device's, so what it has installed is what the card gets.
# PortMaster's ports need these beyond the launcher's own (plorpos-3yv.86.2):
# SDL2_mixer and OpenAL for many ports, Xwayland for X11 ones, glxinfo
# (mesa-utils) for weston_pkg's ROCKNIX check, zip for some installers.
PM_PKGS="libsdl2-mixer-2.0-0 libopenal1 xwayland mesa-utils zip"
[ "$(ssh_ "dpkg-query -W -f '\${db:Status-Status}\n' $PM_PKGS 2>&1" | sort -u)" = installed ] ||
	{ echo "image: $HOST lacks PortMaster's packages; on it: apt install $PM_PKGS" >&2; exit 1; }
echo "image: reading p1, p2 and the rootfs from $HOST"
ssh_ 'dd if=/dev/mmcblk0p1 bs=1M status=none' > "$W/p1.img"
ssh_ 'dd if=/dev/mmcblk0p2 bs=1M status=none' > "$W/p2.img"
ssh_ 'cat /etc/fstab' > "$W/fstab"
UUID=$(awk '$2 == "/" { sub("UUID=", "", $1); print $1 }' "$W/fstab")
ssh_ 'tar -C / --one-file-system --numeric-owner -cf - \
	--exclude=./mnt/SDCARD --exclude=./tmp/* --exclude=./var/tmp/* \
	--exclude=./var/log/journal/* --exclude=./var/cache/apt/archives/*.deb \
	--exclude=./root/.bash_history --exclude="./root/[!.]*" \
	--exclude=./usr/lib/firmware/updates/agdsp.bin \
	--exclude=./usr/lib/firmware/updates/wcnmodem.bin \
	--exclude="./usr/lib/firmware/sprd/*" \
	--exclude=./swapfile . | zstd -1 -T0' > "$W/rootfs.tar.zst"
fi
[ -s "$W/rootfs.tar.zst" ] || { echo "image: no rootfs read" >&2; exit 1; }
UUID=$(awk '$2 == "/" { sub("UUID=", "", $1); print $1 }' "$W/fstab")

# 2b. p2 as Armbian ships it plus the plorpOS kernel, DT and extlinux.conf:
#    no dev kernels or backups, no first-boot markers (plorpos-fw.done
#    would skip the firmware setup), and no initramfs with firmware in it -
#    Armbian's initrd.img/uInitrd (regenerated with agdsp; nothing boots
#    them) go, and initrd-fw.cpio.gz becomes dev/console + pm_bootcode,
#    which plorpos-firmware.sh rewrites on the first boot.
echo "image: stripping p2"
cp "$W/p2.img" "$W/p2-out.img"
mdir -i "$W/p2-out.img" -/ -b ::/ | grep -E \
	'^::/(Image-plorpos(\.uimg\..+)?|initrd-fw\.cpio\.gz\..+|plorpos-fw\..+|initrd\.img-.+|uInitrd|dtb/sprd/.+\.dtb\..+|extlinux/extlinux\.conf\..+)$' |
	xargs -r mdel -i "$W/p2-out.img"
KBUILD=${KBUILD:-$HOME/projects/git/build-ums512}
sh "$ROOT/mk/rotate-pm-bootcode.sh" "$W/pm_bootcode.bin" > /dev/null
printf '%s\n' 'dir /dev 0755 0 0' 'nod /dev/console 0600 0 0 c 5 1' 'dir /lib 0755 0 0' \
	'dir /lib/firmware 0755 0 0' 'dir /lib/firmware/sprd 0755 0 0' \
	"file /lib/firmware/sprd/pm_bootcode.bin $W/pm_bootcode.bin 0644 0 0" > "$W/initrd.list"
"$KBUILD/usr/gen_init_cpio" -t 0 "$W/initrd.list" | gzip -9 -n > "$W/initrd-fw.cpio.gz"
mcopy -o -i "$W/p2-out.img" "$W/initrd-fw.cpio.gz" ::/initrd-fw.cpio.gz

# 3. The rootfs filesystem, inside a container (root there = this user, so
#    owners are kept). fstab gains the data partition; Armbian's resize stays
#    off (the data partition is what grows).
echo "image: packing the rootfs (${ROOT_MB} MiB, UUID $UUID)"
ROOT_HASH=$(openssl passwd -6 plorpos)
docker run --rm -v "$W:/w" -e UUID="$UUID" -e ROOT_MB="$ROOT_MB" -e ROOT_HASH="$ROOT_HASH" \
	debian:trixie-slim sh -euc '
	apt-get -qq update > /dev/null && apt-get -qq install -y e2fsprogs zstd > /dev/null
	mkdir /r
	zstd -dc /w/rootfs.tar.zst | tar -C /r --numeric-owner -xpf -
	cp -a /w/overlay/. /r/
	chown -R 0:0 /r/usr/share/plorpos /r/etc/systemd/system/plorpos*.service \
		/r/etc/systemd/network/20-wlan0.network \
		/r/etc/wireplumber/wireplumber.conf.d/99-rotate-streams.conf \
		/r/etc/wireplumber/wireplumber.conf.d/99-rotate-volume.conf \
		/r/usr/lib/firmware/sprd/pm_bootcode.bin /r/usr/lib/plorpos-mesa
	rm -f /r/etc/systemd/system/multi-user.target.wants/rotate-pad2key.service
	# armbian-firmware'"'"'s wcnmodem.bin -> uwe5622/ is another module'"'"'s
	# firmware: on the first boot, before the unit'"'"'s own lands in
	# updates/, the WCN driver loads it and oopses (plorpos-3yv.13.2.4).
	# Without it the driver waits for the real one.
	rm -f /r/usr/lib/firmware/wcnmodem.bin
	ln -sf /dev/null /r/etc/systemd/system/getty@tty1.service
	# No text consoles to set a font on (plorpos-3yv.49.2): its setfont
	# repaints fbcon and wipes the kernel boot logo.
	ln -sf /dev/null /r/etc/systemd/system/console-setup.service
	ln -sf /dev/null /r/etc/udev/rules.d/90-console-setup.rules
	# Wi-Fi is the launcher'"'"'s (plorpos-3yv.17): not netplan'"'"'s, and not the
	# D-Bus wpa_supplicant either, which owns no interface here.
	rm -f /r/etc/netplan/20-wifi.yaml
	ln -sf /dev/null /r/etc/systemd/system/wpa_supplicant.service
	# A card for anyone (plorpos-3yv.13.4): the owner'"'"'s accounts, /root,
	# history and logs go; root / plorpos; ssh off (Network Services turns
	# it on) and its host keys made on the first boot (plorpos-firstboot.sh);
	# machine-id empty, so systemd makes one on the first boot.
	awk -F: '"'"'$3 >= 1000 && $3 < 65534 { print $1 }'"'"' /r/etc/passwd |
		while read -r u; do userdel -R /r -r "$u" 2> /dev/null || userdel -R /r "$u"; done
	usermod -R /r -p "$ROOT_HASH" root
	rm -rf /r/root && mkdir /r/root && cp -a /r/etc/skel/. /r/root/ && chmod 700 /r/root
	rm -f /r/etc/ssh/ssh_host_* /r/etc/systemd/system/sshd.service \
		/r/etc/systemd/system/multi-user.target.wants/ssh.service
	# log.hdd: Armbian'"'"'s log2ram keeps the logs on disk there (Wi-Fi names
	# were in its journal and syslog).
	find /r/var/log /r/var/log.hdd -type f -delete 2> /dev/null || true
	rm -rf /r/var/lib/bluetooth/* /r/var/lib/systemd/coredump/* /r/var/lib/systemd/random-seed
	: > /r/etc/machine-id
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
dd if="$W/p2-out.img" of="$OUT" bs=512 seek=34816 conv=notrunc status=none
dd if="$W/p3.img" of="$OUT" bs=512 seek=$P3_START conv=notrunc status=none
sha256sum "$OUT" > "$OUT.sha256"
echo "image: $OUT ($(du -h "$OUT" | cut -f1)), $(cut -c1-16 "$OUT.sha256")"
