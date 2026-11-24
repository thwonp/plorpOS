#!/bin/sh
# Syncthing on every plorpOS port with Wi-Fi (plorpos-5jh): plorpOS's own
# build (mk/build-syncthing.sh). Off on a fresh card; the Network Services
# switch is the "syncthing" setting, and the launcher calls "start" when it
# comes up with the switch on. Its keys, config and database live in
# .userdata/shared on the card, so the device keeps its Syncthing ID through a
# reinstall of plorpOS/.
#
#   syncthing.sh start | stop
#
# It shares the card's Saves folder and nothing else; which devices get it is
# chosen in the web UI, http://<device>:8384, signed into as plorpos/plorpos.
# The folder ignores permissions and allows 2 s of time skew: cards are FAT or
# exFAT, which keep neither Unix permissions nor (FAT) exact times.
#
# A transient systemd unit where there is systemd-run: it lives through
# launcher restarts, and systemd stops it at power-off. Otherwise
# start-stop-daemon, or a plain background process. Syncthing's own monitor
# process restarts it after a crash or the web UI's Restart.
D=$(cd "$(dirname "$0")" && pwd)
CARD=$(dirname "$D")
S=$CARD/.userdata/shared/syncthing
SAVES=$CARD/Saves
PID=/tmp/syncthing.pid
UNIT=plorpos-syncthing
ST=$D/syncthing

# One at a time: the launcher does not wait for this script, which would hold
# the menu up, so a quick on and off could otherwise run a start and a stop
# together. Nothing that outlives the script keeps the lock (9>&-).
exec 9> /tmp/syncthing.lock
flock 9

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
	mkdir -p "$S" "$SAVES"
	export HOME="$S"
	[ -f "$S/config.xml" ] ||
		"$ST" generate --home="$S" --gui-user=plorpos --gui-password=plorpos \
			--no-port-probing > /dev/null 2>&1
	# The web UI on every network the device is on (the config says
	# localhost). HOME too (set above), which a systemd unit lacks and
	# Syncthing will not start without. Some of these devices trust no
	# certificate authority: plorpOS's bundle, for the relay list and the
	# discovery servers.
	export SSL_CERT_FILE="$D/cacert.pem"
	set -- serve --home="$S" --gui-address=http://0.0.0.0:8384 --no-browser
	if command -v systemd-run > /dev/null; then
		systemctl reset-failed "$UNIT" 2> /dev/null
		systemd-run -q --unit="$UNIT" --property=Restart=on-failure --setenv=HOME="$HOME" \
			--setenv=SSL_CERT_FILE="$SSL_CERT_FILE" -- "$ST" "$@" > /dev/null 2>&1 9>&-
	elif command -v start-stop-daemon > /dev/null; then
		start-stop-daemon -S -b -m -p "$PID" -x "$ST" -- "$@" > /dev/null 2>&1 9>&-
	else
		"$ST" "$@" > /dev/null 2>&1 < /dev/null 9>&- &
		echo $! > "$PID"
	fi
	# Saves, once: it goes through the running instance, which takes some
	# seconds to answer. A later start finds it there and changes nothing.
	(
		C="$ST cli --home=$S config folders"
		i=0
		while [ $i -lt 60 ] && ! L=$($C list 2> /dev/null); do sleep 1; i=$((i + 1)); done
		echo "$L" | grep -qx plorpos-saves ||
			$C add --id plorpos-saves --label Saves --path "$SAVES" \
				--ignore-perms --raw-mod-time-windows 2 > /dev/null 2>&1
	) > /dev/null 2>&1 < /dev/null 9>&- &
	;;
stop)
	if command -v systemd-run > /dev/null; then
		systemctl stop "$UNIT" 2> /dev/null
	elif [ -f "$PID" ]; then
		P=$(cat "$PID")
		kill "$P" 2> /dev/null
		# The monitor takes Syncthing down with it, which closes its database.
		i=0
		while kill -0 "$P" 2> /dev/null && [ $i -lt 50 ]; do sleep 0.2; i=$((i + 1)); done
		rm -f "$PID"
	fi
	;;
esac
exit 0
