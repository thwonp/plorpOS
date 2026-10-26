# Installing on an Anbernic RG Rotate

plorpOS on the RG Rotate runs on [base618](https://github.com/thwonp/base618) (Armbian, Debian trixie,
with base618's boot chain and kernel) from a microSD card. The Rotate's own Android (stock or GammaOS)
stays on its storage: with the card out, it starts Android as before.

## 0. Flash SPL to the unit once

The Rotate only starts a card with base618's SPL in its boot loader slot. plorpOS uses the same one,
from `base618-v0.2-spl.zip` in [base618's v0.2 release](https://github.com/thwonp/base618/releases/tag/v0.2).
Only `splloader` (boot0) is written; `splloader_bak` keeps the unit's own SPL, and without a plorpOS or
base618 card the unit boots its Android as before. Pick one way:

- **From GammaOS**, any PC with adb (Windows, macOS, Linux), USB debugging on:
  ```sh
  adb root
  adb push spl-cm4.img flash-spl-android.sh /data/local/tmp/
  adb shell sh /data/local/tmp/flash-spl-android.sh
  adb pull /sdcard/base618-spl-backup
  ```
  Keep the pulled backup.
- **From Stock/GammaOS, using Linux over USB**, whatever the unit runs: get `spl-flasher.zip` from
  [beebono's ROCKNIX-T618 release](https://github.com/beebono/distribution/releases/tag/beta-20260818) and
  unzip it next to the `base618-v0.2-spl` folder, then:
  ```sh
  SPD_DUMP=$PWD/spl-flasher/spd_dump-linux ./base618-v0.2-spl/flash-spl.sh spl-flasher
  ```
  Then unplug the unit, power it off, slide the screen open, hold Home/Back (front, between the sticks)
  and plug it in. The screen stays dark the whole time: keep it plugged in for about a minute. USB access
  needs `sudo` in front of the command, or a udev rule - see base618's
  [install.md](https://github.com/thwonp/base618/blob/main/docs/install.md#1-the-spl-once-per-unit).
- **From Stock/GammaOS, using Windows** (not yet tested): with beebono's `spl-flasher.zip` (above), copy
  `spl-cm4.img` into its `spl-flasher` folder and run, in a command prompt there:
  ```bat
  spd_dump.exe --wait 300 keep_charge 1 fdl fdl1-dl.bin 0x5500 fdl fdl2-dl.bin 0x9EFFFE00 exec write_part splloader spl-cm4.img poweroff
  ```
  Then enter download mode as above. Not the zip's own `flash-spl.bat`, which writes both slots.

A unit that already runs base618 has it: skip this step.

## 1. Flash the card

Check `plorpos-rotate.img.xz` against `SHA256SUMS` and write it to a microSD card of 8 GB or more, e.g.
`xz -dc plorpos-rotate.img.xz | sudo dd of=/dev/sdX bs=4M conv=fsync`, or with Rufus / Raspberry Pi
Imager / etc.

## 2. First boot

Put the card in and turn the Rotate on. **With a USB cable plugged in it stops at the charging screen:
hold Power** to go on. Unplugged, it starts on its own.

The first start copies the unit's own firmware off its storage (nothing there is written), makes the data
partition - an exFAT partition named **PLORPOS** across the rest of the card, with `plorpOS/` (plorpOS
itself) and `Roms/ Bios/ Saves/ Music/` (a folder per console in `Roms/` and `Saves/`) - shows
"plorpOS: setup complete" and **powers off (once)**. Turn it on again: the shelf comes up.

Wi-Fi is set up from MENU > Network Services. ssh is off until you turn it on there; the login is
**root / plorpos**.

## 3. Your games and files

Turn the Rotate off, put the card in a computer, and copy your files onto the
**PLORPOS** partition, laid out as on the other plorpOS devices:

```
Roms/<system>/   games, one folder per system (see plorpOS/systems.cfg)
Bios/            BIOS files, and pico8_64 + pico8.dat for native PICO-8
Music/           Muse's music
Saves/           saves, per console
```

## 4. Controls

Open: the buttons as on the other devices. Menu held with Vol+/Vol- sets the
brightness.

Closed (the screen swivelled over the buttons): the picture turns with it,
and the screen's touch does what the buttons did:

| Touch | Does |
|---|---|
| tap | selects what is under it; A on the selected item |
| swipe | the d-pad (a longer swipe moves further, a flick coasts) |
| swipe in from the left or right edge | B (back) |
| long press | Y on what is under it (favorite a game) |
| two-finger tap | X on what is under it (a game's info) |
| three-finger tap | Menu |
| long press in a game | the in-game menu |

The **side key** is Muse's: tap to play or pause, hold for the previous track,
double tap for the next - on the shelf and in a game.

**Bluetooth headsets**: Menu > Bluetooth, Y to search, A to pair. Sound moves
to the headset while it is connected and back to the speaker after; Muse
pauses when the headset goes. Its own buttons play/pause and skip in Muse.

## Updating

Copy a newer `plorpOS/` folder over the one on the PLORPOS partition. A release that changes the
system underneath (the kernel, for one) comes as a new card image.
