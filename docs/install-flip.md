# Installing plorpOS on the Miyoo Mini Flip

You need the Flip, a microSD card and a computer.

### 1. Prepare the card

Use an empty FAT32 card with a Master Boot Record partition scheme (exFAT is
untested), or back up the card you have. A `.tmp_update/` folder from another
firmware stops that firmware booting: the zip replaces its `updater`.

### 2. Copy the zip

Download `plorpOS-v<version>-miyoominiflip.zip` from
[Releases](https://github.com/thwonp/plorpOS/releases/latest) and unzip it.
Copy everything in it to the root of the card, merging folders.

### 3. Check the files (optional)

From the card root: `md5sum -c MD5SUMS`.

### 4. Boot

Eject the card, put it in the Flip and power on.

# Device specific notes for the Miyoo Mini Flip

### Base system

plorpOS runs on Miyoo's stock firmware. At power-on the firmware starts
`.tmp_update/updater` in place of Miyoo's MainUI. Nothing else is installed on
the Flip itself.

- 128 MB of RAM (about 101 MB usable), no swap. This decides what runs; see
  [Limitations](#limitations).
- Wi-Fi uses `plorpOS/8188fu.ko`, Miyoo's own RTL8188FU driver, unmodified.
  The firmware has none of its own.
- Cores are built from pinned upstream source (`mk/flip-cores/cores.lock`;
  licences in `plorpOS/THIRD-PARTY-LICENSES.md`).
- Not included: BIOS files and PICO-8.

### Controls

- **MENU** tapped: the in-game menu, or the shelf menu on the shelf. Held: the
  switcher (System Settings > Menu Hold).
- **Volume keys**: volume. **MENU + volume keys**: brightness. Both volume keys
  together: pause/play Muse+.
- **Hotkeys** (fast-forward, rewind, quick save and load, screenshot): in-game
  menu > Hotkeys.

### Power and lid

- **POWER** tapped, or closing the lid: the screen goes dark and the game
  waits. The Flip has no suspend: after **Sleep Poweroff** (System Settings,
  30 s to 10 min, 2 min to start) it powers off.
- **POWER** held 400 ms: saves and powers off.
- **Lid Close** (System Settings): `sleep` or `nothing`.

### Rumble

The Flip has a vibration motor. **Game Settings > Rumble Enabled** (on by
default) lets games that rumble use it.

### Shelves and cores

| Shelf (`Roms/` folder) | Core |
|---|---|
| NES | FCEUmm |
| SNES | Snes9x 2005 Plus, or Supafaust per game |
| Game Boy, Game Boy Color | Gambatte |
| Game Boy Advance | gpSP |
| Genesis, Master System, Game Gear, Sega CD, 32X | PicoDrive |
| TurboGrafx-16 | Beetle PCE Fast |
| Arcade, Neo Geo | FinalBurn Neo |
| Neo Geo Pocket, Neo Geo Pocket Color | RACE, or Beetle NeoPop per game |
| PlayStation | PCSX ReARMed |
| Pico-8 | fake-08, or native PICO-8 |

A shelf with two cores has a **Core** row in its shelf menu, set per game.

- Save states belong to one core and don't load in the other.
- Neo Geo Pocket battery saves don't carry over either (RACE `.ngf`, Beetle
  NeoPop `.flash`).
- Beetle NeoPop is about 3x slower than RACE. Use it for games RACE can't run
  (the Cool Cool Jam prototype stays black on RACE).
- Supafaust: better for Super FX (Star Fox, Stunt Race FX run full speed) and
  holds 60 fps on light games. Slower on SA-1 games (Kirby Super Star, Super
  Mario RPG): keep those on Snes9x 2005 Plus.
- Star Fox on Snes9x 2005 Plus holds full speed by skipping about 4% of frame
  draws.
- Game Boy Advance runs on gpSP (mGBA lagged).

### Arcade and Neo Geo

FinalBurn Neo runs both shelves.

- ROM sets must match it: a current FinalBurn Neo set, non-merged is safest.
- Arcade games go in `Roms/Arcade`. `neogeo.zip` goes in `Roms/Neo Geo` or
  `Bios/`; the shelf doesn't list BIOS files.
- Every Neo Geo game that fits in memory and all CPS-1 and CPS-2 games run at
  full speed. About 1 arcade game in 8 runs slower (tested on 786 games):
  After Burner II, G-LOC, Space Harrier, Strikers 1945 II, R-Type Leo, Raiden
  DX and Xexex among them.
- Too big for the Flip (about 18 Neo Geo games): Garou, The King of Fighters
  '98 to 2003, Last Blade 2, Matrimelee, Metal Slug 3, 4 and 5, Rage of the
  Dragons, Real Bout Fatal Fury 2, Samurai Shodown V and V Special, Strikers
  1945 Plus, SVC Chaos. The King of Fighters '97 and smaller run.
- Also too big: about 30 large arcade games, including Gunbird 2, Strikers
  1945 III and Raiden Fighters.
- A game that can't run shows FinalBurn Neo's error. Press **MENU**, then
  **Save and Quit**. Some too-big games return to the shelf by themselves after
  about 10 seconds.

### Game Boy and Game Boy Color

Gambatte runs both.

- **Palette** (in-game menu) changes a Game Boy game's colours at once. The
  shelf menu's **Default Palette** covers games without their own choice; a
  game's own pick stays if the default changes later. On
  the Game Boy Color shelf the row shows only for original Game Boy games.
- Cartridge clocks (Pokémon Gold, Silver, Crystal) are kept in a `.rtc` file
  beside the `.srm`.

### Scanline Filter

The Flip has no shaders. **Scanline Filter** (shelf menu and in-game menu)
turns a light scanline look on or off per system, at no speed cost.

- Needs the game shown at 2x or larger; smaller pictures are drawn plainly.
- Not on native PICO-8.

### Native PICO-8

1. From your PICO-8 **Raspberry Pi** download, copy `pico8_dyn` and `pico8.dat`
   into `Bios/`. (Tested with 0.2.7.)
2. PICO-8 shelf menu > **Core** > **Pico-8 Native**. The same row switches
   back to fake-08 (the default).

- **Splore** is the first entry on the shelf when `Bios/pico8_dyn` is there. It
  needs Wi-Fi. A cart played in Splore is added to the shelf as
  `<title>.p8.png` when you leave. Delete it and it stays gone until a newer
  version is played.
- **MENU** in a game: Continue, Display, Reset, Splore, Quit. A **Display**
  change applies at the next Reset.
- **Start**: PICO-8's own pause menu. **B** is O, **A** is X.
- Settings, cart data and Splore downloads are in `Saves/Pico-8/`, shared with
  fake-08.
- No save states, rewind, fast-forward or Scanline Filter.

### Wi-Fi and network time

Turn Wi-Fi on under **MENU > Wi-Fi**. Joining a network sets the clock (it
waits up to 2 minutes for the connection).

Online features (RetroAchievements, art and name downloads, Splore) use a
`curl` shipped in `plorpOS/`; the firmware has none.

### SSH

**MENU > Network Services > SSH** starts a server on port 22 (off on a fresh
card; the setting survives a reboot).

- Log in as `plorpos`, password `plorpos` (a root account; starts in the card
  root). Anyone on your network can use that password while SSH is on, so
  switch it off when you don't need it.
- Keys: add your public key to `plorpOS/ssh/authorized_keys`, one per line, and
  log in as `plorpos` or `root`.
- The host key is made on first start and kept in `plorpOS/ssh/`.
- No sftp, so `scp` fails. Use `ssh plorpos@<flip> 'cat > /path' < file`.

### Limitations

- No Bluetooth, Tailscale, Syncthing or Volume Limiter.
- Muse+: no Plex or Jellyfin (a big library doesn't fit in memory), and no
  Playback Settings (no Sleep Button Lock or Paused Sleep Delay).
- No Auto Off: Sleep Poweroff takes its place.
- Achievements are off by default (watching a set can cost a heavy game its
  full speed). RetroAchievements sign-in is untested end to end.
- **PlayStation** is experimental: full speed in testing, but only Ace Combat 2
  was played at length and memory is tight. No rewind.
- No shelves for Nintendo DS, Ports, Lynx, WonderSwan, Atari or 7800.

### If plorpOS can't start

If `plorpOS/launch.sh` is missing, or the launcher fails 5 times in a row, the
hook hands the Flip back to Miyoo's MainUI and renames itself to
`.tmp_update.off` (the firmware would delete it otherwise).

- To boot plorpOS again: fix the card on a computer, then rename
  `.tmp_update.off` back to `.tmp_update`.
- An empty file `plorpOS/.boot-idle` makes the hook wait instead (useful with
  SSH on).
- **Never kill the process `/customer/main` started from
  `.tmp_update/updater`.** The firmware reads that as a failed update and
  deletes all of `.tmp_update/`.

### Faster boot (optional)

A separate download, `plorpOS-miyoominiflip-fast-boot.zip`, gets the Flip to
the shelf about 6 seconds sooner, games and sound ready when it shows. The
Miyoo splash pictures are replaced by black (the boot logo stays).

- **It writes the Flip's internal flash**: the `/customer` and `miservice`
  partitions and one bootloader setting. The bootloader, kernel and system are
  not touched.
- **Firmware build 202510011046 only** (what the Flip ships with). Don't use it
  on another build.
- Not affiliated with Miyoo. Use at your own risk.

Install:

1. Charge the Flip or keep it plugged in. Don't interrupt the update.
2. Put `miyoo285_fw.img` from the download in the root of the card (only one
   file with that name).
3. Power the Flip fully off and unplug the cable.
4. Hold **MENU**, plug in the USB cable, and keep MENU held until the update
   screen shows. **Don't press POWER.** A normal power-on or restart doesn't
   start the update.
5. It writes for a short while, then restarts into plorpOS.

You can delete `miyoo285_fw.img` afterwards. Left there, it only runs when you
plug in the cable while holding MENU.

### Faster boot: undo or recover

1. Put Miyoo's own `miyoo285_fw.img` for the Mini Flip (build 202510011046,
   from lomiyoo.com/pages/firmware) in the card root.
2. Repeat install steps 3 to 5.

- This puts back everything the fast-boot update changed.
- Miyoo's update takes 5 to 8 minutes and rewrites the bootloader: keep the
  Flip plugged in and don't unplug until it restarts.
- It resets the Flip's own settings, saved Wi-Fi networks included. Join them
  again afterwards.
- If the fast-boot update itself is interrupted, the bootloader is untouched:
  the same steps bring the Flip back.

### Faster boot: what it changes

- `/customer/main` (start-up script): no 1 s wait, no `sdldisplay` splash
  pictures, SD card checked every 0.1 s instead of 0.5 s, two settings read
  with `sed` instead of `jsonval`.
- `/customer/demo.sh`: unused drivers not loaded (network file systems, NTFS,
  USB storage, USB keyboards/mice).
- `mi_sys.ko` in `miservice`: one byte, so the first program to use display and
  sound waits 0.1 s instead of 2 s.
- Bootloader start-up command: `sleepms 1000` removed.

Source: `mk/flip-fw/lean-customer.patch`; `mk/flip-fw/build-lean.sh` rebuilds
the image from your own copy of Miyoo's firmware file. Thanks to tenlevels for
finding the last three.

# Adding Content

| Folder | What |
|---|---|
| `Roms/<System>/` | games, one folder per system (see [Shelves and cores](#shelves-and-cores)) |
| `Bios/` | BIOS files, loose (Sega CD, PlayStation, PC Engine CD, Neo Geo) |
| `Music/` | albums, one folder each |
| `Audiobooks/` | books |

A `BIOS/` folder must be renamed `Bios/` in two steps (`BIOS` to `Bios.tmp`,
then to `Bios`): FAT ignores a case-only rename.

### Copy them to the card

Put the card in a computer and copy into the folders above.

### Over Wi-Fi

Join a network under **MENU > Wi-Fi**. Then **MENU > Network Services > Over
The Hare** shows an address and a PIN (new each time). Open the address in a
browser, enter the PIN and drag files into their folders. They're on the shelf
when you leave the screen. See the [guide](guide.md).

# Updating plorpOS

Settings, saves, play time and achievements (`.userdata/`, `Saves/`) are kept
either way.

### Over Wi-Fi

1. **MENU > Network Services > Check for update.**
2. Confirm the download, then the restart.

Details: [guide](guide.md#check-for-update).

### By hand

1. Unzip `plorpOS-v<version>-miyoominiflip.zip`.
2. Copy its contents onto the card, replacing what is there.

# Removing plorpOS

### Steps

1. Delete `plorpOS/` and `.tmp_update/` from the card. The Flip then boots
   Miyoo's MainUI.
2. If you installed the faster boot, undo it
   ([Faster boot: undo or recover](#faster-boot-undo-or-recover)).

`Roms/`, `Bios/` and `Saves/` stay.

# FAQ

### I'm upgrading a 1.9.x card. What happens to `TortOS/`?

1.9.x used a `TortOS/` folder. The first start after updating moves anything
in it that `plorpOS/` doesn't have (your PICO-8 files, say) and deletes the
rest.

### My old Arcade (FBA 2012) and Arcade (MAME 2000) shelves are gone

Those folders aren't read any more. Move the sets you want to `Roms/Arcade`.
Save states from the old cores don't load.

### Do my mGBA Game Boy saves still work?

Battery saves (`.srm` in `Saves/<console>/`) load as before. mGBA save states
don't load in Gambatte.

### "needs Bios/pico8_dyn" when starting a PICO-8 game

The shelf's Core is Pico-8 Native but `Bios/pico8_dyn` is missing. Copy it
([Native PICO-8](#native-pico-8)) or switch Core back to fake-08.

### Splore shows no carts

Wi-Fi is off or not connected.

### A Display change did nothing in native PICO-8

Choose Reset in its menu.
