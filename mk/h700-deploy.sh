#!/bin/sh
# Put a PLATFORM=h700 build on an H700 running BaseOS, over adb (the USB-A to
# USB-C cable attached at power-on: BaseOS's port picks host role otherwise).
#
#   mk/h700-deploy.sh elf       the launcher; the launch loop restarts it
#   mk/h700-deploy.sh diatom    the resident emulator (../diatom, PORT=h700,
#                               or DIATOM_ELF); restarted with the launcher
#   mk/h700-deploy.sh muse      Muse, musectl and btplayer, with the FFmpeg libraries
#   mk/h700-deploy.sh all       launcher, SDL2 and FFmpeg libraries, USB audio modules, launch
#                               script, Splore's wget shim, Muse and diatom; the frontend session restarts to run it
#
#   mk/h700-deploy.sh ds        the Nintendo DS shelf: DSperate (the Brick's build, mk/build-dsperate-brick.sh)
#                               and its systems.cfg row, once; the launcher restarts to show it
#
# More than one device on adb: ANDROID_SERIAL=<serial>.
#
# The first `all` on a card that ran plorpOS under ROCKNIX moves that build's
# tortos.elf, diatom, muse and musectl to plorpOS/.rocknix/ - they cannot run on BaseOS, and
# moving rather than deleting keeps the way back.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
MODE=${1:-elf}
CARD=/mnt/SDCARD
ELF=$ROOT/build/h700/tortos.elf
LIB=$ROOT/sysroot-h700/usr/lib

DIATOM=${DIATOM_ELF:-$ROOT/../diatom/build/h700/diatom}

adb get-state > /dev/null 2>&1 || { echo "no device over adb" >&2; exit 1; }
[ -f "$ELF" ] || { echo "no $ELF; run: make PLATFORM=h700" >&2; exit 1; }
case $MODE in muse|all)
	for f in muse btplayer pico8sdl.so; do
		[ -f "$ROOT/build/h700/$f" ] || { echo "no build/h700/$f; run: make PLATFORM=h700" >&2; exit 1; }
	done ;;
esac
case $MODE in diatom|all)
	[ -f "$DIATOM" ] || { echo "no diatom at $DIATOM (set DIATOM_ELF)" >&2; exit 1; } ;;
esac

if [ "$MODE" = ds ]; then
	CORE=$ROOT/vendor/cores-brick/dsperate_libretro.so
	[ -f "$CORE" ] || { echo "no $CORE; run mk/build-dsperate-brick.sh" >&2; exit 1; }
	adb push "$CORE" "$CARD/plorpOS/cores/dsperate_libretro.so.new" > /dev/null
	adb shell "mv -f $CARD/plorpOS/cores/dsperate_libretro.so.new $CARD/plorpOS/cores/dsperate_libretro.so; mkdir -p '$CARD/Roms/Nintendo DS' '$CARD/Saves/Nintendo DS'"
	# Through a file: a pipe into `adb shell cat` can wait for an EOF that never comes.
	ROWS=$(mktemp)
	grep '^sys|Nintendo DS|' "$ROOT/config/systems-rotate.cfg" > "$ROWS"
	adb push "$ROWS" /tmp/rows.cfg > /dev/null
	adb shell "grep -q '^sys|Nintendo DS|' $CARD/plorpOS/systems.cfg || cat /tmp/rows.cfg >> $CARD/plorpOS/systems.cfg; rm -f /tmp/rows.cfg"
	rm -f "$ROWS"
	echo "deployed DSperate $(md5sum "$CORE" | cut -c1-8) and the DS row; the launcher reads it at its next start"
	exit 0
fi

if [ "$MODE" = all ]; then
	adb shell "cd $CARD/plorpOS && [ -d .rocknix ] || { mkdir .rocknix &&
		for f in tortos.elf diatom muse musectl; do [ -f \$f ] && mv \$f .rocknix/; done; true; }"
	adb shell "mkdir -p $CARD/plorpOS/lib $CARD/System"
	# Libraries go in as .new and are renamed over: a running launcher or Muse
	# keeps the file it mapped. Written in place, the one in use changed under
	# it - Muse died with SIGILL that way (plorpos-9rf.8).
	for l in libSDL2-2.0.so.0 libSDL2_image-2.0.so.0 libSDL2_ttf-2.0.so.0; do
		adb push "$(readlink -f "$LIB/$l")" "$CARD/plorpOS/lib/$l.new" > /dev/null
		adb shell "mv -f $CARD/plorpOS/lib/$l.new $CARD/plorpOS/lib/$l"
	done
	adb push "$ROOT/sd/h700/launch_frontend.sh" "$CARD/System/launch_frontend.sh" > /dev/null
	# USB DACs on the RG35XX H / SP: launch_frontend.sh loads these (plorpos-2wl.15).
	adb shell "mkdir -p $CARD/plorpOS/h700/usb-audio"
	for k in "$ROOT/sd/h700/usb-audio/"*.ko; do adb push "$k" "$CARD/plorpOS/h700/usb-audio/" > /dev/null; done
	adb push "$ROOT/sd/h700/radio.sh" "$CARD/plorpOS/radio.sh" > /dev/null
	adb push "$ROOT/sd/tortos/bt-alsa.sh" "$CARD/plorpOS/bt-alsa.sh" > /dev/null
	adb shell "mkdir -p $CARD/plorpOS/pico8"
	adb push "$ROOT/sd/tortos/pico8/wget" "$CARD/plorpOS/pico8/wget" > /dev/null
	adb shell "chmod +x $CARD/plorpOS/pico8/wget"
	# PortMaster's runner and what it runs on (plorpos-2wl.10)
	adb shell "mkdir -p $CARD/plorpOS/h700/portmaster"
	for f in "$ROOT/sd/h700/portmaster/"* "$ROOT/vendor/portmaster/PortMaster.zip" "$ROOT/build/h700/pmtools.squashfs" "$ROOT/build/h700/portcompat.so"; do
		[ -f "$f" ] && adb push "$f" "$CARD/plorpOS/h700/portmaster/" > /dev/null
	done
	if [ -f "$ROOT/build/h700/lib32/libSDL2-2.0.so.0" ]; then
		adb shell "mkdir -p $CARD/plorpOS/lib32"
		adb push "$ROOT/build/h700/lib32/libSDL2-2.0.so.0" "$CARD/plorpOS/lib32/" > /dev/null
	fi
	adb shell "chmod +x $CARD/plorpOS/h700/portmaster/pm-run.sh $CARD/plorpOS/h700/portmaster/retroarch"
fi
case $MODE in muse|all)
	adb shell "mkdir -p $CARD/plorpOS/lib"
	for l in libavformat.so.60 libavcodec.so.60 libavfilter.so.9 libswresample.so.4 libavutil.so.58; do
		adb push "$(readlink -f "$LIB/$l")" "$CARD/plorpOS/lib/$l.new" > /dev/null   # see the SDL loop
		adb shell "mv -f $CARD/plorpOS/lib/$l.new $CARD/plorpOS/lib/$l"
	done
	for t in COPYING.LGPLv3 COPYING.GPLv3 LICENSE.mbedtls; do adb push "$LIB/$t" "$CARD/plorpOS/lib/" > /dev/null; done
	adb shell "rm -f $CARD/plorpOS/lib/COPYING.LGPLv2.1"   # FFmpeg is LGPL 3 now (7ni.1)
	for f in muse musectl btplayer pico8sdl.so; do
		adb push "$ROOT/build/h700/$f" "$CARD/plorpOS/$f.new" > /dev/null
		adb shell "chmod +x $CARD/plorpOS/$f.new && mv -f $CARD/plorpOS/$f.new $CARD/plorpOS/$f"
	done
	echo "deployed muse $(md5sum "$ROOT/build/h700/muse" | cut -c1-8)"
	# Tailscale (plorpos-8ns.7), around a running daemon, which keeps the old
	# binary until it is switched off and on.
	if [ -f "$ROOT/build/h700/tailscaled" ]; then
		adb push "$ROOT/build/h700/tailscaled" "$CARD/plorpOS/tailscaled.new" > /dev/null
		adb shell "chmod +x $CARD/plorpOS/tailscaled.new && mv -f $CARD/plorpOS/tailscaled.new $CARD/plorpOS/tailscaled"
		adb push "$ROOT/sd/tortos/tailscale.sh" "$ROOT/build/h700/LICENSE-tailscale.md" "$CARD/plorpOS/" > /dev/null
		echo "deployed tailscaled"
	fi ;;
esac
[ "$MODE" = muse ] && { adb shell 'killall muse 2>/dev/null; true'; echo "muse restarted on the launcher's next ask"; exit 0; }
case $MODE in diatom|all)
	adb push "$DIATOM" "$CARD/plorpOS/diatom.new" > /dev/null
	adb shell "chmod +x $CARD/plorpOS/diatom.new && mv -f $CARD/plorpOS/diatom.new $CARD/plorpOS/diatom"
	echo "deployed diatom $(md5sum "$DIATOM" | cut -c1-8)" ;;
esac
[ "$MODE" = diatom ] && { adb shell 'killall diatom tortos.elf 2>/dev/null; true'; echo "launcher and diatom restarted"; exit 0; }
# Pushed aside and renamed, so a running launcher is never overwritten in place.
adb push "$ELF" "$CARD/plorpOS/tortos.elf.new" > /dev/null
adb shell "chmod +x $CARD/plorpOS/tortos.elf.new && mv -f $CARD/plorpOS/tortos.elf.new $CARD/plorpOS/tortos.elf && sync"
echo "deployed $(md5sum "$ELF" | cut -c1-8) ($MODE)"

if [ "$MODE" = all ]; then
	# A new launch script only runs in a new session: end this one (the
	# shell running System/launch_frontend.sh, found by its command line) and
	# init respawns frontend-session.
	adb shell 'for p in /proc/[0-9]*; do
		case "$(tr "\0" " " < $p/cmdline 2>/dev/null)" in
		"/bin/sh /mnt/sdcard/System/launch_frontend.sh"*) kill ${p#/proc/};;
		esac; done; killall tortos.elf 2>/dev/null; true'
	echo "frontend session restarted"
else
	adb shell 'killall tortos.elf 2>/dev/null; true'
	echo "launcher restarted"
fi
