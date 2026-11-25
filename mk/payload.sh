#!/bin/sh
# Assemble the installable payload and its release zip.
#
# Brick (the default): out/sd/. Copy the CONTENTS of out/sd/ to the root of a
# FAT32 SD card, put it in a stock Brick or Brick Hammer and power on: the first
# boot installs the runtrimui.sh hook and every boot after that comes straight
# up in TortOS.
#
# GKD (PLATFORM=gkd): out/gkd/, the two folders docs/install-gkd.md installs -
# copy_to_sd/ (plorpOS/ and the card's empty Roms/, Music/, Audiobooks/, Bios/,
# Saves/) for the card and system.d/ for the GKD's storage - and that guide as
# INSTALL.md. No boot hook: ROCKNIX owns the GKD's boot.
#
# H700 (PLATFORM=h700): out/h700/, the TF2 card's root as docs/install-h700.md
# describes it - System/launch_frontend.sh (BaseOS's frontend hook), plorpOS/
# with the SDL2 and FFmpeg libraries BaseOS lacks in plorpOS/lib/, and the empty
# folders - plus that guide as INSTALL.md. BaseOS itself is the user's, on TF1.
#
# GKD on BasedOS (PLATFORM=gkdb, plorpos-gkd.94): out/gkdb/, the card's root -
# System/launch_frontend.sh (BasedOS's frontend hook), plorpOS/ with Muse's
# FFmpeg in plorpOS/lib/, and the empty folders. Asset gkdb, never gkd: ROCKNIX
# GKDs download gkd.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/mk/plorpos-files.sh"
PLATFORM=${PLATFORM:-brick}
if [ "$PLATFORM" = gkd ]; then
	B=$ROOT/build/gkd
	OUT=$ROOT/out/gkd
	CARD=$OUT/copy_to_sd
	PORT=gkd
elif [ "$PLATFORM" = rgds ]; then
	# The RG DS (plorpos-rgds): out/rgds/card is what mk/rgds-image.sh packs
	# onto the image's exFAT partition, and its zip is the update over Wi-Fi
	# (plorpos-koo) - or by hand, onto that partition.
	B=$ROOT/build/rgds
	OUT=$ROOT/out/rgds
	CARD=$OUT/card
	PORT=rgds
elif [ "$PLATFORM" = zero40 ]; then
	# The MagicX Zero 40 on oakMOSS (plorpos-auv): the zip's contents go on the
	# card that hosts - SD1's SPRUCEOS partition or SD2.
	B=$ROOT/build/zero40
	OUT=$ROOT/out/zero40
	CARD=$OUT
	PORT=zero40
elif [ "$PLATFORM" = h700 ]; then
	B=$ROOT/build/h700
	OUT=$ROOT/out/h700
	CARD=$OUT
	PORT=h700
elif [ "$PLATFORM" = gkdb ]; then
	B=$ROOT/build/gkdb
	OUT=$ROOT/out/gkdb
	CARD=$OUT
	PORT=gkdb
else
	B=$ROOT/build
	OUT=$ROOT/out/sd
	CARD=$OUT
	PORT=brick
fi
P=$CARD/plorpOS
# The version the launcher was built with (the Makefile's stamp), so the zip's
# name and the About page agree. The update check reads the version from the
# name: plorpOS-v<version>-<port>.zip (docs/building.md, "Releases").
VERSION=${VERSION:-$(cat "$B/version" 2>/dev/null)}
[ -n "$VERSION" ] || { echo "no $B/version; run make PLATFORM=$PLATFORM first"; exit 1; }
ZIP=$ROOT/out/plorpOS-v$VERSION-$PORT.zip

[ -f "$B/tortos.elf" ] || { echo "run make PLATFORM=$PLATFORM first"; exit 1; }
# A release ships with the ScreenScraper developer pair built in, or box art
# quietly falls back to libretro for everyone who installs it. A build without
# one is fine for development; it is not fine to package. The pair comes from
# .screenscraper.env (see the Makefile). ALLOW_NO_SS=1 packages one anyway.
if grep -q '^#define SS_DEVID *""' "$B/ss_creds.h" 2>/dev/null ||
   [ ! -f "$B/ss_creds.h" ]; then
	if [ "${ALLOW_NO_SS:-0}" != 1 ]; then
		echo "payload: $B/tortos.elf has no ScreenScraper developer pair." >&2
		echo "payload: add .screenscraper.env, rebuild, or set ALLOW_NO_SS=1" >&2
		exit 1
	fi
	echo "payload: WARNING: packaging without the ScreenScraper developer pair" >&2
fi
# The GKD on BasedOS runs diatom's GKD build as it is: the same device, and it
# draws through SDL, KMSDRM there instead of sway (plorpos-gkd.94.14).
DPORT=$PLATFORM; [ "$PLATFORM" = gkdb ] && DPORT=gkd
DIATOM_ELF=${DIATOM_ELF:-$ROOT/../diatom/build/$DPORT/diatom}
[ -f "$DIATOM_ELF" ] || { echo "no diatom at $DIATOM_ELF (set DIATOM_ELF)"; exit 1; }
[ -f "$ROOT/vendor/cores/fceumm_libretro.so" ] || { echo "run mk/fetch-vendor.sh first"; exit 1; }

rm -rf "$OUT"
mkdir -p "$P/cards" "$P/cores" "$P/res/web" \
         "$CARD/Roms" "$CARD/Music" "$CARD/Audiobooks" "$CARD/Bios" "$CARD/Saves"

cp "$B/tortos.elf" "$P/"
cp "$ROOT/sd/tortos/folder-move.sh" "$P/"   # TortOS/ -> plorpOS/ on an updated card (plorpos-nzi)
cp "$B/muse" "$P/"                        # the audio player's engine; Muse cannot play without it
if [ "$PLATFORM" = gkd ]; then
	cp "$B/musectl" "$P/"
	cp "$ROOT/sd/gkd/launch.sh" "$P/"
	# Drop-ins from before the rename start TortOS/launch.sh (plorpos-nzi).
	mkdir -p "$CARD/TortOS"
	cp "$ROOT/sd/gkd/tortos-launch.sh" "$CARD/TortOS/launch.sh"
	# The three systemd drop-ins, folders and all: each .conf only works
	# inside its <service>.service.d/.
	cp -R "$ROOT/sd/gkd/system.d" "$OUT/"
	# ROCKNIX's boot touches an empty roms/pico-8/Splore.png (for its own
	# EmulationStation) unless this file is there, and the shelf would list
	# it as a cart. It does nothing to plorpOS's Splore (plorpos-gkd.32.8).
	mkdir -p "$CARD/Roms/Pico-8"
	touch "$CARD/Roms/Pico-8/.disable_splore"
	# PortMaster's runner and PortMaster itself, unpacked on first use
	# (plorpos-46o.5; mk/fetch-vendor.sh).
	[ -f "$ROOT/vendor/portmaster/PortMaster.zip" ] ||
		{ echo "no vendor/portmaster/PortMaster.zip; run mk/fetch-vendor.sh" >&2; exit 1; }
	mkdir -p "$P/gkd/portmaster"
	cp "$ROOT/sd/gkd/portmaster/"* "$ROOT/vendor/portmaster/PortMaster.zip" "$P/gkd/portmaster/"
	cp "$B/portfullscreen.so" "$P/gkd/portmaster/"
	cp "$ROOT/docs/install-gkd.md" "$OUT/INSTALL.md"
elif [ "$PLATFORM" = rgds ]; then
	cp "$B/musectl" "$P/"
	cp "$ROOT/sd/rgds/launch.sh" "$P/"
	# PortMaster: the GKD's runner (the same ROCKNIX) with the RG DS's pad.
	[ -f "$ROOT/vendor/portmaster/PortMaster.zip" ] ||
		{ echo "no vendor/portmaster/PortMaster.zip; run mk/fetch-vendor.sh" >&2; exit 1; }
	mkdir -p "$P/rgds/portmaster"
	cp "$ROOT/sd/gkd/portmaster/pm-run.sh" "$ROOT/sd/rgds/portmaster/control.txt" \
	   "$ROOT/vendor/portmaster/PortMaster.zip" "$B/portfullscreen.so" "$P/rgds/portmaster/"
	# adb (plorpos-rgds.11): ROCKNIX has no adbd, so Debian's rides along.
	[ -f "$ROOT/vendor/adbd-rgds/adbd" ] ||
		{ echo "no vendor/adbd-rgds; run mk/fetch-rgds-adbd.sh" >&2; exit 1; }
	mkdir -p "$P/rgds/adb"
	cp -R "$ROOT/vendor/adbd-rgds/"* "$ROOT/sd/rgds/adb/adb.sh" "$B/adbroot.so" "$P/rgds/adb/"
	mkdir -p "$CARD/Roms/Pico-8"
	touch "$CARD/Roms/Pico-8/.disable_splore"
elif [ "$PLATFORM" = zero40 ]; then
	cp "$B/musectl" "$P/"
	cp "$ROOT/sd/zero40/launch.sh" "$ROOT/sd/zero40/radio.sh" "$ROOT/sd/zero40/ssh.sh" "$P/"
	[ -f "$B/dropbearmulti" ] || { echo "no $B/dropbearmulti; run mk/build-dropbear-zero40.sh" >&2; exit 1; }
	cp "$B/dropbearmulti" "$P/"           # ssh: oakMOSS has no server (sd/zero40/ssh.sh)
	[ -f "$B/curl" ] || { echo "no $B/curl; run mk/build-curl-zero40.sh" >&2; exit 1; }
	cp "$B/curl" "$P/"                    # oakMOSS has no curl: net.c finds this one beside cacert.pem
	mkdir -p "$OUT/.tmp_update"
	cp "$ROOT/sd/zero40/updater" "$OUT/.tmp_update/"   # oakMOSS's hand-off (runmagicx.sh)
	# Muse's FFmpeg: oakMOSS has none (TrimUI's firmware carries the Brick's).
	# The H700's own build (mk/fetch-h700-sysroot.sh: from source, glibc 2.17
	# symbols, no network), the sonames Muse links against.
	mkdir -p "$P/lib"
	L=$ROOT/sysroot-h700/usr/lib
	for l in libavformat.so.60 libavcodec.so.60 libavfilter.so.9 libswresample.so.4 libavutil.so.58; do
		cp "$(readlink -f "$L/$l")" "$P/lib/$l" ||
			{ echo "payload: no $l; run mk/fetch-h700-sysroot.sh" >&2; exit 1; }
	done
	cp "$L/COPYING.LGPLv3" "$L/COPYING.GPLv3" "$L/LICENSE.mbedtls" "$P/lib/"   # FFmpeg LGPL 3 + Mbed TLS (7ni.1)
	[ -f "$ROOT/docs/install-zero40.md" ] && cp "$ROOT/docs/install-zero40.md" "$OUT/INSTALL.md"
elif [ "$PLATFORM" = gkdb ]; then
	cp "$B/musectl" "$P/"
	mkdir -p "$OUT/System" "$P/gkdb" "$P/lib" "$P/pico8"
	cp "$ROOT/sd/gkdb/launch_frontend.sh" "$OUT/System/"
	cp "$ROOT/sd/tortos/pico8/wget" "$P/pico8/"  # Splore's downloads over HTTPS: stock's curl (the shim says why)
	# PortMaster: the H700's runner on BasedOS (plorpos-gkd.94.15), with the
	# H700's python image and port shims as they are (mk/build-pmtools-h700.sh,
	# tools/portcompat.c); BasedOS brings its own SDL2, 64- and 32-bit.
	[ -f "$ROOT/vendor/portmaster/PortMaster.zip" ] ||
		{ echo "no vendor/portmaster/PortMaster.zip; run mk/fetch-vendor.sh" >&2; exit 1; }
	[ -f "$B/portscale.so" ] || { echo "no $B/portscale.so; run make PLATFORM=gkdb" >&2; exit 1; }
	for f in pmtools.squashfs portcompat.so; do
		[ -f "$ROOT/build/h700/$f" ] ||
			{ echo "no build/h700/$f; run mk/build-pmtools-h700.sh (and make PLATFORM=h700)" >&2; exit 1; }
	done
	mkdir -p "$P/gkdb/portmaster"
	cp "$ROOT/sd/gkdb/portmaster/"* "$ROOT/vendor/portmaster/PortMaster.zip" \
	   "$ROOT/build/h700/pmtools.squashfs" "$ROOT/build/h700/portcompat.so" "$B/portscale.so" "$P/gkdb/portmaster/"
	# Its libgl_ROCKNIX.txt is PortMaster's own (sd/gkdb/portmaster): gl4es on
	# GBM (LIBGL_FB=4), as the GKD's libmali needs; the H700's drops it for fbdev.
	cp "$ROOT/sd/h700/portmaster/launch_hooks.txt" "$ROOT/sd/h700/portmaster/retroarch" "$P/gkdb/portmaster/"
	cp "$ROOT/sd/gkdb/asound.conf" "$P/gkdb/"   # ALSA's default on PipeWire (the launch script's ALSA_CONFIG_PATH)
	# The boot logos, put on the eMMC by the launch script (plorpos-gkd.94.26):
	# U-Boot's, and BasedOS's with the boot keys labelled
	python3 "$ROOT/mk/gkdb-bootlogo.py" "$ROOT/res/boot/plorpos-wordmark.png" "$ROOT/res/fonts/menu.ttf" \
		"$P/gkdb/bootlogo.bmp.gz" || exit 1
	python3 "$ROOT/mk/gkdb-bootlogo.py" "$ROOT/res/boot/plorpos-wordmark.png" "$ROOT/res/fonts/menu.ttf" \
		"$P/gkdb/bootlogo-keys.bmp.gz" --keys || exit 1
	# Muse's FFmpeg: BasedOS has none. The H700's own build, as on the Zero 40.
	L=$ROOT/sysroot-h700/usr/lib
	for l in libavformat.so.60 libavcodec.so.60 libavfilter.so.9 libswresample.so.4 libavutil.so.58; do
		cp "$(readlink -f "$L/$l")" "$P/lib/$l" ||
			{ echo "payload: no $l; run mk/fetch-h700-sysroot.sh" >&2; exit 1; }
	done
	cp "$L/COPYING.LGPLv3" "$L/COPYING.GPLv3" "$L/LICENSE.mbedtls" "$P/lib/"   # FFmpeg LGPL 3 + Mbed TLS (7ni.1)
elif [ "$PLATFORM" = h700 ]; then
	cp "$B/musectl" "$P/"
	cp "$B/btplayer" "$P/"                # a headset's volume reports and buttons (tools/btplayer.c)
	cp "$B/pico8sdl.so" "$P/"             # native PICO-8 quiet while Muse plays (tools/pico8sdl.c)
	mkdir -p "$OUT/System" "$P/lib" "$P/pico8"
	cp "$ROOT/sd/h700/launch_frontend.sh" "$OUT/System/"
	cp "$ROOT/sd/h700/radio.sh" "$P/"     # Bluetooth: sourced by launch_frontend.sh AND by plat_sleep()
	cp "$ROOT/sd/tortos/bt-alsa.sh" "$P/" # one ALSA PCM per bonded headset, as on the Brick
	cp "$ROOT/sd/tortos/pico8/wget" "$P/pico8/"  # Splore's downloads over HTTPS (the shim says why)
	# PortMaster's runner, PortMaster itself (unpacked on first use) and the
	# python and bash it needs (plorpos-2wl.10; mk/fetch-vendor.sh,
	# mk/build-pmtools-h700.sh).
	[ -f "$ROOT/vendor/portmaster/PortMaster.zip" ] ||
		{ echo "no vendor/portmaster/PortMaster.zip; run mk/fetch-vendor.sh" >&2; exit 1; }
	[ -f "$B/pmtools.squashfs" ] ||
		{ echo "no $B/pmtools.squashfs; run mk/build-pmtools-h700.sh" >&2; exit 1; }
	[ -f "$B/lib32/libSDL2-2.0.so.0" ] ||
		{ echo "no $B/lib32/libSDL2-2.0.so.0; run mk/build-sdl2-armhf.sh" >&2; exit 1; }
	mkdir -p "$P/lib32"
	cp "$B/lib32/libSDL2-2.0.so.0" "$P/lib32/"   # SDL2 for 32-bit ports (mk/build-sdl2-armhf.sh)
	mkdir -p "$P/h700/portmaster"
	cp "$ROOT/sd/h700/portmaster/"* "$ROOT/vendor/portmaster/PortMaster.zip" "$B/pmtools.squashfs" "$B/portcompat.so" "$P/h700/portmaster/"
	mkdir -p "$P/h700/usb-audio"   # USB DACs on the RG35XX H / SP, loaded by launch_frontend.sh (plorpos-2wl.15)
	cp "$ROOT/sd/h700/usb-audio/"*.ko "$P/h700/usb-audio/"
	# By soname, as the binaries ask for them; FFmpeg's LGPL text beside it
	# (THIRD-PARTY-LICENSES.md).
	L=$ROOT/sysroot-h700/usr/lib
	for l in libSDL2-2.0.so.0 libSDL2_image-2.0.so.0 libSDL2_ttf-2.0.so.0 \
	         libavformat.so.60 libavcodec.so.60 libavfilter.so.9 libswresample.so.4 libavutil.so.58; do
		cp "$(readlink -f "$L/$l")" "$P/lib/$l" ||
			{ echo "payload: no $l; run mk/fetch-h700-sysroot.sh" >&2; exit 1; }
	done
	cp "$L/COPYING.LGPLv3" "$L/COPYING.GPLv3" "$L/LICENSE.mbedtls" "$P/lib/"   # FFmpeg LGPL 3 + Mbed TLS (7ni.1)
	cp "$ROOT/docs/install-h700.md" "$OUT/INSTALL.md"
else
	mkdir -p "$OUT/.tmp_update" "$OUT/trimui/app"
	[ -f "$B/pmtools.squashfs" ] ||
		{ echo "run mk/build-pmtools-brick.sh first" >&2; exit 1; }
	[ -f "$ROOT/vendor/portmaster/PortMaster.zip" ] ||
		{ echo "run mk/fetch-vendor.sh first" >&2; exit 1; }
	mkdir -p "$P/brick/portmaster"
	cp "$ROOT/sd/brick/portmaster/"* "$ROOT/vendor/portmaster/PortMaster.zip" "$B/pmtools.squashfs" "$P/brick/portmaster/"
	cp "$B/setbright" "$P/"               # brightness before the boot animation
	cp "$B/btplayer" "$P/"                # lets a headset's volume through BlueZ
	cp "$B/pico8sdl.so" "$P/"             # native PICO-8 on the firmware SDL (tools/pico8sdl.c)
	mkdir -p "$P/pico8"
	cp "$ROOT/sd/tortos/pico8/wget" "$P/pico8/"  # Splore's downloads over HTTPS (the shim says why)
	# Muse's FFmpeg with TLS (plorpos-5eq.2): the firmware's has none, so a
	# podcast's https episode could not stream. The H700's own build, as on
	# the Zero 40; muse finds it by its rpath (mk/cross.mk).
	mkdir -p "$P/lib"
	L=$ROOT/sysroot-h700/usr/lib
	for l in libavformat.so.60 libavcodec.so.60 libavfilter.so.9 libswresample.so.4 libavutil.so.58; do
		cp "$(readlink -f "$L/$l")" "$P/lib/$l" ||
			{ echo "payload: no $l; run mk/fetch-h700-sysroot.sh" >&2; exit 1; }
	done
	cp "$L/COPYING.LGPLv3" "$L/COPYING.GPLv3" "$L/LICENSE.mbedtls" "$P/lib/"   # FFmpeg LGPL 3 + Mbed TLS
	cp "$ROOT/sd/tortos/launch.sh" "$P/"
	plorpos_unzip "$P" || exit 1            # the firmware has none (plorpos-koo)
	cp "$ROOT/sd/tortos/bt-alsa.sh" "$P/" # sourced by launch.sh, run by the launcher
	cp "$ROOT/sd/tortos/radio.sh" "$P/"   # sourced by launch.sh AND by plat_sleep()
	cp "$ROOT/res/boot/tortos-boot.mp4" "$P/"
	cp "$ROOT/res/boot/bootlogo.bmp" "$P/"    # u-boot splash, applied on first boot
	cp "$ROOT/res/boot/splash.png" "$P/"      # the pic2fb loading splash, likewise
	cp "$ROOT/docs/install-brick.md" "$OUT/INSTALL.md"  # lands on the card root; harmless
fi
# Only systems.cfg is shipped now. tortos.cfg, turbo.cfg and coreopts.cfg are
# compiled into the launcher and seed the settings database on first run, so
# there is no file to ship and none to drift from the code that reads it.
#
# systems.cfg stays a file because it is BUILD input, not a setting: the check
# below reads it to refuse a card whose cores are missing, and the loop further
# down reads it to create the ROM folders. Both run on the host, before any
# database exists.
#
# Both devices ship every system since plorpos-reo: the Brick dropped fbneo,
# pcsx_rearmed and fake08 for v1.01 and v1.1 (plorpos-gkd.84).
cp "$ROOT/config/systems.cfg" "$P/"
# The H700 has the Rotate's Ports shelf (plorpos-2wl.10) and its DS one, on the
# Brick's build of DSperate (plorpos-reo.21.7).
[ "$PLATFORM" = h700 ] && grep -E '^sys\|(Nintendo DS|Ports)\|' "$ROOT/config/systems-rotate.cfg" >> "$P/systems.cfg"
# The Brick also has the DS (plorpos-reo.21): its own build of DSperate, one
# screen at a time (mk/build-dsperate-brick.sh).
# The Zero 40 has the DS shelf on the same DSperate build; no Ports yet.
[ "$PLATFORM" = zero40 ] && grep '^sys|Nintendo DS|' "$ROOT/config/systems-rotate.cfg" >> "$P/systems.cfg"
# The GKD on BasedOS: the DS and Ports shelves (plorpos-gkd.94.15).
[ "$PLATFORM" = gkdb ] && grep -E '^sys\|(Nintendo DS|Ports)\|' "$ROOT/config/systems-rotate.cfg" >> "$P/systems.cfg"
if [ "$PLATFORM" = brick ]; then
	grep '^sys|Nintendo DS|' "$ROOT/config/systems-rotate.cfg" >> "$P/systems.cfg"
	grep '^sys|Ports|' "$ROOT/config/systems-rotate.cfg" >> "$P/systems.cfg"
fi
# Everything else in plorpOS/ that every device ships, cores included (one
# list with the Rotate's card, plorpos-3yv.89). The GKD has the Rotate's DS
# and Ports shelves too, its own builds of their cores first, and PokeDaisy DS
# for the DS shelf's Core row (plorpos-oe0.44).
if [ "$PLATFORM" = rgds ]; then
	# The DS and Ports shelves, as the GKD (plorpos-rgds.9).
	cat "$ROOT/config/systems-rotate.cfg" >> "$P/systems.cfg"
	plorpos_files "$P" "$ROOT/vendor/cores-gkd $ROOT/vendor/cores $ROOT/vendor/cores-brick" "pokedaisy_gba pokedaisy_gbc pokedaisy_ds mgba"
elif [ "$PLATFORM" = gkdb ]; then
	plorpos_files "$P" "$ROOT/vendor/cores-gkd $ROOT/vendor/cores $ROOT/vendor/cores-brick" "pokedaisy_gba pokedaisy_gbc pokedaisy_ds mgba"
elif [ "$PLATFORM" = gkd ]; then
	cat "$ROOT/config/systems-rotate.cfg" >> "$P/systems.cfg"
	plorpos_files "$P" "$ROOT/vendor/cores-gkd $ROOT/vendor/cores $ROOT/vendor/cores-brick" "pokedaisy_gba pokedaisy_gbc pokedaisy_ds mgba"
elif [ "$PLATFORM" = brick ] || [ "$PLATFORM" = zero40 ]; then
	# The Zero 40 is the Brick's CPU and C library family: its cores as they are.
	[ -f "$ROOT/vendor/cores-brick/dsperate_libretro.so" ] ||
		{ echo "run mk/build-dsperate-brick.sh first" >&2; exit 1; }
	plorpos_files "$P" "$ROOT/vendor/cores-brick $ROOT/vendor/cores" "pokedaisy_gba pokedaisy_gbc pokedaisy_ds mgba"
else
	# The H700 loads the Brick's DSperate: GCC 10, glibc 2.17 symbols, BaseOS has 2.35.
	[ "$PLATFORM" != h700 ] || [ -f "$ROOT/vendor/cores-brick/dsperate_libretro.so" ] ||
		{ echo "run mk/build-dsperate-brick.sh first" >&2; exit 1; }
	if [ "$PLATFORM" = h700 ]; then plorpos_files "$P" "$ROOT/vendor/cores-brick $ROOT/vendor/cores" "pokedaisy_gba pokedaisy_gbc pokedaisy_ds mgba"
	else plorpos_files "$P" "$ROOT/vendor/cores"; fi
fi
plorpos_tailscale "$P" "$B" || exit 1
plorpos_syncthing "$P" || exit 1
cp "$DIATOM_ELF" "$P/diatom"


# One ROM folder per system, with the .media folder box art goes in.
plorpos_rom_folders "$P" |
while IFS= read -r folder; do
	mkdir -p "$CARD/Roms/$folder/.media"
done
plorpos_save_folders "$P" |
while IFS= read -r folder; do
	mkdir -p "$CARD/Saves/$folder"
done
# Bios/ is created above and stays FLAT. It is handed to the core as its system
# directory and a core asks for a filename inside it - mgba wants gba_bios.bin,
# mednafen wants syscard3.pce - so a folder per system is a place a BIOS goes to
# be ignored. This line used to read `mkdir -p "$OUT/Bios/GBA"`, directly under a
# comment saying not to litter the card with empty Bios folders, and it shipped
# in v1.0: an empty GBA/ that contradicted the README two directories away.
# Nothing errors when a BIOS lands in it. mgba falls back to its built-in one and
# the only symptom is the boot animation you were trying to enable not appearing.

chmod +x "$P/tortos.elf" "$P/diatom" "$P/muse"
case $PLATFORM in
gkd)  chmod +x "$P/launch.sh" "$P/musectl" "$P/gkd/portmaster/pm-run.sh" ;;
zero40) chmod +x "$P/launch.sh" "$P/musectl" "$P/ssh.sh" "$P/dropbearmulti" "$P/curl" "$OUT/.tmp_update/updater"
      du -sh "$OUT"; echo "payload ready: $OUT"
      rm -f "$ZIP"; ( cd "$OUT" && zip -qr "$ZIP" . -x '.DS_Store' '._*' )
      echo "release zip:  $ZIP  ($(du -h "$ZIP" | cut -f1))"; exit 0 ;;
rgds) chmod +x "$P/launch.sh" "$P/musectl" "$P/rgds/portmaster/pm-run.sh"
      du -sh "$OUT"; echo "payload ready: $CARD"
      rm -f "$ZIP"; ( cd "$CARD" && zip -qr "$ZIP" . -x '.DS_Store' '._*' )
      echo "release zip:  $ZIP  ($(du -h "$ZIP" | cut -f1))"; exit 0 ;;
gkdb) chmod +x "$P/musectl" "$OUT/System/launch_frontend.sh" "$P/pico8/wget" \
	"$P/gkdb/portmaster/pm-run.sh" "$P/gkdb/portmaster/retroarch" ;;
h700) chmod +x "$P/musectl" "$P/btplayer" "$OUT/System/launch_frontend.sh" "$P/pico8/wget" \
	      "$P/h700/portmaster/pm-run.sh" "$P/h700/portmaster/retroarch" ;;
*)    chmod +x "$P/launch.sh" "$P/brick/portmaster/pm-run.sh" "$P/brick/portmaster/retroarch" ;;
esac
if [ "$PLATFORM" = gkd ] || [ "$PLATFORM" = h700 ] || [ "$PLATFORM" = gkdb ]; then
	du -sh "$OUT"
	echo "payload ready: $OUT"
	rm -f "$ZIP"
	( cd "$OUT" && zip -qr "$ZIP" . -x '.DS_Store' '._*' )
	echo "release zip:  $ZIP  ($(du -h "$ZIP" | cut -f1))"
	exit 0
fi

cp "$ROOT/sd/.tmp_update/updater" "$ROOT/sd/.tmp_update/tg3040.sh" "$OUT/.tmp_update/"
cp "$ROOT/sd/trimui/app/MainUI" "$ROOT/sd/trimui/app/runtrimui.sh" "$OUT/trimui/app/"

chmod +x "$OUT/.tmp_update/updater" "$OUT/.tmp_update/tg3040.sh" \
         "$OUT/trimui/app/MainUI" "$OUT/trimui/app/runtrimui.sh" \
         "$P/setbright" "$P/btplayer" "$P/pico8/wget"

du -sh "$OUT"
echo "payload ready: $OUT"

rm -f "$ZIP"
( cd "$OUT" && zip -qr "$ZIP" . -x '.DS_Store' '._*' )
echo "release zip:  $ZIP  ($(du -h "$ZIP" | cut -f1))"
