#!/bin/sh
# ssh on the MagicX Zero 40 (plorpos-auv): plorpOS's own static dropbear,
# since oakMOSS ships no SSH server. Off on a fresh card; the Network Services
# switch is the "ssh" setting, which boot.env carries to launch.sh. As root,
# key login only (no crypt() in the static build), keys from
# plorpOS/ssh/authorized_keys. After the Flip's (sd/miyoo/ssh.sh).
D=/mnt/SDCARD/plorpOS
K=$D/ssh
PID=/tmp/dropbear.pid
RUN=/tmp/ssh

case "$1" in
start)
	[ -f "$PID" ] && kill -0 "$(cat "$PID")" 2> /dev/null && exit 0
	mkdir -p "$K" "$RUN"
	chmod 700 "$RUN"
	[ -f "$K/authorized_keys" ] && cp "$K/authorized_keys" "$RUN/authorized_keys"
	chmod 600 "$RUN/authorized_keys" 2> /dev/null
	[ -s "$K/dropbear_ed25519_host_key" ] ||
		"$D/dropbearmulti" dropbearkey -t ed25519 -f "$K/dropbear_ed25519_host_key" > /dev/null 2>&1
	ifconfig lo up
	"$D/dropbearmulti" dropbear -r "$K/dropbear_ed25519_host_key" -D "$RUN" \
		-p 22 -P "$PID" < /dev/null > /dev/null 2>&1
	;;
stop)
	[ -f "$PID" ] && kill "$(cat "$PID")" 2> /dev/null
	rm -f "$PID"
	;;
esac
exit 0
