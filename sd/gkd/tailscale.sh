#!/bin/sh
# Tailscale on the GKD (plorpos-8ns.5): plorpOS's own tailscaled, the CLI
# built in (/tmp/tailscale, a symlink: the card is exFAT). Off on a fresh
# card; the Network Services switch is the "tailscale" setting, and the
# launcher calls "start" when it comes up with the switch on (ts_boot).
# Its state - the sign-in - lives in .userdata/shared, so it survives a
# reinstall of plorpOS/.
#
# Userspace networking: the vendor kernel has no TUN driver (plorpos-8ns.4).
# Nothing on the device sees the tailnet but what goes through the HTTP
# proxy on 127.0.0.1:1055 - plorpOS's own requests and Muse's streams
# (ts_proxy). Reaching the GKD from the tailnet (SSH, Samba) still works:
# tailscaled hands those connections to the local services.
#
# A transient systemd unit rather than a child of the launcher: it lives
# through launcher restarts, and systemd stops it at power-off. ROCKNIX's own
# tailscaled.service is a separate thing and stays disabled.
D=/storage/games-external/plorpOS
S=/storage/games-external/.userdata/shared/tailscale
SOCK=/tmp/tailscaled.sock
UNIT=plorpos-tailscaled
UP=/tmp/tailscale-up.pid
T=/tmp/tailscale

case "$1" in
start)
	systemctl -q is-active "$UNIT" && exit 0
	mkdir -p "$S"
	ln -sf "$D/tailscaled" "$T"
	systemctl reset-failed "$UNIT" 2> /dev/null
	# Not running, so any socket is a stale one, which would end the wait
	# below before the new daemon listens.
	rm -f "$SOCK"
	systemd-run -q --unit="$UNIT" --property=Restart=on-failure \
		--setenv=SSL_CERT_FILE="$D/cacert.pem" -- "$D/tailscaled" \
		--statedir="$S" --socket="$SOCK" --tun=userspace-networking \
		--outbound-http-proxy-listen=127.0.0.1:1055 > /dev/null 2>&1
	# `up` waits for the sign-in, then exits; after the first one the daemon
	# signs back in by itself and `up` returns at once.
	(
		i=0
		while [ ! -S "$SOCK" ] && [ $i -lt 50 ]; do sleep 0.2; i=$((i + 1)); done
		"$T" --socket="$SOCK" up --hostname=plorpos-gkd --accept-routes > /dev/null 2>&1 &
		echo $! > "$UP"
	) &
	;;
stop)
	[ -f "$UP" ] && kill "$(cat "$UP")" 2> /dev/null
	rm -f "$UP"
	systemctl stop "$UNIT" 2> /dev/null
	;;
esac
exit 0
