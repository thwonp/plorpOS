#!/bin/sh
# The Rotate's session (plorpos-3yv): plorpos.service runs this under its own
# D-Bus. PipeWire owns the codec, so Muse and a game share it the GKD's way
# and the sc2730's habit of resetting HPL/HPR Gain at every PCM open happens
# once, not at every track. sway then runs launch.sh (sway.conf).
DIR=$(cd "$(dirname "$0")/.." && pwd)
# Check for update's download goes in before anything in plorpOS/ starts, and
# this script starts again as the update has it (plorpos-koo, ota.sh).
[ -f "${DIR%/*}/.ota/ready" ] && sh "$DIR/ota.sh" apply "${DIR%/*}" && exec /bin/sh "$0" "$@"
export XDG_RUNTIME_DIR=/run/plorpos
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
# base618's device tree, so opening the swivel wakes from deep sleep
# (plorpos-5eq.25), with 15 ms key debounce (plorpos-3yv.116: at 2 ms a
# button bounced on release into a second press), over the ones earlier
# cards shipped - only those exact files: a device tree someone changed is
# theirs. U-Boot reads it at boot, so the launcher says so and restarts once
# (system-updated).
dtb_update() {
	CUR=/boot/dtb/sprd/ums512-rg-rotate.dtb
	NEW=$DIR/rotate/boot/ums512-rg-rotate.dtb
	NEW_MD5=978f10eef851b295d16865e8b07aed6e
	[ -f "$CUR" ] && [ -f "$NEW" ] || return 0
	CUR_MD5=$(md5sum < "$CUR" | cut -c1-32)
	case $CUR_MD5 in
	f71e3340bc8266d88ac6eff107f09c29|ee1a8c4efcfef4270fba3036e79e3b78) ;;
	*) return 0 ;;
	esac
	[ "$(md5sum < "$NEW" | cut -c1-32)" = $NEW_MD5 ] || return 0
	cp "$CUR" "$CUR.bak-$(echo $CUR_MD5 | cut -c1-8)" && cp "$NEW" "$CUR.new" && sync &&
		[ "$(md5sum < "$CUR.new" | cut -c1-32)" = $NEW_MD5 ] &&
		mv -f "$CUR.new" "$CUR" && sync || { rm -f "$CUR.new"; return 0; }
	echo "session: device tree updated (swivel wake, key debounce); restarting once"
	: > "$XDG_RUNTIME_DIR/system-updated"
}
dtb_update
# /run outlives a restart of the service: an old sway's IPC socket would be
# the first swaymsg finds.
rm -f "$XDG_RUNTIME_DIR"/sway-ipc.*.sock
pipewire &
wireplumber &
pipewire-pulse &
exec sway -c "$DIR/rotate/sway.conf"
