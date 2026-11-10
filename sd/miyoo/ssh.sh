#!/bin/sh
# plorpOS's ssh on the Miyoo Mini Flip (plorpos-f1p.62.2): ssh.sh start|stop.
# Run by launch.sh at boot when boot.env says SSH='1', and by the launcher's
# Network Services > SSH switch. Off on a fresh card.
#
# Key login only (-s): put your public key in TortOS/ssh/authorized_keys.
# The card is FAT, where every file reads as world-writable and dropbear
# refuses such a key file, so it is copied to a private folder in /tmp.
# The host key is made on the first start and kept in TortOS/ssh.
D=/mnt/SDCARD/TortOS
K=$D/ssh
PID=/tmp/dropbear.pid
RUN=/tmp/ssh
PORT=${SSH_PORT:-22}
[ "$PORT" = 22 ] || PID=/tmp/dropbear-$PORT.pid

case "$1" in
start)
	[ -f "$PID" ] && kill -0 "$(cat "$PID")" 2> /dev/null && exit 0
	mkdir -p "$K" "$RUN"
	chmod 700 "$RUN"
	[ -f "$K/authorized_keys" ] && cp "$K/authorized_keys" "$RUN/authorized_keys"
	chmod 600 "$RUN/authorized_keys" 2> /dev/null
	[ -s "$K/dropbear_ed25519_host_key" ] ||
		"$D/bin/dropbearmulti" dropbearkey -t ed25519 -f "$K/dropbear_ed25519_host_key" > /dev/null 2>&1
	ifconfig lo up
	"$D/bin/dropbearmulti" dropbear -s -r "$K/dropbear_ed25519_host_key" -D "$RUN" \
		-p "$PORT" -P "$PID" < /dev/null > /dev/null 2>&1
	;;
stop)
	[ -f "$PID" ] && kill "$(cat "$PID")" 2> /dev/null
	rm -f "$PID"
	;;
esac
exit 0
