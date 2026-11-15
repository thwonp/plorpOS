#!/bin/sh
# adb over USB on the Anbernic RG DS (plorpos-rgds.11). ROCKNIX has no adbd;
# this folder carries Debian's (mk/fetch-rgds-adbd.sh) and adbroot.so, which
# keeps it root (tools/adbroot.c).
#
#   adb.sh start   a USB gadget with one FunctionFS function, adbd on it
#   adb.sh stop    all of that undone
#
# System Settings > USB Debugging runs these and keeps the marker file
# <card>/.userdata/rgds/adb; launch.sh starts adb at boot when the file is
# there. It takes the place of ROCKNIX's own gadgets (USB network, MTP): one
# gadget can own the port, and nothing in plorpOS turns those on.
#
# There is no authorisation: any computer on the cable gets a root shell.
HERE=$(cd "$(dirname "$0")" && pwd)
G=/sys/kernel/config/usb_gadget/plorpos-adb
FFS=/dev/usb-ffs/adb       # adbd's own fixed path
UDC=$(ls /sys/class/udc 2> /dev/null | head -n1)

stop() {
	[ -d $G ] && echo "" > $G/UDC 2> /dev/null
	P=$(pidof adbd) && kill $P
	i=0
	while mountpoint -q $FFS && ! umount $FFS 2> /dev/null && [ $i -lt 20 ]; do
		sleep 0.1; i=$((i + 1))
	done
	[ -d $G ] || return 0
	rm -f $G/configs/c.1/ffs.adb
	rmdir $G/configs/c.1/strings/0x409 $G/configs/c.1 $G/functions/ffs.adb \
	      $G/strings/0x409 $G 2> /dev/null
}

start() {
	[ -n "$UDC" ] || { echo "adb: no USB device controller"; return 1; }
	[ "$(cat $G/UDC 2> /dev/null)" = "$UDC" ] && pidof adbd > /dev/null && return 0
	stop
	# Whatever of ROCKNIX's holds the port lets go of it.
	for u in /sys/kernel/config/usb_gadget/*/UDC; do
		[ -n "$(cat "$u" 2> /dev/null)" ] && echo "" > "$u"
	done
	modprobe -q usb_f_fs
	mkdir -p $G/strings/0x409 $G/configs/c.1/strings/0x409 $G/functions/ffs.adb || return 1
	# This adbd describes itself for full and high speed only; without the
	# limit a SuperSpeed-capable port finds no configuration (as ROCKNIX's MTP).
	[ -w $G/max_speed ] && echo high-speed > $G/max_speed
	echo 0x18d1 > $G/idVendor          # Google's adb interface: no host setup
	echo 0x4ee7 > $G/idProduct
	echo plorpOS > $G/strings/0x409/manufacturer
	echo "Anbernic RG DS" > $G/strings/0x409/product
	cut -c1-16 /storage/.cache/systemd-machine-id > $G/strings/0x409/serialnumber
	echo adb > $G/configs/c.1/strings/0x409/configuration
	ln -s $G/functions/ffs.adb $G/configs/c.1/
	mkdir -p $FFS
	mount -t functionfs adb $FFS || return 1
	LD_LIBRARY_PATH=$HERE LD_PRELOAD=$HERE/adbroot.so HOME=/storage \
		"$HERE/adbd" > /dev/null 2>&1 &
	# The gadget can only take the port once adbd has described itself, which
	# is when the two data endpoints appear.
	i=0
	while [ ! -e $FFS/ep1 ] && [ $i -lt 30 ]; do sleep 0.1; i=$((i + 1)); done
	echo "$UDC" > $G/UDC || { echo "adb: the gadget did not start"; stop; return 1; }
	# The RK3566's port, started with no cable in, has to be told so, or a
	# cable plugged in later is never seen; ROCKNIX's udev rule then connects it.
	/usr/bin/usbgadget trigger > /dev/null 2>&1
	return 0
}

case $1 in
start) start ;;
stop)  stop ;;
*)     echo "usage: adb.sh start|stop"; exit 2 ;;
esac
