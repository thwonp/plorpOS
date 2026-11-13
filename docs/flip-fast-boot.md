# Optional: faster boot on the Miyoo Mini Flip

This is **optional**. plorpOS works without it. It makes the Flip reach the
plorpOS shelf about **4 seconds sooner** (the stock firmware spends that time
on a one-second wait and two splash pictures before it hands over to the SD
card). The Miyoo splash pictures during start-up are gone as well, so you see
black instead.

**It writes to the Flip's internal flash** (one partition, `/customer`). It is
the same kind of update Miyoo's own firmware updates use, and the stock
bootloader, kernel and system are not touched. It is meant for the firmware
the Mini Flip ships with, **build 202510011046**. Do not use it on another build.

Not affiliated with Miyoo. Use at your own risk.

## Install

1. Charge the Flip or keep it plugged in. Do not interrupt the update.
2. Put `miyoo285_fw.img` from this download in the **root** of the SD card
   (only one file with that name; replace any other).
3. Power the Flip **fully off** and unplug the cable.
4. Hold **MENU**, plug in the USB cable, and keep MENU held until you see the
   update screen. **Do not press the power button.** (A normal power-on or a
   restart does not start the update.)
5. It writes for a short while, then restarts into plorpOS.

You can delete `miyoo285_fw.img` from the card afterwards. Leaving it there
does no harm: it only runs when you plug in the cable while holding MENU.

## Undo, or if something goes wrong

Put Miyoo's own firmware file for the Mini Flip (`miyoo285_fw.img`, build
202510011046, from Miyoo's firmware page, lomiyoo.com/pages/firmware) in the
card root and repeat steps 3 to 5. That restores the stock `/customer`. Miyoo's
full update takes about 5 to 8 minutes and also rewrites the bootloader, so
keep the Flip plugged in and do not unplug it until it restarts.

If the fast-boot update itself is interrupted, the bootloader is untouched
(this image writes `/customer` only), and the same steps with Miyoo's file
bring the Flip back.

## What it changes

In `/customer/main` (the firmware's start-up script) it sets the first wait to
0 and skips the two `sdldisplay` splash pictures. Nothing else differs from
Miyoo's `/customer`. The patch is `mk/flip-fw/lean-main.patch` in the source,
and `mk/flip-fw/build-lean.sh` rebuilds the image from your own copy of Miyoo's
firmware file.
