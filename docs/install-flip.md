# Installing on a Miyoo Mini Flip

*v1.9.x installs to `TortOS/`; `plorpOS/` starts with the next release.*

The Flip has 128 MB of RAM (about 101 MB usable) and no swap, which decides
what runs; see "Limits".

## What this zip is

A complete card: the boot hook, plorpOS, its cores, ssh and the Wi-Fi driver.
Nothing else is needed on the card besides Miyoo's stock firmware on the Flip
itself, which starts the hook (`.tmp_update/updater`) at power-on in place of
Miyoo's MainUI.

- **Cores** are built from pinned upstream source for the Flip
  (`mk/flip-cores/cores.lock` in the source tree lists each one's commit and
  licence; `plorpOS/THIRD-PARTY-LICENSES.md` on the card).
- **Wi-Fi driver**: `plorpOS/8188fu.ko`, Miyoo's own Realtek RTL8188FU module
  for the Flip's kernel, unmodified. The stock firmware loads the radio driver
  from the SD card and has none of its own.
- **ssh** (dropbear), off until you switch it on (see "Wi-Fi and ssh").
- **Not included**: BIOS files and PICO-8 itself. Both are yours to supply.

## Install

1. Start from an empty FAT32 card (exFAT is untested) with a Master Boot
   Record partition scheme, or back up the card
   you have. If it has a `.tmp_update/` folder from another firmware, that
   firmware stops booting: the zip replaces its `updater`.
2. Download `plorpOS-v<version>-flip.zip` from
   [Releases](https://github.com/thwonp/plorpOS/releases/latest) and unzip it.
   Copy everything in it to the root of the card, merging folders. It adds:
   ```
   .tmp_update/updater   the boot hook
   plorpOS/              the launcher, diatom, Muse+, cores, ssh, the Wi-Fi driver, fonts, cards
   Roms/<console>/       empty folders named as the shelves expect
   Bios/  Saves/
   MD5SUMS               a checksum for every file
   INSTALL-FLIP.md       this guide
   ```
3. Put your ROMs in `Roms/<console>/`.
4. Firmware (Sega CD, PlayStation, PC Engine CD, Neo Geo) is looked for in
   `Bios/`. If your card has a `BIOS/` folder, rename it to `Bios/` in two
   steps (`BIOS` to `Bios.tmp`, then to `Bios`), because FAT treats a
   case-only rename as no change and then cannot find the folder.
5. `MD5SUMS` lists every file in the zip. Check with `md5sum -c MD5SUMS` from
   the card root.
6. Eject, put the card in, power on.

## Updating from an earlier version

Copy everything in the new zip onto the card, replacing what's there. Your
settings, saves, play time and achievements live in `.userdata/` and `Saves/`
and stay as they are.

Before v1.10 the launcher's folder was `TortOS/`. The first start after
updating moves whatever is still in it that the new `plorpOS/` does not have
(your own PICO-8 files, for one) and deletes the rest.

## Adding games over Wi-Fi

1. In **Menu > Network Services**, turn Wi-Fi on and pick your network.
2. Open **Over The Hare**. It shows an address and a PIN.
3. Open that address in a browser on your phone or computer and type the PIN.
4. Drag games into their console's folder or albums into `Music`. They're on
   the shelf when you leave the screen.

The PIN is new every time you open Over The Hare.

## Shelves and cores

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
| Pico-8 | fake-08, or PICO-8 itself (see "Native PICO-8") |

A shelf with two cores has a **Core** row in its shelf menu, set per game: it
turns the game under the cursor to the other core and back. Save states belong
to one core and do not load in the other; on the Neo Geo Pockets the battery
saves do not carry over either (RACE keeps `.ngf`, Beetle NeoPop `.flash`).
Beetle NeoPop costs about three times RACE's time per frame and is there for
the games RACE does not run (the Cool Cool Jam prototype stays black on RACE).
Supafaust is the better SNES core for the Super FX games (Star Fox, Stunt Race
FX: full speed where Snes9x 2005 Plus skips frames) and holds 60 fps on the
lighter ones, but is slower on the SA-1 games (Kirby Super Star, Super Mario
RPG), where Snes9x 2005 Plus stays the better pick.

## Controls

- **Menu**, tapped: the in-game menu (Continue, Save, Load, Display, Scanline
  Filter, Hotkeys, Reset, Quit and the rows a system adds). On the shelf it
  opens the shelf's menu.
- **Menu**, held: the switcher (recent games, Muse+).
- **Volume keys**: volume. **Menu + volume keys**: brightness. Both volume
  keys together pause or play Muse+.
- **Hotkeys** (fast-forward, rewind, quick save and load, screenshot) are set
  in the in-game menu's **Hotkeys** screen.
- **Power**, tapped, or closing the lid: the screen goes dark and the game
  waits; the Flip has no suspend, so a long wait ends in a power-off.
  **Power**, held: saves and powers off.

## Game Boy and Game Boy Color

Both run on Gambatte, which replaced mGBA in v1.9: it costs about half
as much per frame on the Flip.

- **Palettes**: a Game Boy game takes its colours from the **Palette** row of
  the in-game menu, and the change shows at once. The shelf menu's **Default
  Palette** sets what games without their own choice use; a palette picked in
  a game stays with that game even if the default changes later. On the Game
  Boy Color shelf the row appears only for original Game Boy games.
- **Saves from mGBA**: battery saves (`.srm` in `Saves/<console>/`) are plain
  cartridge memory and should load as before. Save states made with mGBA do
  not load in Gambatte.
- **Clocks**: games with a cartridge clock (Pokémon Gold, Silver, Crystal)
  keep it in a `.rtc` file beside the `.srm`.

## Scanline Filter

The **Scanline Filter** row (shelf menu and in-game menu) turns a light
scanline look on or off per system. It is drawn by the Flip's 2D engine and
costs nothing measurable. It needs the game shown at twice its size or more;
smaller pictures are drawn plainly. It does not apply to native PICO-8.

## Native PICO-8

plorpOS can run PICO-8 itself instead of the fake-08 core, with Splore, its
cart browser. PICO-8 is sold by Lexaloffle and is **not** part of this zip.

1. From your PICO-8 download for **Raspberry Pi**, copy `pico8_dyn` and
   `pico8.dat` into `Bios/` on the card. (Tested with PICO-8 0.2.7.)
2. On the PICO-8 shelf, open the shelf menu and set **Core** to **Pico-8
   Native**. fake-08 stays the default; the same row switches back.
3. **Splore** is the first entry on the PICO-8 shelf whenever `Bios/pico8_dyn`
   is there. It needs Wi-Fi. A cart you play in Splore is added to the shelf
   as `<title>.p8.png` when you leave; if you delete it from the shelf it stays
   gone until a newer version of it is played.

In a native game, **Menu** freezes PICO-8 and shows Continue, Display, Reset,
Splore and Quit. A **Display** change (Aspect, Integer, Stretch) applies at the
next Reset. PICO-8's own pause menu is on **Start**. Buttons: **B** is O,
**A** is X. PICO-8 keeps its settings, cart data and Splore downloads in
`Saves/Pico-8/`, where fake-08 keeps its cart data too, so the two share them.

Limits of native PICO-8: no save states, rewind or fast-forward (PICO-8 has
its own cart saves); no Scanline Filter.

## Wi-Fi and ssh

Wi-Fi is switched on in the plorpOS menu, Network Services. Its **SSH** row
starts an ssh server on port 22 and is **off** on a fresh card. Log in as
**`plorpos`**, password **`plorpos`** (a root account; it starts in the card's
root folder). Keys work too: put your public key (for example the contents of
`~/.ssh/id_ed25519.pub`) in `plorpOS/ssh/authorized_keys` on the card, one key
per line, and log in as `plorpos` or `root`. Anyone on your network who knows
the default password can log in while SSH is on, so switch it off when you do
not need it. The server's own host key is made the first time it starts and
kept in `plorpOS/ssh/`. The setting survives a reboot. The card has no sftp
server, so `scp` fails; copy with `ssh plorpos@<flip> 'cat > /path' < file`.

## Limits

- **Arcade** and **Neo Geo** run on FinalBurn Neo; ROM sets must match it (a
  current FinalBurn Neo set, non-merged is safest). Arcade games go in
  `Roms/Arcade`. `neogeo.zip` can sit in `Roms/Neo Geo` or `Bios/`; the shelf
  does not list BIOS files. Earlier releases had `Arcade (FBA 2012)` and
  `Arcade (MAME 2000)` shelves: those folders are no longer read, so move the
  sets you want to `Roms/Arcade`. Save states from the old cores do not load.
- **Speed**: tested on a full set of 786 games. Every Neo Geo game that fits in
  memory and all CPS-1 and CPS-2 games (Street Fighter II, Marvel vs. Capcom,
  1944) run at full speed. About one arcade game in eight runs slower, mostly
  on heavier boards: After Burner II, G-LOC, Space Harrier, Strikers 1945 II,
  R-Type Leo, Raiden DX and Xexex among them.
- **Memory**: about 18 Neo Geo games need more than the Flip has: Garou, The
  King of Fighters '98 to 2003, Last Blade 2, Matrimelee, Metal Slug 3, 4 and 5,
  Rage of the Dragons, Real Bout Fatal Fury 2, Samurai Shodown V and V Special,
  Strikers 1945 Plus and SVC Chaos. The King of Fighters '97 and smaller run.
  About 30 large arcade games are too big as well, Gunbird 2, Strikers 1945 III
  and Raiden Fighters among them.
- **A game that cannot run** (too big, or a set that does not match) shows
  FinalBurn Neo's error message; press **Menu**, then **Quit**. Some too-big
  games go back to the shelf by themselves after about 10 seconds instead.
- **PlayStation** is **experimental**: it held full speed in testing, but only
  one game (Ace Combat 2) was checked at length, and memory is tight, so other
  titles may not run as well. Rewind is not offered for it.
- **Online features** (RetroAchievements sign-in, art and name downloads,
  Splore) use a curl bundled in `plorpOS/` because the Flip's firmware has none.
  They need Wi-Fi; RetroAchievements sign-in has not been tested end to end.
- **Nintendo DS, Ports, Lynx, WonderSwan, Atari, 7800**: no shelves.
- Super Nintendo's Star Fox (Super FX) holds full speed by leaving out an
  occasional frame draw (about 4% of frames in testing); the emulation itself
  runs at stock speed.
- Game Boy Advance runs on gpSP, not mGBA (mGBA lagged in play).

## Troubleshooting

- **"needs Bios/pico8_dyn"** when starting a PICO-8 game: the shelf's Core is
  Pico-8 Native but `Bios/pico8_dyn` is missing. Copy it (step 1 above) or
  switch Core back to fake-08.
- **Splore shows no carts**: Wi-Fi is off or not connected.
- **A Display change did nothing in native PICO-8**: choose Reset in its menu.

## If plorpOS cannot start

When `plorpOS/launch.sh` is missing, or the launcher fails five times in a row,
the hook hands the Flip back to Miyoo's stock MainUI and renames itself to
`.tmp_update.off` (the stock firmware would otherwise delete it). To boot
plorpOS again, fix the card from a PC and rename `.tmp_update.off` back to
`.tmp_update`. An empty file `plorpOS/.boot-idle` makes the hook wait instead
(useful with ssh on).

**Never kill the process that stock `/customer/main` started from
`.tmp_update/updater`.** The firmware reads that as a failed update and deletes
the whole `.tmp_update/` folder.

## Optional: faster boot

A separate download, **plorpOS Flip fast boot** (`plorpOS-miyoominiflip-fast-boot.zip`
beside this one), makes the Flip start about 4 seconds sooner by flashing one
partition of its internal storage. It is not needed and not part of this zip.
Its own guide (`FAST-BOOT.md`, in that download) has the steps and how to undo
it: the update starts only when you hold MENU while plugging in the USB cable
with the Flip off. It is for the stock firmware build 202510011046 only.

## Removing plorpOS

Delete `plorpOS/` and `.tmp_update/`; the Flip then boots Miyoo's MainUI. Your
`Roms/`, `Bios/` and `Saves/` stay as they are. If you installed the fast-boot
update, undo it as its guide says.
