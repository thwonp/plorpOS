#!/bin/sh
# Tailscale on the MagicX Zero 40 (plorpos-8ns): plorpOS's own tailscaled, the
# CLI built in (/tmp/tailscale, a symlink: the card is FAT). Off on a fresh
# card; the Network Services switch is the "tailscale" setting, which boot.env
# carries to launch.sh. Its state - the sign-in - lives in .userdata/shared, so
# it survives a reinstall of plorpOS/.
#
# No firewall rules (netfilter off): tailscaled filters what reaches the device
# itself. `up` waits for the sign-in, then exits; after the first one the daemon
# signs back in by itself and `up` returns at once.
D=/mnt/SDCARD/plorpOS
S=/mnt/SDCARD/.userdata/shared/tailscale
SOCK=/tmp/tailscaled.sock
PID=/tmp/tailscaled.pid
UP=/tmp/tailscale-up.pid
T=/tmp/tailscale

case "$1" in
start)
	[ -f "$PID" ] && kill -0 "$(cat "$PID")" 2> /dev/null && exit 0
	mkdir -p "$S"
	ln -sf "$D/tailscaled" "$T"
	# oakMOSS trusts no certificate authority: plorpOS's bundle.
	SSL_CERT_FILE=$D/cacert.pem start-stop-daemon -S -b -m -p "$PID" -x "$D/tailscaled" -- \
		--statedir="$S" --socket="$SOCK" > /dev/null 2>&1
	(
		i=0
		while [ ! -S "$SOCK" ] && [ $i -lt 50 ]; do sleep 0.2; i=$((i + 1)); done
		"$T" --socket="$SOCK" up --hostname=plorpos-zero40 --accept-routes \
			--netfilter-mode=off > /dev/null 2>&1 &
		echo $! > "$UP"
	) &
	;;
stop)
	[ -f "$UP" ] && kill "$(cat "$UP")" 2> /dev/null
	if [ -f "$PID" ]; then
		P=$(cat "$PID")
		kill "$P" 2> /dev/null
		# It takes its interface, routes and DNS down on the way out.
		i=0
		while kill -0 "$P" 2> /dev/null && [ $i -lt 25 ]; do sleep 0.2; i=$((i + 1)); done
	fi
	rm -f "$PID" "$UP"
	;;
esac
exit 0
