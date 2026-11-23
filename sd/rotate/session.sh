#!/bin/sh
# The Rotate's session (plorpos-3yv): plorpos.service runs this under its own
# D-Bus. PipeWire owns the codec, so Muse and a game share it the GKD's way
# and the sc2730's habit of resetting HPL/HPR Gain at every PCM open happens
# once, not at every track. sway then runs launch.sh (sway.conf).
DIR=$(cd "$(dirname "$0")/.." && pwd)
export XDG_RUNTIME_DIR=/run/plorpos
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
# base618's device tree, so opening the swivel wakes from deep sleep
# (plorpos-5eq.25), over the one every card before it shipped - only that
# exact file: a device tree someone changed is theirs. U-Boot reads it at
# boot, so the launcher says so and restarts once (system-updated).
dtb_update() {
	CUR=/boot/dtb/sprd/ums512-rg-rotate.dtb
	NEW=$DIR/rotate/boot/ums512-rg-rotate.dtb
	OLD_MD5=f71e3340bc8266d88ac6eff107f09c29
	NEW_MD5=ee1a8c4efcfef4270fba3036e79e3b78
	[ -f "$CUR" ] && [ -f "$NEW" ] || return 0
	[ "$(md5sum < "$CUR" | cut -c1-32)" = $OLD_MD5 ] || return 0
	[ "$(md5sum < "$NEW" | cut -c1-32)" = $NEW_MD5 ] || return 0
	cp "$CUR" "$CUR.bak-f71e3340" && cp "$NEW" "$CUR.new" && sync &&
		[ "$(md5sum < "$CUR.new" | cut -c1-32)" = $NEW_MD5 ] &&
		mv -f "$CUR.new" "$CUR" && sync || { rm -f "$CUR.new"; return 0; }
	echo "session: device tree updated (swivel wake); restarting once"
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
