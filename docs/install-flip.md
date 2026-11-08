# Installing on a Miyoo Mini Flip

plorpOS on the Flip is **work in progress** (plorpos-f1p): it runs, but sleep,
the lid, brightness keys and Muse's audio hand-over are not finished.

The Flip has 128 MB of RAM (about 101 MB usable) and no swap. That decides what
runs; see "What is not included".

## What this zip is

An overlay for a card that **already runs Allium** (the Miyoo Mini Flip's usual
community firmware). It takes over the boot, and falls back to Allium when it
cannot start. It does not include or change Allium, and it needs three things
from it: the Wi-Fi driver (`.tmp_update/8188fu.ko`), the ssh server
(`.allium/bin/dropbear`), and your BIOS files. Without Allium the Flip boots,
but without Wi-Fi.

## Install

1. Back up the card. Keep a copy of `.tmp_update/` in particular.
2. On the card, rename `.tmp_update/updater` to `.tmp_update/updater.allium`.
   **Do this first**: the zip carries its own `updater`, and without the rename
   it would overwrite Allium's, leaving nothing to fall back to.
3. Unzip. Copy everything in it to the root of the card, merging folders.
   It adds:
   ```
   .tmp_update/updater   the boot hook (Allium's own is the updater.allium you renamed)
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

- **Arcade and Neo Geo** (FBNeo): its 45 MB of working memory is more than the
  Flip can map next to the launcher. The shelves exist; the core is not shipped.
- **PlayStation**: too slow, and memory grows while it runs. Not shipped.
- **Lynx, WonderSwan, Atari, 7800**: no shelves yet.
- Super Nintendo's Star Fox (Super FX) runs at 84% speed.
- Game Boy Advance runs on gpsp, not mGBA (mGBA lagged in play).

Measured per console in plorpos-f1p.11; the table is in that bead.
