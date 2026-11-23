# plorpOS radio helpers for the MagicX Zero 40 on oakMOSS (plorpos-auv).
# Sourced by launch.sh at boot and by plat_sleep() around a suspend
# (radio_sh_call, src/platform_brick.c). oakMOSS loads no radio driver and
# leaves the supplicant to the launcher, so both are ours here, started the
# way src/wifi.c starts them: the XR829's xradio_wlan by name, our own
# wpa_supplicant on the card's config, BusyBox udhcpc once associated.
#
# Bluetooth is not done yet on this device: bt_on/bt_off are no-ops.

WPA_SOCK=/var/run/wpa_supplicant
WPA_CONF=${USERDATA_PATH:-/mnt/SDCARD/.userdata/zero40}/wpa_supplicant.conf

wifi_on() {
	i=0
	[ -d /sys/class/net/wlan0 ] || modprobe xradio_wlan 2> /dev/null
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
# radio module is unloaded (hardware-notes, "takes ~4 s longer to go to sleep").
wifi_stop_once() {
	wpa_cli -p $WPA_SOCK -i wlan0 terminate > /dev/null 2>&1
	killall -q udhcpc 2> /dev/null
	killall -q ntpd 2> /dev/null      # a sync still waiting for an answer
	ip link set dev wlan0 down 2> /dev/null
	# One at a time, users first: BusyBox rmmod given all three unloads none.
	rmmod xradio_wlan 2> /dev/null; rmmod xradio_core 2> /dev/null; rmmod xradio_mac 2> /dev/null
}

radio_off() { wifi_stop_once; }
bt_on() { :; }
bt_off() { :; }
