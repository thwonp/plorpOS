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
# Bluetooth: not done yet on the Zero 40, and the Zero 28 has no antenna.
# bt_on/bt_off are no-ops.

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
bt_on() { :; }
bt_off() { :; }
