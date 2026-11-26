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

## 1. Put oakMOSS on the first card

Follow oakMOSS's own guide, [Using oakMOSS](https://github.com/spruceUI/oakMOSS/blob/main/docs/using.md).
Back up the Zero 40's stock card first: it is the only way back. Then boot the
new card once in the Zero 40. That first boot adds a FAT32 partition called
`SPRUCEOS` to the free space on the card, which is where plorpOS can go.

## 2. Copy plorpOS onto a card

Unzip `plorpOS-v<version>-zero40.zip` onto either card:

- **The first card's `SPRUCEOS` partition**, for one card that does
  everything. A computer sees it as an ordinary drive.
- **A second card (SD2)**, formatted FAT32. oakMOSS cannot read exFAT.

If the second card has a launcher on it (spruceOS, say), it wins and plorpOS
on the first card does not start. Take spruceOS off that card, or put plorpOS
on it instead.

```
.tmp_update/updater   oakMOSS starts plorpOS through this
plorpOS/              the launcher, its cores and settings
Roms/                 one folder per system
Saves/  Bios/  Music/  Audiobooks/
```

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
| hold | Favorite (Y) |
| two-finger tap | Game details (X) |
| three-finger tap | Menu |

In a DS game the touches go to the lower screen.

## Display

Games start in **Aspect**: their own shape, as wide as the screen. A system's
menu has its Default Display Mode, and the game menu's Display changes it for
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
  its edge (Stardew Valley, Cave Story).
- Ports that need a game's own files (Downwell, To the Moon, Stardew Valley)
  have to be given them: put them where the port's page says. A port may
  download a runtime the first time, so do that on Wi-Fi.
- Only 64-bit ports are offered. 32-bit ports are not supported yet.
- Dust Racing 2D does not start until PortMaster fixes the runtime name its
  script asks for.
- PortMaster's tools ship as read-only images in `plorpOS/zero40/portmaster/`,
  mounted only while a port runs. The first port adds two things to
  oakMOSS: a udev rule for the pad, and `/run`.

## Limits

- No Bluetooth yet.
- No native PICO-8. The PICO-8 shelf plays carts on the fake08 core.
- Plex downloads each track before playing it, as on the H700 devices.
- Below and above a landscape game the screen is black.

## Updating

From v1.10, **MENU > Network Services > Check for update** does this over
Wi-Fi ([guide](guide.md#check-for-update)). By hand:

Copy everything in the new zip onto the card, replacing what is there. Your
settings, saves, play time and achievements are in `.userdata/` and `Saves/`
and stay as they are.

## Removing plorpOS

Delete `plorpOS/` and `.tmp_update/` from the card. oakMOSS then starts
spruceOS from the other card if it has it, or shows its "No frontend" screen.
Your `Roms/`, `Saves/` and `Bios/` stay.
