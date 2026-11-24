#!/bin/sh
# Tailscale on every plorpOS port with Wi-Fi (plorpos-8ns): plorpOS's own
# tailscaled, the CLI built in (/tmp/tailscale, a symlink: cards are FAT or
# exFAT). Off on a fresh card; the Network Services switch is the "tailscale"
# setting, and the launcher calls "start" when it comes up with the switch on
# (ts_boot). The sign-in lives in .userdata/shared on the card, so it survives
# a reinstall of plorpOS/.
#
#   tailscale.sh start HOSTNAME [userspace]
#   tailscale.sh stop
#
# The kernel's TUN everywhere but the GKD, whose vendor kernel has none
# (plorpos-8ns.4): there it is userspace networking, and nothing on the device
# sees the tailnet but what goes through the HTTP proxy on 127.0.0.1:1055 -
# plorpOS's own requests and Muse's streams (ts_proxy). Reaching the device
# from the tailnet (SSH, Samba) works either way.
#
# A transient systemd unit where there is systemd-run: it lives through
# launcher restarts, and systemd stops it at power-off. Otherwise
# start-stop-daemon, or a plain background process.
D=$(cd "$(dirname "$0")" && pwd)
S=$(dirname "$D")/.userdata/shared/tailscale
SOCK=/tmp/tailscaled.sock
PID=/tmp/tailscaled.pid
UP=/tmp/tailscale-up.pid
UNIT=plorpos-tailscaled
T=/tmp/tailscale

running() {
	if command -v systemd-run > /dev/null; then
		systemctl -q is-active "$UNIT"
	else
		[ -f "$PID" ] && kill -0 "$(cat "$PID")" 2> /dev/null
	fi
}

case "$1" in
start)
	running && exit 0
	HOST=$2 MODE=$3
	mkdir -p "$S"
	ln -sf "$D/tailscaled" "$T"
	# Not running, so any socket is a stale one, which would end the wait
	# below before the new daemon listens.
	rm -f "$SOCK"
	set -- --statedir="$S" --socket="$SOCK"
	if [ "$MODE" = userspace ]; then
		set -- "$@" --tun=userspace-networking --outbound-http-proxy-listen=127.0.0.1:1055
	else
		modprobe tun 2> /dev/null   # a module on the RG DS; built in elsewhere
	fi
	# Some of these devices trust no certificate authority: plorpOS's bundle.
	if command -v systemd-run > /dev/null; then
		systemctl reset-failed "$UNIT" 2> /dev/null
		systemd-run -q --unit="$UNIT" --property=Restart=on-failure \
			--setenv=SSL_CERT_FILE="$D/cacert.pem" -- "$D/tailscaled" "$@" > /dev/null 2>&1
	elif command -v start-stop-daemon > /dev/null; then
		SSL_CERT_FILE=$D/cacert.pem start-stop-daemon -S -b -m -p "$PID" \
			-x "$D/tailscaled" -- "$@" > /dev/null 2>&1
	else
		SSL_CERT_FILE=$D/cacert.pem "$D/tailscaled" "$@" > /dev/null 2>&1 < /dev/null &
		echo $! > "$PID"
	fi
	# `up` waits for the sign-in, then exits; after the first one the daemon
	# signs back in by itself and `up` returns at once. No firewall rules
	# (netfilter off): tailscaled filters what reaches the device itself.
	(
		i=0
		while [ ! -S "$SOCK" ] && [ $i -lt 50 ]; do sleep 0.2; i=$((i + 1)); done
		"$T" --socket="$SOCK" up --hostname="$HOST" --accept-routes --netfilter-mode=off \
			> /dev/null 2>&1 &
		echo $! > "$UP"
	) &
	;;
stop)
	[ -f "$UP" ] && kill "$(cat "$UP")" 2> /dev/null
	rm -f "$UP"
	if command -v systemd-run > /dev/null; then
		systemctl stop "$UNIT" 2> /dev/null
	elif [ -f "$PID" ]; then
		P=$(cat "$PID")
		kill "$P" 2> /dev/null
		# It takes its interface, routes and DNS down on the way out.
		i=0
		while kill -0 "$P" 2> /dev/null && [ $i -lt 25 ]; do sleep 0.2; i=$((i + 1)); done
		rm -f "$PID"
	fi
	;;
esac
exit 0
