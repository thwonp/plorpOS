# Optional: faster boot on the Miyoo Mini Flip

This is **optional**. plorpOS works without it. It makes the Flip reach the
plorpOS shelf about **6 seconds sooner**, with games and sound ready by the
time it shows (without it, about a second later). The stock firmware spends that time on
fixed waits, two splash pictures and a slow settings reader before it hands
over to the SD card. The Miyoo splash pictures during start-up are gone, so
you see black instead (the boot logo stays).

**It writes to the Flip's internal flash**: two partitions (`/customer`, the
start-up scripts, and `miservice`, the system's drivers) and one bootloader
setting (its start-up command). It uses the same kind of update Miyoo's own
firmware updates use. The bootloader itself, the kernel and the system are
not touched. It is meant for the firmware the Mini Flip ships with, **build
202510011046**. Do not use it on another build.

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
card root and repeat steps 3 to 5. That puts back everything this update
changed. Miyoo's full update takes about 5 to 8 minutes and also rewrites the
bootloader, so keep the Flip plugged in and do not unplug it until it
restarts. It also resets the Flip's own settings, including saved Wi-Fi
networks: join them again afterwards.

If the fast-boot update itself is interrupted, the bootloader is untouched,
and the same steps with Miyoo's file bring the Flip back.

## What it changes

- `/customer/main` (the firmware's start-up script): no 1 s wait, no
  `sdldisplay` splash pictures, the SD card is checked every 0.1 s instead of
  0.5 s, and two settings are read with `sed` instead of `jsonval`.
- `/customer/demo.sh`: drivers plorpOS never uses are not loaded (network
  file systems, NTFS, USB storage and USB keyboards/mice).
- `mi_sys.ko` in `miservice`: one byte, so the first program to use the
  display and sound after start-up waits 0.1 s instead of 2 s.
- The bootloader's start-up command: its `sleepms 1000` is removed.

The `/customer` changes are `mk/flip-fw/lean-customer.patch` in the source,
and `mk/flip-fw/build-lean.sh` rebuilds the image from your own copy of
Miyoo's firmware file. Thanks to tenlevels for finding the last three.
