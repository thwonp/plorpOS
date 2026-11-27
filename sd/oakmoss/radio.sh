# plorpOS radio helpers for the MagicX boards on oakMOSS (plorpos-auv,
# plorpos-08c). Sourced by launch.sh at boot and by plat_sleep() around a
# suspend (radio_sh_call, src/platform_brick.c). oakMOSS leaves the supplicant
# to the launcher, so it is ours here, started the way src/wifi.c starts it:
# our own wpa_supplicant on the card's config, BusyBox udhcpc once associated.
# The driver: the Zero 40's XR829 (xradio_wlan, by name; oakMOSS loads none),
# the Zero 28's RTL8189ES (8189es; oakMOSS loads it at boot). Never xradio on
# the Zero 28: its platform init rescans SDIO and knocks the Realtek off
# (oakMOSS TODO.md).
#
# Bluetooth (plorpos-auv.6): the Zero 40's XR829, the Brick's chip, brought up
# the Brick's way (sd/tortos/radio.sh has the reasoning for every step) on the
# same BlueZ 5.54 and bluealsa. One difference: this kernel has no driver for
# the device tree's btlpm node, so nothing raises bt_wake (PL4, gpio 356) and
# the chip never answers the attach; bt_on raises it by hand. The Zero 28 has
# no antenna: bt_on/bt_off are no-ops there.

WPA_SOCK=/var/run/wpa_supplicant
BOARD=$(cat /usr/magicx/device)
WPA_CONF=${USERDATA_PATH:-/mnt/SDCARD/.userdata/$BOARD}/wpa_supplicant.conf
case $BOARD in
zero28) WIFI_MODS=8189es ;;
*) WIFI_MODS="xradio_wlan xradio_core xradio_mac" ;;   # users first: see below
esac

wifi_on() {
	i=0
	[ -d /sys/class/net/wlan0 ] || modprobe ${WIFI_MODS%% *} 2> /dev/null
	while [ ! -d /sys/class/net/wlan0 ] && [ $i -lt 10 ]; do sleep 1; i=$((i + 1)); done
	ip link set dev wlan0 up 2> /dev/null
	[ -f "$WPA_CONF" ] || echo "update_config=1" > "$WPA_CONF"
	wpa_cli -p $WPA_SOCK -i wlan0 ping 2> /dev/null | grep -q PONG ||
		wpa_supplicant -B -D nl80211,wext -i wlan0 -C $WPA_SOCK -c "$WPA_CONF" > /dev/null 2>&1
	i=0
	while [ $i -lt 15 ]; do
		if wpa_cli -p $WPA_SOCK -i wlan0 status 2> /dev/null | grep -q '^wpa_state=COMPLETED'; then
			udhcpc -i wlan0 -S -t 5 -T 7 -b -q > /dev/null 2>&1
			# The clock (plorpos-5tm.9): oakMOSS runs no time daemon. One
			# BusyBox ntpd -q sets it and the RTC (UTC), then exits.
			(ntpd -n -q -p pool.ntp.org && hwclock -w -u) > /dev/null 2>&1 &
			return 0
		fi
		sleep 1
		i=$((i + 1))
	done
	echo "wifi: no association after ${i}s" >> "${LOGS_PATH:-/tmp}/tortos.log"
}

# Before a suspend, and for Wi-Fi off at boot: the daemon, the lease, the
# link and the driver. oakMOSS measured the Zero 40 sleeping only once its
# radio module is unloaded (hardware-notes, "takes ~4 s longer to go to sleep");
# the Zero 28's comes out the same way, and wifi_on loads it again.
wifi_stop_once() {
	wpa_cli -p $WPA_SOCK -i wlan0 terminate > /dev/null 2>&1
	killall -q udhcpc 2> /dev/null
	killall -q ntpd 2> /dev/null      # a sync still waiting for an answer
	ip link set dev wlan0 down 2> /dev/null
	# One at a time, users first: BusyBox rmmod given all three unloads none.
	for m in $WIFI_MODS; do rmmod $m 2> /dev/null; done
}

radio_off() { wifi_stop_once; }

# bt_pcm_name and bt_write_asoundrc. Guarded: `.` on a missing file ends a
# non-interactive shell.
[ -f "$TORTOS_DIR/bt-alsa.sh" ] && . "$TORTOS_DIR/bt-alsa.sh"

BT_RECONNECT_PIDFILE=/tmp/tortos_bt_reconnect.pid
BT_WAKE=356     # PL4, the btlpm node's bt_wake

if [ "$BOARD" = zero28 ]; then
bt_on() { :; }
bt_off() { :; }
else

bt_stop_proc() {
	killall -q "$1" 2> /dev/null
	n=0
	while pidof "$1" > /dev/null && [ $n -lt 20 ]; do
		sleep 0.1; n=$((n + 1))
	done
}

bt_wake() {     # bt_wake 1|0
	[ -d /sys/class/gpio/gpio$BT_WAKE ] || echo $BT_WAKE > /sys/class/gpio/export 2> /dev/null
	echo out > /sys/class/gpio/gpio$BT_WAKE/direction 2> /dev/null
	echo "$1" > /sys/class/gpio/gpio$BT_WAKE/value 2> /dev/null
}

# The rail behind rfkill's sunxi-bt (bt_rst, PL2), cycled as on the Brick: a
# failed attach leaves the chip wedged until it is.
bt_rail() {
	for r in /sys/class/rfkill/rfkill*; do
		[ "$(cat "$r/name" 2> /dev/null)" = sunxi-bt ] && echo "$1" > "$r/state" 2> /dev/null
	done
}

# The Brick's order: links closed while bluetoothd still owns them, then the
# daemons, the adapter, the UART and the rail (plorpos-pky.11).
bt_off() {
	if [ -f "$BT_RECONNECT_PIDFILE" ]; then
		kill "$(cat "$BT_RECONNECT_PIDFILE")" 2> /dev/null
		rm -f "$BT_RECONNECT_PIDFILE"
	fi
	rm -f /tmp/tortos_btsink
	killall -q btplayer 2> /dev/null
	if pidof bluetoothd > /dev/null; then
		for mac in $(hcitool con 2> /dev/null | awk '/ACL/{print $3}'); do
			bluetoothctl disconnect "$mac" > /dev/null 2>&1
		done
		i=0
		while hcitool con 2> /dev/null | grep -q ACL && [ $i -lt 30 ]; do
			sleep 0.1; i=$((i + 1))
		done
	fi
	bt_stop_proc bluealsa
	bt_stop_proc bluetoothd
	hciconfig hci0 down 2> /dev/null
	bt_stop_proc hciattach
	rfkill block bluetooth 2> /dev/null
	bt_rail 0
	bt_wake 0
}

bt_player() {
	[ -x "$TORTOS_DIR/btplayer" ] || return 0
	pidof btplayer > /dev/null && return 0
	setsid "$TORTOS_DIR/btplayer" >> "$LOGS_PATH/tortos.log" 2>&1 < /dev/null &
}

# Whether bluealsa has an A2DP stream for this address yet (the Brick's).
bt_a2dp_ready() {
	bluealsa-aplay -l 2> /dev/null |
		awk -v m="$1" '/^hci/ { on = index($0, m) > 0 } on && /A2DP/ { f = 1 } END { exit !f }'
}

bt_on() {
	bt_wake 1
	bt_rail 0; sleep 1; bt_rail 1; sleep 1
	rfkill unblock bluetooth 2> /dev/null
	killall -q hciattach 2> /dev/null
	try=0
	while [ $try -lt 3 ]; do
		# -n under setsid: it stays up for as long as the adapter does. The
		# address is hciattach's, kept in /etc/bluetooth/xr_bt.conf.
		setsid hciattach -n /dev/ttyS1 xradio > /dev/null 2>&1 < /dev/null &
		i=0
		while [ ! -d /sys/class/bluetooth/hci0 ] && [ $i -lt 10 ]; do
			sleep 1; i=$((i + 1))
		done
		[ -d /sys/class/bluetooth/hci0 ] && break
		try=$((try + 1))
		echo "bt: attach attempt $try produced no hci0" >> "$LOGS_PATH/tortos.log"
		killall -q hciattach 2> /dev/null
		bt_rail 0; sleep 2; bt_rail 1; sleep 2
	done
	if [ ! -d /sys/class/bluetooth/hci0 ]; then
		echo "bt: gave up after $try attach attempts" >> "$LOGS_PATH/tortos.log"
		return 1
	fi
	hciconfig hci0 up 2> /dev/null
	# oakMOSS's dbus is up from boot (S10dbus). Bonds: BlueZ 5.54 keeps them
	# in /etc/lib/bluetooth, on the overlay, as on the Brick.
	pidof bluetoothd > /dev/null ||
		setsid /usr/bin/bluetoothd -n > /dev/null 2>&1 < /dev/null &
	pidof bluealsa > /dev/null ||
		setsid bluealsa -S -p a2dp-source -p hfp-ag --a2dp-volume > /dev/null 2>&1 < /dev/null &
	sleep 3          # both profiles registered with BlueZ before any connect
	bt_player
	setsid /bin/sh -c '. "$0" && bt_reconnect' "$TORTOS_DIR/radio.sh" \
		> /dev/null 2>&1 < /dev/null &
	echo $! > "$BT_RECONNECT_PIDFILE"
}

bt_link_set() {
	set -- /sys/class/bluetooth/hci0:*
	BT_LINKS="$*"
}

# Keep a bonded headset connected and publish where the sound goes
# (/tmp/tortos_btsink). The Brick's bt_reconnect, line for line: its comments
# say why each rule is there.
bt_reconnect() {
	adapter=$(hciconfig hci0 2> /dev/null | sed -n 's/.*BD Address: \([0-9A-F:]*\).*/\1/p')
	[ -n "$adapter" ] || return 0
	latest=
	prev_links=
	soon=0
	while :; do
		bt_player
		connected=
		bonds=
		for d in "$TORTOS_BT_BONDS/$adapter"/*:*; do
			[ -d "$d" ] || continue
			grep -q '^Trusted=true' "$d/info" 2> /dev/null || continue
			grep -q '^\[LinkKey\]' "$d/info" 2> /dev/null || continue
			bonds="$bonds $(basename "$d")"
		done
		cons=$(hcitool con 2> /dev/null)
		links=
		for mac in $bonds; do
			bluetoothctl info "$mac" 2> /dev/null | grep -q 'Connected: yes' || continue
			h=$(echo "$cons" | sed -n "s/.*ACL $mac handle \([0-9]*\).*/\1/p" | head -1)
			links="$links $mac/$h"
			case " $prev_links " in *" $mac/$h "*) ;; *) latest=$mac ;; esac
		done
		prev_links=$links
		case "$links " in
		*" $latest/"*) connected=$latest ;;
		*) set -- $links; connected=${1%%/*} ;;
		esac
		[ -n "$connected" ] || rm -f /tmp/tortos_btsink
		for mac in $bonds; do
			[ -z "$connected" ] || break
			bluetoothctl connect "$mac" > /dev/null 2>&1
			sleep 1          # judge by info, never by connect's return
			bluetoothctl info "$mac" 2> /dev/null | grep -q 'Connected: yes' &&
				connected=$mac
		done
		if [ -n "$connected" ] && ! bt_a2dp_ready "$connected"; then
			soon=$((soon + 1))
		elif [ -n "$connected" ]; then
			soon=0
			link=$(hcitool con 2> /dev/null |
				sed -n "s/.*ACL $connected handle \([0-9]*\).*/\1/p" | head -1)
			printf '%s\n%s\n' "$(bt_pcm_name "$connected")" "$link" \
				> /tmp/tortos_btsink.tmp && mv /tmp/tortos_btsink.tmp /tmp/tortos_btsink
		else
			soon=0
			rm -f /tmp/tortos_btsink
		fi
		n=0
		wait=20
		[ $soon -gt 0 ] && [ $soon -le 15 ] && wait=1
		bt_link_set
		was=$BT_LINKS
		while [ $n -lt $wait ] && [ ! -e /tmp/tortos_btpass ]; do
			sleep 1
			n=$((n + 1))
			bt_link_set
			[ "$BT_LINKS" = "$was" ] || break
		done
		rm -f /tmp/tortos_btpass
	done
}

fi
