# Installing plorpOS on the RG Nano

plorpOS nano is a pared-down, music-first plorpOS: consoles, games, Recently
Played, Favorites and Muse, with music playing over a game.

You need the Nano, a microSD card and a computer.

### 1. Download and extract

Get `plorpOS-nano-v<version>-sdcard.zip` from
[plorpos-v1.5.5](https://github.com/thwonp/plorpOS/releases/tag/plorpos-v1.5.5)
(the Nano's latest release) and unzip it.

### 2. Flash the image

Write `plorpOS-nano-v<version>.img` to the card with your imager of choice
([Etcher](https://etcher.balena.io/), [Rufus](https://rufus.ie/), `dd`, etc).
Flashing **wipes the card**.

### 3. Boot

The first start sets the card up (FunKey's six steps on screen, under a
minute), then the shelf comes up.

# Device specific notes for the RG Nano

### Base system

Not the full plorpOS launcher: **nanoshelf** (the shelf) and **PicoArch** (the
games) on
[DrUm78's FunKey-OS build](https://github.com/DrUm78/FunKey-OS/releases/tag/fps-classics),
with FunKey's menus replaced and its extra games and themes removed.

- PicoArch is [plorpOS's fork](https://github.com/thwonp/picoarch/tree/plorpos-nano)
  of [DrUm78's](https://github.com/DrUm78/picoarch), with updated cores and a
  Music page in its menu.
- FunKey's volume, brightness and power keys and its game menu work as before.
- No Wi-Fi.

### Controls

| Button | On the shelf | On Now Playing |
|---|---|---|
| D-pad | move; left/right a page | left/right seek 10 s |
| A | open, play | pause / play |
| B | back | back |
| X | Now Playing | the queue |
| Y | a game: Favorites on/off | play mode (in order, repeat all, repeat one, shuffle) |
| L / R | R in the PICO-8 list: fake08 / native PICO-8 | previous / next track |
| START | pause / play, anywhere | pause / play |
| Power (tap) | dim to Now Playing | brightness back |

- **FN is SELECT.** Everywhere: volume **SELECT + A / Y**, brightness
  **SELECT + X / B**, screenshot to `Screenshots/` **SELECT + Up**.
- **Settings > Controls** shows this, a page per place.

### In a game

Tap **power** for the menu.

- First page, **MUSIC**: A pauses/plays, left/right previous/next track. Up/Down
  change pages, B closes. The rest is FunKey's: volume, brightness, save, load,
  aspect ratio, exit.
- While music plays the game is silent; pause the music to hear it.
- Measured full speed with music: Game Boy, Game Boy Advance, Genesis. Heavier
  systems may slow down.

### Resume and power

- A game's place is saved on exit. Its next start asks **RESUME GAME** (A) or
  **NEW GAME** (asks to be sure); B goes back to the shelf, place kept.
- Power held in a game: saves and turns off. The next start goes straight back
  into it (MUSIC page works); leaving it shows the shelf.
- Power held on the shelf: turns off. **Settings > Power off** does it cleanly.

### Favorites and Dimmed Music Mode

- Y on a game adds or removes it. Favorites shows A-Z under Recently Played
  once it has a game.
- A power tap outside a game dims to Now Playing (lowest backlight, not fully
  dark); music goes on. Tap again for your brightness. While dimmed B and X do
  nothing; with **Dimmed button lock** on, only the power tap works.

### Settings

Last on the shelf; the title shows the version. Left/Right change a choice; A
steps to the next.

- **Output**, **Library**: where music plays, how many tracks it found.
- **Mount card on computer**: the card becomes a USB drive; music stops. Eject
  on the computer, then press B. Only when the Nano started as a USB drive.
- **USB at start**: USB drive, Network (ssh) or adb, after a restart
  ([Connecting](#connecting)).
- **Inactive shutdown**: 1, 2, 5, 10, 30 minutes or Never (default 5): off
  after that long with no button and no music, outside a game.
- **Dimmed button lock**: On (default).
- **Restart**, **Power off**: press A twice.
- **Controls**: the button guide.

The header shows the battery (`+` while charging) and whether music is
playing.

### Native PICO-8

With `pico8_dyn` and `pico8.dat` in `Bios/` (from your PICO-8 Raspberry Pi
download):

- R in the PICO-8 list switches engine; the footer shows **R fake08** or
  **R pico8**.
- No save states. Cart data is in `Saves/Pico-8/native/`.
- A power tap opens the Nano's menu (music, volume, brightness, exit).

### Arcade

- Sets keep their names (`mslug.zip`); lists show titles from each core's
  romset list.
- Vertical games come up upright. PicoArch's **Screen rotation** (game menu
  settings) turns them for holding the Nano sideways.

### USB DAC

- USB-C port. Music moves to it within 2 s; a game started while it is in plays
  through it too. Unplugged, music pauses, on the speaker.
- One USB port: no DAC and computer at the same time.

### Connecting

A shell is only for poking around. Set the USB mode in **Settings > USB at
start** (after a restart) or with a file at the card root:

| File at the root of the drive | The Nano is |
|---|---|
| none | a USB drive (copying files) |
| `usbnet` | a network device: `ssh root@192.168.137.2`, password `funkey`, after giving the computer's side `192.168.137.1/24` |
| `adb` | an adb device: `adb shell` ([platform-tools](https://developer.android.com/tools/releases/platform-tools)) |

### Where things are kept

| What | Where |
|---|---|
| plorpOS (`recent.txt`, `favorites.txt`, `settings.txt`, `queue.txt`) | `/mnt/plorpOS/` |
| Saves | `/mnt/Saves/<console folder>/` |
| BIOS | `/mnt/Bios/` |
| Screenshots | `/mnt/Screenshots/` |
| PicoArch settings | `/mnt/FunKey/.picoarch/` (keep it) |
| Log | `/tmp/nanoshelf.log` (gone at restart) |

`queue.txt` keeps the queue and place over a restart (stopped until play).

### Known issues

- After a USB DAC is unplugged, adb or network may not come back until a
  restart. Once, the Nano restarted by itself.
- The speaker can pop on pause/play and when a DAC is plugged in or out.

# Adding Content

The card's big partition holds:

| Folder | What |
|---|---|
| `Roms/<console>/` | games, one folder per console (below). A console shows once its folder has a game |
| `Bios/` | BIOS files, loose (below) |
| `Saves/<console folder>/` | battery saves and save states, made at first play, named after the game |
| `Music/<artist>/<album>/` | MP3, FLAC, AAC/M4A, Ogg, Opus |
| `FunKey/` | FunKey's and PicoArch's files; keep it |

| Folder in `Roms/` | Shown as | Files |
|---|---|---|
| `Game Boy` | Game Boy | `.gb` `.dmg` `.zip` |
| `Game Boy Color` | Game Boy Color | `.gbc` `.zip` |
| `Game Boy Advance` | Game Boy Advance | `.gba` `.agb` `.gbz` `.bin` `.zip` |
| `NES` | NES | `.nes` `.fds` `.unf` `.unif` `.zip` |
| `SNES` | Super NES | `.smc` `.sfc` `.fig` `.swc` `.gd3` `.gd7` `.dx2` `.bsx` `.zip` |
| `Genesis` | Genesis | `.md` `.gen` `.smd` `.bin` `.32x` `.cue` `.iso` `.chd` `.cso` `.m3u` `.68k` `.sgd` `.pco` `.zip` |
| `Master System` | Master System | `.sms` `.gg` `.sg` `.sc` `.bin` `.zip` |
| `Game Gear` | Game Gear | `.gg` `.zip` |
| `TurboGrafx-16` | PC Engine | `.pce` `.sgx` `.cue` `.ccd` `.chd` `.toc` `.m3u` `.zip` |
| `Neo Geo Pocket` | Neo Geo Pocket | `.ngp` `.ngc` `.ngpc` `.npc` `.zip` |
| `WonderSwan` | WonderSwan | `.ws` `.wsc` `.pc2` `.zip` |
| `Atari Lynx` | Atari Lynx | `.lnx` `.lyx` `.o` `.zip` |
| `Pokemon Mini` | Pokemon Mini | `.min` `.zip` |
| `Pico-8` | PICO-8 | `.p8` `.png` `.zip` |
| `PlayStation` | PlayStation | `.cue` `.bin` `.chd` `.pbp` `.m3u` `.iso` `.img` `.mdf` `.toc` `.cbn` |
| `Arcade (MAME 2000)` | Arcade (MAME) | `.zip` |
| `Arcade (FBA 2012)` | Arcade (FBA) | `.zip` |

BIOS files are all optional:

| System | File | Without it |
|---|---|---|
| Game Boy Advance | `gba_bios.bin` | built-in BIOS; a few games misbehave |
| PlayStation | `scph1001.bin` (or `scph5501.bin`, `scph7001.bin`) | built-in BIOS; less compatible |
| PC Engine CD | `syscard3.pce` | CD games don't start (cards play) |
| Pokemon Mini | `bios.min` | built-in BIOS |
| Atari Lynx | `lynxboot.img` | none needed: plorpOS puts FunKey's there |
| PICO-8 | `pico8_dyn` and `pico8.dat` (PICO-8 Raspberry Pi download) | fake-08 plays carts |

### Copy them to the card

Power off (hold power), put the card in a computer and copy into the folders
above. **Don't format the other partitions if Windows asks.**

### Over USB

Connect the Nano by USB: it starts as a USB drive (**Settings > Mount card on
computer**).

# Updating plorpOS

Settings, saves and lists are kept.

### By hand

1. Download `plorpOS-nano-v<version>-UPDATE.zip` from
   [plorpos-v1.5.5](https://github.com/thwonp/plorpOS/releases/tag/plorpos-v1.5.5)
   and unzip it.
2. Copy the `plorpOS` folder inside over the one on the card (USB drive, or the
   card in a computer).
3. Start the Nano. It shows **INSTALLING PLORPOS** and restarts once.

If the install fails, the old version starts and `plorpOS/update-failed.log` is
left on the card.

# Removing plorpOS

### Steps

1. Copy off what you want to keep. Flashing wipes the card.
2. Flash DrUm78's FunKey-OS image
   ([`FunKey-sdcard-DrUm78_RG_Nano.img`](https://github.com/DrUm78/FunKey-OS/releases/tag/fps-classics))
   in its place.

# FAQ

### I'm on plorpOS nano v0.2. How do I upgrade?

Games moved into `Roms/` and some console folders were renamed. Either flash
this image, or by hand:

1. Move the console folders into `Roms/`.
2. Rename `Sega Genesis`, `Sega Master System`, `PCE-TurboGrafx`, `PS1`,
   `MAME 2000` and `Final Burn Alpha 2012` to the names in the table, in
   `Saves/` too. (`PICO-8` and `Atari lynx` work as they are: the card ignores
   case.)

Recently Played and Favorites forget the games of renamed folders.

### I'm on plorpOS nano v0.1 (the adb install). How do I upgrade?

Flash this image once. It wipes the card, so first copy off your music, games,
BIOS files and `FunKey/.picoarch/` (saves are in its `data/` folders).
Afterwards put BIOS files in `Bios/` and copy each game's `.srm` / `.sav` /
`.st*` files into `Saves/<console folder>/`; the names already match.

### Sources and licences

- The card image is DrUm78's FunKey-OS `fps-classics` image for the RG Nano
  (FunKey-OS 2.3.0; GPL and other licences;
  [DrUm78/FunKey-OS](https://github.com/DrUm78/FunKey-OS/tree/fps-classics),
  mirrored at [thwonp/FunKey-OS](https://github.com/thwonp/FunKey-OS/tree/fps-classics)),
  with plorpOS installed by `mk/nano-image.sh`, which lists every change.
- The kernel is DrUm78's
  ([DrUm78/linux `v1.0-rg-nano`](https://github.com/DrUm78/linux/tree/v1.0-rg-nano),
  mirrored at [thwonp/linux](https://github.com/thwonp/linux/tree/v1.0-rg-nano);
  GPL-2.0), built unchanged with his configuration except the boot logo
  (`mk/build-nano-kernel.sh`, `res/nano/bootlogo.png`).
- plorpOS's own programs, PicoArch, the cores, FFmpeg and the PICO-8 runtime:
  `plorpOS/src/SOURCE.txt` and `/usr/local/plorpos/` on the card, with their
  licences.
