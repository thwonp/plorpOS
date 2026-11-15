#!/bin/sh
# The Anbernic RG DS card image (plorpos-rgds.6): a ROCKNIX release image,
# trimmed and carrying plorpOS. No device and no root needed.
#
#   p1  FAT   "ROCKNIX"  kernel, device trees, the trimmed SYSTEM squashfs,
#                        and plorpos-payload.tar (out/rgds/card)
#   p2  ext4  "STORAGE"  ROCKNIX's own empty storage, as it ships it
#   p3  exFAT "PLORPOS"  made on first boot by sd/rgds/image/fs-resize, which
#                        unpacks the payload onto it: Roms, Bios, Saves, plorpOS
#
# What changes in ROCKNIX's SYSTEM:
#   - EmulationStation, its themes, RetroArch, every libretro core and
#     standalone emulator, Wine, and the cloud-sync extras are removed
#     (STRIP below). Networking, Bluetooth, Samba, ssh, box64/box86 and the
#     32-bit libraries stay.
#   - /usr/lib/rocknix/fs-resize is sd/rgds/image/fs-resize.
#   - InputPlumber's RG DS map sends Home/Back as a keyboard F1 (ROCKNIX
#     20261101 sends an invalid gamepad button, so the key did nothing).
#   - plorpOS's three service drop-ins (sd/rgds/system.d) are built in.
# and on p1: extlinux.conf names the RG DS's device tree.
#
# ROCKNIX's parts stay under ROCKNIX's terms (see its README); plorpOS is a
# separate work on its own partition.
#
# Usage: mk/rgds-image.sh <ROCKNIX-RK3566...-Specific.img> [out.img]
#        W=<dir> for the work directory (needs ~6 GB; not a tmpfs)
#        run `make PLATFORM=rgds` and `PLATFORM=rgds mk/payload.sh` first
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=${1:?usage: mk/rgds-image.sh <rocknix.img> [out.img]}
OUT=${2:-$ROOT/out/plorpOS-rgds.img}
W=${W:-$ROOT/out/rgds-image}
CARD=$ROOT/out/rgds/card
IMAGE=plorpos-rgds-image
FDT=rk3568-anbernic-rg-ds.dtb
P1_START=32768            # sectors: ROCKNIX's own, after the loaders
P1_MIB=1024
P2_SECTORS=65536

STRIP='
usr/lib/libretro usr/lib32/libretro usr/lib/wine usr/share/wine usr/lib/touchHLE
usr/bin/emulationstation usr/bin/retroarch usr/bin/retroarch32
usr/bin/scummvm usr/bin/duckstation-sa usr/bin/flycast usr/bin/azahar usr/bin/vita3k-sa
usr/bin/ppsspp usr/bin/hatarisa usr/bin/amiberry usr/bin/touchHLE usr/bin/gzdoom
usr/bin/dolphin-emu usr/bin/dolphin-emu-nogui usr/bin/mednafen usr/bin/dsperate
usr/bin/melonDS usr/bin/yabasanshiro usr/bin/x128 usr/bin/xscpu64 usr/bin/x64sc
usr/bin/rclone usr/bin/syncthing usr/sbin/tailscale usr/sbin/tailscaled
usr/share/scummvm usr/share/armsx2-sa usr/share/themes usr/share/slang-shaders
usr/share/glsl-shaders usr/share/common-shaders usr/share/retroarch-assets
usr/share/retroarch-overlays usr/share/retroarch usr/share/vice usr/share/daphne
usr/share/glmark2
usr/config/scummvm usr/config/ppsspp usr/config/drastic usr/config/gzdoom
usr/config/emulationstation usr/config/amiberry usr/config/vita3k usr/config/dolphin-emu
'

[ -f "$SRC" ] || { echo "no image at $SRC" >&2; exit 1; }
[ -f "$CARD/plorpOS/tortos.elf" ] || { echo "no payload; run PLATFORM=rgds mk/payload.sh" >&2; exit 1; }
docker image inspect "$IMAGE" > /dev/null 2>&1 ||
	docker build -q -f "$ROOT/mk/rgds-image.Dockerfile" -t "$IMAGE" "$ROOT/mk" > /dev/null
for t in mcopy mformat sfdisk; do command -v $t > /dev/null || { echo "need $t" >&2; exit 1; }; done

rm -rf "$W"; mkdir -p "$W/p1" "$W/in"
P1="$SRC@@$((P1_START * 512))"

echo "unpacking ROCKNIX"
mcopy -s -n -i "$P1" ::/KERNEL ::/device_trees ::/overlays ::/extlinux "$W/p1/"
mcopy -n -i "$P1" ::/SYSTEM "$W/in/SYSTEM"
[ -f "$W/p1/device_trees/$FDT" ] || { echo "$FDT is not in this image" >&2; exit 1; }
sed -i "s|^\( *FDT \).*|\1/device_trees/$FDT|" "$W/p1/extlinux/extlinux.conf"
grep -q "/device_trees/$FDT" "$W/p1/extlinux/extlinux.conf"

echo "trimming SYSTEM (a few minutes)"
cp "$ROOT/sd/rgds/image/fs-resize" "$W/in/fs-resize"
mkdir -p "$W/in/system.d"
for u in essway input rocknix-automount; do
	cp -R "$ROOT/sd/rgds/system.d/$u.service.d" "$W/in/system.d/"
done
docker run --rm -v "$W:/w:z" -e STRIP="$STRIP" "$IMAGE" sh -euc '
	unsquashfs -q -n -d /w/root /w/in/SYSTEM
	cd /w/root
	for p in $STRIP; do
		[ -e "$p" ] || [ -L "$p" ] || { echo "strip: no $p in this ROCKNIX" >&2; continue; }
		rm -rf "$p"
	done
	# The overlay mounts over what was just removed (RetroArch assets, cores,
	# shaders, overlays): each failed at boot, in red, on the screen.
	for u in assets cores shaders overlays; do
		find usr/lib/systemd usr/lib32/systemd \( -name "tmp-$u.mount" \) -exec rm -f {} +
	done
	install -m 755 /w/in/fs-resize usr/lib/rocknix/fs-resize
	M=usr/share/inputplumber/capability_maps/anbernic_rgds.yaml
	sed -i "/event_code: BTN_BACK/,/button: KeyF1/{/gamepad:/d;s/        button: KeyF1/      keyboard: KeyF1/}" $M
	grep -q "keyboard: KeyF1" $M || echo "map: Home was not KeyF1 in this ROCKNIX; left as it is" >&2
	for d in /w/in/system.d/*.service.d; do
		mkdir -p usr/lib/systemd/system/$(basename $d)
		install -m 644 $d/plorpos.conf usr/lib/systemd/system/$(basename $d)/
	done
	mksquashfs /w/root /w/p1/SYSTEM -comp zstd -Xcompression-level 19 -b 1M -noappend -no-progress > /dev/null
	rm -rf /w/root
'
( cd "$W/p1" && md5sum SYSTEM | sed 's|  SYSTEM|  target/SYSTEM|' > SYSTEM.md5
  md5sum KERNEL | sed 's|  KERNEL|  target/KERNEL|' > KERNEL.md5 )

echo "packing the payload"
tar -cf "$W/p1/plorpos-payload.tar" -C "$CARD" .

echo "building the image"
truncate -s ${P1_MIB}M "$W/p1.img"
mformat -i "$W/p1.img" -F -v ROCKNIX ::
mcopy -s -i "$W/p1.img" "$W/p1"/* ::/
P1_SECTORS=$((P1_MIB * 2048))
P2_START=$((P1_START + P1_SECTORS))
TOTAL=$((P2_START + P2_SECTORS + 2048))
rm -f "$OUT"; mkdir -p "$(dirname "$OUT")"
truncate -s $((TOTAL * 512)) "$OUT"
# The loaders (idbloader at sector 64, u-boot at 16384), then the partitions.
dd if="$SRC" of="$OUT" bs=512 skip=64 seek=64 count=$((P1_START - 64)) conv=notrunc status=none
dd if="$W/p1.img" of="$OUT" bs=1M seek=$((P1_START / 2048)) conv=notrunc status=none
SRC_P2=$(sfdisk -d "$SRC" | sed -n 's/.*img2 : start= *\([0-9]*\),.*/\1/p')
dd if="$SRC" of="$OUT" bs=512 skip="$SRC_P2" seek="$P2_START" count="$P2_SECTORS" conv=notrunc status=none
sfdisk -q "$OUT" << SFD
label: gpt
start=$P1_START, size=$P1_SECTORS, type=EBD0A0A2-B9E5-4433-87C0-68B6B72699C7, name="system", attrs="LegacyBIOSBootable"
start=$P2_START, size=$P2_SECTORS, type=0FC63DAF-8483-4772-8E79-3D69D8477DE4, name="storage"
SFD
rm -rf "$W"
ls -l "$OUT" | awk '{printf "image: %s (%.0f MB)\n", $NF, $5 / 1048576}'
