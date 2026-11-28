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

### 3. Boot

Optional check first, from the card root: `md5sum -c MD5SUMS`. Then put the
card in the Flip and power on.

# Device specific notes for the Miyoo Mini Flip

### Base system

plorpOS runs on Miyoo's stock firmware, which starts `.tmp_update/updater` in
place of MainUI. Nothing is installed on the Flip itself.

- 128 MB of RAM (about 101 MB usable), no swap. This decides what runs; see
  [Limitations](#limitations).
- Wi-Fi uses `plorpOS/8188fu.ko` (Miyoo's RTL8188FU driver, unmodified) and a
  `curl` in `plorpOS/`; the firmware has neither. Joining a network sets the
  clock (waits up to 2 minutes for the connection).
- Cores are built from pinned upstream source (`mk/flip-cores/cores.lock`;
  licences in `plorpOS/THIRD-PARTY-LICENSES.md`).
- Not included: BIOS files and PICO-8.

### Controls and power

- Both volume keys together: pause/play Muse+.
- **POWER** tapped, or closing the lid: the screen goes dark and the game
  waits. No suspend: after **Sleep Poweroff** (System Settings, 30 s to 10 min,
  2 min to start) it powers off.
- **POWER** held 400 ms: saves and powers off.
- **Lid Close** (System Settings): `sleep` or `nothing`.

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

Two cores: a **Core** row in the shelf menu, also per game.

- Save states don't load in the other core. Neo Geo Pocket battery saves don't
  either (RACE `.ngf`, Beetle NeoPop `.flash`).
- Beetle NeoPop is about 3x slower than RACE: use it for games RACE can't run
  (the Cool Cool Jam prototype stays black on RACE).
- Supafaust: better for Super FX (Star Fox, Stunt Race FX at full speed) and
  60 fps on light games; slower on SA-1 (Kirby Super Star, Super Mario RPG):
  keep those on Snes9x 2005 Plus. Star Fox on Snes9x 2005 Plus holds full
  speed by skipping about 4% of frame draws.
- Game Boy Advance runs on gpSP (mGBA lagged).

### Arcade and Neo Geo

- ROM sets must match FinalBurn Neo: a current set, non-merged is safest.
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
- A game that can't run shows FinalBurn Neo's error: **MENU** > **Save and
  Quit**. Some too-big games return to the shelf by themselves after about
  10 s.

### Game Boy and Game Boy Color

- **Palette** (in-game menu) applies at once. The shelf's **Default Palette**
  covers games without their own pick; a game's pick stays if the default
  changes. On the Game Boy Color shelf the row shows only for Game Boy games.
- Cartridge clocks (Pokémon Gold, Silver, Crystal) are kept in a `.rtc` beside
  the `.srm`.

### Scanline Filter

No shaders. **Scanline Filter** (shelf and in-game menu, in place of Shader)
turns light scanlines on or off per system, at no speed cost. Needs the game at
2x or larger. Not on native PICO-8.

### Native PICO-8

1. Copy `pico8_dyn` and `pico8.dat` from your PICO-8 **Raspberry Pi**
   download into `Bios/` (tested with 0.2.7).
2. PICO-8 shelf menu > **Core** > **Pico-8 Native** (fake-08 is the default).

- **Splore** is the shelf's first entry when `Bios/pico8_dyn` is there; needs
  Wi-Fi. A cart played in Splore lands on the shelf as `<title>.p8.png` when you
  leave; deleted, it stays gone until a newer version is played.
- **Start**: PICO-8's pause menu. **B** is O, **A** is X.
- Settings, cart data and Splore downloads: `Saves/Pico-8/`, shared with
  fake-08.
- No save states, rewind, fast-forward or Scanline Filter. Menu: see
  [guide](guide.md).

### SSH

**MENU > Network Services > SSH** starts a server on port 22 (off on a fresh
card; the setting survives a reboot).

- Log in as `plorpos`, password `plorpos` (a root account; starts in the card
  root). Anyone on your network can use it: switch SSH off when not needed.
- Keys: add your public key to `plorpOS/ssh/authorized_keys`, one per line, and
  log in as `plorpos` or `root`.
- The host key is made on first start and kept in `plorpOS/ssh/`.
- No sftp, so `scp` fails. Use `ssh plorpos@<flip> 'cat > /path' < file`.

### Limitations

- No Bluetooth, Tailscale, Syncthing or Volume Limiter.
- Muse+: no Plex or Jellyfin (a big library doesn't fit in memory), and no
  Playback Settings (no Sleep Button Lock or Paused Sleep Delay).
- No Auto Off: Sleep Poweroff takes its place.
- Achievements are off by default (they can cost a heavy game full speed). RetroAchievements sign-in is untested end to end.
- **PlayStation** is experimental: full speed in testing, but only Ace Combat 2
  was played at length and memory is tight. No rewind.
- No shelves for Nintendo DS, Ports, Lynx, WonderSwan, Atari or 7800.

### If plorpOS can't start

If `plorpOS/launch.sh` is missing, or the launcher fails 5 times in a row, the
Flip boots Miyoo's MainUI and `.tmp_update` is renamed `.tmp_update.off`.

- To boot plorpOS again: fix the card on a computer, then rename
  `.tmp_update.off` back to `.tmp_update`.
- An empty file `plorpOS/.boot-idle` makes the hook wait instead (useful with
  SSH on).
- **Never kill the process `/customer/main` started from
  `.tmp_update/updater`.** The firmware reads that as a failed update and
  deletes all of `.tmp_update/`.

### Faster boot (optional)

`plorpOS-miyoominiflip-fast-boot.zip` (separate download) reaches the shelf
about 7 s sooner, games and sound ready. Miyoo's splash pictures become black;
the boot logo stays.

- **It writes the Flip's internal flash**: the `/customer` and `miservice`
  partitions and one bootloader setting. Bootloader, kernel and system are not
  touched.
- **Firmware build 202510011046 only** (what the Flip ships with).
- Not affiliated with Miyoo. Use at your own risk.

Install:

1. Charge the Flip or keep it plugged in. Don't interrupt the update.
2. Put `miyoo285_fw.img` from the download in the card root (only one file with
   that name).
3. Power the Flip fully off and unplug the cable.
4. Hold **MENU**, plug in the USB cable, keep MENU held until the update screen
   shows. **Don't press POWER.** A normal power-on or restart doesn't start it.
5. It writes briefly, then restarts into plorpOS.

You can delete `miyoo285_fw.img` afterwards; left there, it runs only on
MENU + plug-in.

Undo or recover (also after an interrupted fast-boot update, which leaves the
bootloader untouched):

1. Put Miyoo's own `miyoo285_fw.img` for the Mini Flip (build 202510011046,
   from lomiyoo.com/pages/firmware) in the card root.
2. Repeat install steps 3 to 5. It puts back everything fast boot changed.

- Miyoo's update takes 5 to 8 minutes and rewrites the bootloader: keep the
  Flip plugged in until it restarts.
- It resets the Flip's own settings, saved Wi-Fi networks included.

What it changes (`mk/flip-fw/lean-customer.patch`; `mk/flip-fw/build-lean.sh`
rebuilds it from your own copy of Miyoo's firmware):

- `/customer/main`: no 1 s wait, no `sdldisplay` splash pictures, SD card
  checked every 0.1 s instead of 0.5 s, settings read with `sed` instead of
  `jsonval`.
- `/customer/demo.sh`: unused drivers not loaded (network file systems, NTFS,
  USB storage, USB keyboards/mice).
- `mi_sys.ko` in `miservice`: one byte, so the first program to use display and
  sound waits 0.1 s instead of 2 s.
- Bootloader: `sleepms 1000` removed.

Thanks to tenlevels for finding the last three.

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

**MENU > Network Services > Over The Hare** (Wi-Fi on): open the address it
shows in a browser, enter the PIN and drag files in. See the
[guide](guide.md).

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
   ([Faster boot](#faster-boot-optional)).

`Roms/`, `Bios/` and `Saves/` stay.

# FAQ

### I'm upgrading a 1.9.x card. What happens to `TortOS/`?

The first start moves anything in it that `plorpOS/` lacks (your PICO-8 files,
say) and deletes the rest.

### My old Arcade (FBA 2012) and Arcade (MAME 2000) shelves are gone

They aren't read any more. Move the sets to `Roms/Arcade`. Their old save
states don't load.

### Do my mGBA Game Boy saves still work?

Battery saves (`.srm` in `Saves/<console>/`) load as before. mGBA save states
don't load in Gambatte.

### "needs Bios/pico8_dyn" when starting a PICO-8 game

Copy `Bios/pico8_dyn` ([Native PICO-8](#native-pico-8)) or switch Core back
to fake-08.

### Splore shows no carts

Wi-Fi is off or not connected.

### A Display change did nothing in native PICO-8

Choose Reset in its menu.
