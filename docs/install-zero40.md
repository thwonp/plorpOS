# Installing on the MagicX Zero 40

*There is no Zero 40 release yet. This guide is for the next release.*

On the Zero 40, plorpOS is the launcher on top of
[oakMOSS](https://github.com/spruceUI/oakMOSS), the small base system that
spruceOS uses on this handheld. oakMOSS starts the board and hands off to
plorpOS on the card.

The Zero 40's screen is tall (480x800), so plorpOS stands on end: the shelves
scroll up and down, the list layouts put the art above the list, and Now
Playing puts the cover above the track. Landscape games sit in the middle of
the screen at their own shape. DS games show both screens stacked, the full
width of the panel.

You need the Zero 40, a microSD card for oakMOSS (and optionally a second one
for your games) and a computer.

## 1. Flash the image

The release has one image for the Zero 40: oakMOSS (Shaun Inman's boot chain with our system) with plorpOS
already in it. Back up the Zero 40's stock card first: it is the only way back.

- **New card:** write `plorpOS-v<version>-zero40.img.xz` to a microSD card with
  [Etcher](https://etcher.balena.io/) or `dd`. It replaces everything on the
  card.
- **A card that already runs oakMOSS:** copy `plorpOS-v<version>-zero40.omupd`
  to the root of the card's `SPRUCEOS` partition and reboot. The board
  installs it, restarts and deletes the file. Your games and saves stay. It
  waits while the battery is under 30% with no charger.

The first boot adds a FAT32 partition called `SPRUCEOS` to the free space on
the card and puts plorpOS on it. That is the partition a computer sees as an
ordinary drive.

## 2. What is on the card

```
.tmp_update/updater   oakMOSS starts plorpOS through this
plorpOS/              the launcher, its cores and settings
Roms/                 one folder per system
Saves/  Bios/  Music/  Audiobooks/
```

The image puts these on the card only when the card has no launcher yet. A
card that already has plorpOS, spruceOS or anything else is left alone, and
plorpOS updates itself from then on (see Updating).

**Already running oakMOSS and want to keep it?** Unzip
`plorpOS-v<version>-zero40.zip` onto the first card's `SPRUCEOS` partition
instead, or onto a second card (SD2) formatted FAT32; oakMOSS cannot read
exFAT. If the second card has a launcher on it (spruceOS, say), it wins and
plorpOS on the first card does not start.

The zip is for the Zero 40 only.

## 3. Add games

Games go in `Roms/<System>/`, in the folder named for their system. BIOS files
go loose in `Bios/`. Music goes in `Music/`, one folder per album.

## Controls

| Button | Does |
|---|---|
| Up / Down | Move along the shelf |
| Left / Right | Jump by letter |
| L1 / R1 | A screenful |
| A | Open or play |
| B | Back |
| X | Game details |
| Y | Favorite |
| Select | Muse+ |
| Menu | The plorpOS menu; in a game, the game menu |
| Menu + Volume | Brightness |

Touch works on the shelves and in the menus:

| Touch | Does |
|---|---|
| tap | selects what is under it; tap again for A |
| swipe | moves along the shelf or the list |
| swipe in from the left or right edge | Back |
| swipe down from the top edge | Muse+ (or what Select Shortcut says), also in a game |
| swipe up from the bottom edge | Recent games (or what Menu Hold says), also in a game |
| hold | Favorite (Y) |
| two-finger tap | Game details (X) |
| three-finger tap | Menu |

In a DS game the touches go to the lower screen.

## Display

Games start in **Aspect**: their own shape, as wide as the screen. A system's
menu > **Defaults** sets its Display Mode, and the game menu's Display Mode changes it for
one game. DS games start in **Even Split**, both screens stacked; the game
menu's Layout row has the others.

## Wi-Fi and ssh

Switch Wi-Fi on in the plorpOS menu, under Wi-Fi. The **SSH** row in Network Services starts
an ssh server on port 22. It is off on a fresh card. Log in as `root` with a
key: put your public key (the contents of `~/.ssh/id_ed25519.pub`, for
example) in `plorpOS/ssh/authorized_keys` on the card, one key per line.
There is no password login. The server makes its own host key the first time
it starts and keeps it in `plorpOS/ssh/`. There is no sftp, so `scp` fails;
copy with `ssh root@<zero40> 'cat > /path' < file`.

## Ports

The **Ports** shelf sits above Muse+. Its first entry is **PortMaster**; open
it with the device on Wi-Fi to install ports, which land in `Roms/Ports/` and
appear on the shelf. A port is a script (`Name.sh`) beside a folder of the
same name, so ports can also be copied onto the card. PortMaster itself lives
in `.userdata/shared/portmaster/`. This is unofficial support: report problems
to plorpOS, not the PortMaster team.

What to expect:

- **Menu** opens the plorpOS menu over a port (Continue, Reset, Quit); Quit
  ends the port and returns to the shelf.
- The screen is portrait, so most ports fit its width with black above and
  below. A few stretch to fill it (6 Feet Under, Ren'Py games) or draw past
  its edge (Cave Story).
- Ports that need a game's own files (Downwell, To the Moon, Stardew Valley)
  have to be given them: put them where the port's page says. A port may
  download a runtime the first time, so do that on Wi-Fi.
- 32-bit ports run on software graphics: the Zero 40's GPU has no 32-bit
  driver. GameMaker ones play slowly (MEGA MAN Sunrise about 15 fps). Ports
  that run through box86 (Braid, Shovel Knight) do not work.
- Dust Racing 2D does not start until PortMaster fixes the runtime name its
  script asks for.
- PortMaster's tools ship as read-only images in `plorpOS/zero40/portmaster/`,
  mounted only while a port runs. The first port adds three things to
  oakMOSS: a udev rule for the pad, `/run`, and the links a 32-bit port needs
  (`/usr/lib32` and its neighbours).

## Limits

- Charging can slow to a trickle after a USB DAC is plugged in or pulled out
  with a charger connected. Re-plug the charger. The fix is in oakMOSS
  ([guide](guide.md#usb-dac)).
- No Bluetooth yet.
- Native PICO-8 needs your own `pico8_64` and `pico8.dat` in `Bios/`;
  without them the PICO-8 shelf plays carts on the fake08 core.
- Plex downloads each track before playing it, as on the H700 devices.
- Below and above a landscape game the screen is black.

## Updating

Two things update separately. The base system (oakMOSS, the kernel, the DAC
drivers) comes from a new `.omupd` file, copied to the card as in step 1. plorpOS
itself updates from the menu, below. An image update never replaces the
plorpOS on a card that already has it.

From v1.10, **MENU > Network Services > Check for update** does this over
Wi-Fi ([guide](guide.md#check-for-update)). By hand:

Copy everything in the new zip onto the card, replacing what is there. Your
settings, saves, play time and achievements are in `.userdata/` and `Saves/`
and stay as they are.

## Removing plorpOS

Delete `plorpOS/` and `.tmp_update/` from the card. oakMOSS then starts
spruceOS from the other card if it has it, or shows its "No frontend" screen.
Your `Roms/`, `Saves/` and `Bios/` stay.
