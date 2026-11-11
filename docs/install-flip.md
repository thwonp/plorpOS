# Installing on a Miyoo Mini Flip

The Flip has 128 MB of RAM (about 101 MB usable) and no swap. That decides what
runs; see "What is not included".

## What this zip is

A complete card: the boot hook, plorpOS, its cores, ssh and the Wi-Fi driver.
Nothing else is needed on the card besides Miyoo's stock firmware on the Flip
itself, which starts the hook (`.tmp_update/updater`) at power-on in place of
Miyoo's MainUI.

- **Cores** are built from pinned upstream source for the Flip
  (`mk/flip-cores/cores.lock` in the source tree lists each one's commit and
  licence; `TortOS/THIRD-PARTY-LICENSES.md` on the card).
- **Wi-Fi driver**: `TortOS/8188fu.ko`, Miyoo's own Realtek RTL8188FU module
  for the Flip's kernel, unmodified. The stock firmware loads the radio driver
  from the SD card and has none of its own.
- **ssh** (dropbear), off until you switch it on (see "ssh").
- BIOS files are yours to supply.

## Install

1. Start from an empty FAT32 card, or back up the card you have. If it has a
   `.tmp_update/` folder from another firmware, that firmware stops booting:
   the zip replaces its `updater`.
2. Unzip. Copy everything in it to the root of the card, merging folders.
   It adds:
   ```
   .tmp_update/updater   the boot hook
   TortOS/               the launcher, diatom, Muse, cores, ssh, the Wi-Fi driver, fonts, cards
   Roms/<console>/       empty folders named as the shelves expect
   Bios/  Saves/
   ```
3. Put your ROMs in `Roms/<console>/`.
4. Firmware (Sega CD, PlayStation, PC Engine CD, Neo Geo) is looked for in
   `Bios/`. If your card has a `BIOS/` folder, rename it to `Bios/` in two
   steps (`BIOS` to `Bios.tmp`, then to `Bios`), because FAT treats a
   case-only rename as no change and then cannot find the folder.
5. `MD5SUMS` lists every file in the zip. Check with `md5sum -c MD5SUMS` from
   the card root.
6. Eject, put the card in, power on.

## Wi-Fi and ssh

Wi-Fi is switched on in the plorpOS menu, Network Services. Its **SSH** row
starts an ssh server on port 22 and is **off** on a fresh card. Log in as
**`plorpos`**, password **`plorpos`** (a root account; it starts in the card's
root folder). Keys work too: put your public key (for example the contents of
`~/.ssh/id_ed25519.pub`) in `TortOS/ssh/authorized_keys` on the card, one key
per line, and log in as `plorpos` or `root`. Anyone on your network who knows
the default password can log in while SSH is on, so switch it off when you do
not need it. The server's own host key is made the first time it starts and
kept in `TortOS/ssh/`. The setting survives a reboot. The card has no sftp
server, so `scp` fails; copy with `ssh plorpos@<flip> 'cat > /path' < file`.

## If plorpOS cannot start

When `TortOS/launch.sh` is missing, or the launcher fails five times in a row,
the hook hands the Flip back to Miyoo's stock MainUI and renames itself to
`.tmp_update.off` (the stock firmware would otherwise delete it). To boot
plorpOS again, fix the card from a PC and rename `.tmp_update.off` back to
`.tmp_update`. An empty file `TortOS/.boot-idle` makes the hook wait instead
(useful with ssh on).

To remove plorpOS, delete `TortOS/` and `.tmp_update/`; the Flip then boots
Miyoo's MainUI.

**Never kill the process that stock `/customer/main` started from
`.tmp_update/updater`.** The firmware reads that as a failed update and deletes
the whole `.tmp_update/` folder.

## Optional: faster boot

A separate download, **plorpOS Flip fast boot**, makes the Flip start about 4
seconds sooner by flashing one partition of its internal storage. It is not
needed and not part of this zip. Its own guide (`FAST-BOOT.md`, in that
download) has the steps: the update starts only when you hold MENU while
plugging in the USB cable with the Flip off. It is for the stock firmware
build 202510011046 only.

## What is not included

- **Arcade** is two shelves, each its own folder: `Arcade (FBA 2012)` (the
  FBA 2012 core: CPS1, CPS2 and more) and `Arcade (MAME 2000)` (MAME 0.37 sets;
  on the test card 122 of 152 loaded, the rest being BIOS archives, Neo Geo and
  newer boards that core predates). A ROM set must match its core's version.
- **Neo Geo** runs on the small FBA 2012 Neo Geo core.
- **PlayStation** runs on a pcsx_rearmed build with a smaller dynarec cache. It is
  **experimental**: it held full speed in testing, but only one game (Ace Combat 2)
  was checked at length, and memory is tight, so other titles may not run as well.
- **Online features** (RetroAchievements sign-in, art and name downloads) use a curl
  bundled in `TortOS/` because the Flip's firmware has none. They need Wi-Fi set up;
  RetroAchievements sign-in has not been tested end to end.
- **Lynx, WonderSwan, Atari, 7800**: no shelves yet.
- Super Nintendo's Star Fox (Super FX) holds full speed by leaving out an occasional
  frame draw (about 4% of frames in testing); the emulation itself runs at stock speed.
- Game Boy Advance runs on gpsp, not mGBA (mGBA lagged in play).

Measured per console in plorpos-f1p.11 and again for the from-source cores in
plorpos-f1p.62.3; the tables are in those beads.
