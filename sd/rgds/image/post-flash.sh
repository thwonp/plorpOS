# plorpOS's boot logo in place of ROCKNIX's (plorpos-rgds.10).
#
# ROCKNIX's initramfs init sources /flash/post-flash.sh, if there is one, as
# soon as it has mounted this partition, and only later runs load_splash,
# which draws the ROCKNIX logo (compiled into /usr/bin/rocknix-splash) and
# prints /etc/issue and "Loading, please wait..." under it. This replaces
# load_splash: the plorpOS logo, and no text.
#
# plorpos-bootlogo.raw is the panel's framebuffer, 640x480 XRGB, made by
# mk/rgds-image.sh. Not drawn here already: init clears the console right
# after this file is read, which would blank it again.
load_splash() {
	[ -e /dev/fb0 ] && cat /flash/plorpos-bootlogo.raw > /dev/fb0
}
