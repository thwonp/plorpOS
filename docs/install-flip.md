# Installing on a Miyoo Mini Flip

plorpOS on the Flip is **work in progress** (plorpos-f1p): it runs, but sleep,
the lid, brightness keys and Muse's audio hand-over are not finished.

The Flip has 128 MB of RAM (about 101 MB usable) and no swap. That decides what
runs; see "What is not included".

## What this zip is

A card overlay. On a card that **already runs Allium** (the Flip's usual
community firmware) it takes over the boot, and falls back to Allium when it
cannot start; it does not change Allium itself. On a card without Allium it
boots straight into plorpOS (the stock firmware starts the hook).

It carries the Wi-Fi driver, `.tmp_update/8188fu.ko`, copied unmodified from
Allium: the stock firmware has none. The driver is a Realtek module under
GPL-2.0, so its source is that of the project it came from. The ssh server
Allium also starts is not included. BIOS files are yours to supply.

## Install

1. Back up the card. Keep a copy of `.tmp_update/` in particular.
2. Only if the card runs Allium: rename `.tmp_update/updater` to `.tmp_update/updater.allium`.
   **Do this first**: the zip carries its own `updater`, and without the rename
   it would overwrite Allium's, leaving nothing to fall back to.
3. Unzip. Copy everything in it to the root of the card, merging folders.
   It adds:
   ```
   .tmp_update/          the boot hook and the Wi-Fi driver (Allium's own updater is the updater.allium you renamed)
   TortOS/               the launcher, diatom, Muse, cores, fonts, cards
   Roms/<console>/       empty folders named as the shelves expect
   Bios/  Saves/
   ```
4. Put your ROMs in `Roms/<console>/`. Your existing Allium `Roms/` folders keep
   working where the folder name matches.
5. Firmware (Sega CD, PlayStation, PC Engine CD, Neo Geo) is looked for in
   `Bios/`. Allium keeps it in `BIOS/`: rename it to `Bios/` in two steps
   (`BIOS` to `Bios.tmp`, then to `Bios`), because FAT treats a case-only rename
   as no change and then cannot find the folder. Allium, if you fall back to it,
   will no longer find its BIOS files.
6. `MD5SUMS` lists every file in the zip. Check with `md5sum -c MD5SUMS` from
   the card root.
7. Eject, put the card in, power on.

## Back to Allium

Create an empty file `TortOS/.boot-allium` on the card; the next boot starts
Allium. To remove plorpOS, delete `TortOS/`, then rename
`.tmp_update/updater.allium` to `.tmp_update/updater`.

**Never kill the process that stock `/customer/main` started from
`.tmp_update/updater`.** The firmware reads that as a failed update and deletes
the whole `.tmp_update/` folder.

## What is not included

- **Arcade** is two shelves, each its own folder: `Arcade (FBA 2012)` (the
  FBA 2012 core: CPS1, CPS2 and more) and `Arcade (MAME 2000)` (MAME 0.37 sets;
  on the test card 122 of 152 loaded, the rest being BIOS archives, Neo Geo and
  newer boards that core predates). A ROM set must match its core's version.
- **Neo Geo** runs on the small FBA 2012 Neo Geo core.
- **PlayStation** runs on a pcsx_rearmed build with a smaller dynarec cache; one game
  (Ace Combat 2) was measured at full speed, others are untested.
- **Lynx, WonderSwan, Atari, 7800**: no shelves yet.
- Super Nintendo's Star Fox (Super FX) runs at 84% speed.
- Game Boy Advance runs on gpsp, not mGBA (mGBA lagged in play).

Measured per console in plorpos-f1p.11; the table is in that bead.
