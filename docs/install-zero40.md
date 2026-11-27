# Installing plorpOS on the MagicX Zero 40

You need the Zero 40, a microSD card and a computer.

### 1. Back up the stock card

It is the only way back to stock.

### 2. Reformat the card

Leftover partitions can break the flash. Start from a freshly formatted card.

### 3. Extract the image

Unpack `plorpOS-v<version>-zero40.img.xz` to `plorpOS-v<version>-zero40.img`
first, with [7-Zip](https://www.7-zip.org/) for example. Some imagers fail on
`.xz` files.

### 4. Flash the image

Write `plorpOS-v<version>-zero40.img` to the card with your imager of choice
([Etcher](https://etcher.balena.io/), [Rufus](https://rufus.ie/), `dd`, etc).

### 5. Boot

The first boot can take a minute. It adds a FAT32 partition, `SPRUCEOS`, in the
card's free space and puts plorpOS on it. That is the drive a computer sees.

# Device specific notes for the Zero 40

### Base system

The image is a hybrid: Shaun Inman's `main-zero40` boot chain with the
[oakMOSS](https://github.com/spruceUI/oakMOSS) rootfs (spruceOS's base system
for this handheld), plorpOS on top.

### Screen

The 480x800 screen is portrait, and so is plorpOS:

- Shelves scroll up and down.
- List layouts put the art above the list; Now Playing puts the cover above the
  track.
- Landscape games sit in the middle at their own shape, black above and below.

### Controls

- The stick = d-pad.
- Touch works on the shelves and in the menus:

| Touch | Does |
|---|---|
| tap | selects what is under it; tap again for A |
| swipe | moves along the shelf or list |
| swipe in from the left or right edge | B |
| swipe down from the top edge | Muse+ (or what **Select Shortcut (Top Swipe)** says), in a game too |
| swipe up from the bottom edge | game switcher (or what **Menu Hold (Bottom Swipe)** says), in a game too |
| hold | Y (favorite) |
| two-finger tap | X (game details) |
| three-finger tap | Menu |

In a DS game, touches go to the lower screen.

### Display

- Games start in **Aspect**: their own shape, as wide as the screen. Change it
  per system (system menu > Defaults) or per game (game menu > Display Mode).
- DS games start in **Even Split**, both screens stacked at full width. Layout
  also has Overlay, Single Screen and Hybrid.

### SSH

**Network Services > SSH** starts a server on port 22 (off on a fresh card).

- Log in as `root` with a key: add your public key to
  `plorpOS/ssh/authorized_keys`, one per line. No password login.
- The host key is made on first start and kept in `plorpOS/ssh/`.
- No sftp, so `scp` fails. Use `ssh root@<zero40> 'cat > /path' < file`.

### USB and DACs

- **System Settings > USB Debugging** keeps the USB port in device mode for adb
  (from the next boot). Off on a fresh card. Without it, a computer's cable can
  flip the port to host mode and drop adb.
- A USB DAC goes in the OTG port (the adb one; the other port keeps charging).
  It needs USB Debugging **off**. Reboot after changing it. With a DAC in, adb
  is gone.
- After sleep the DAC takes about 5 s to come back; sound before then plays on
  the speaker.
- **Known issue:** plugging a DAC in or out with a charger connected can slow
  charging to a trickle, or drain the battery on the charger. Re-plug the
  charger ([guide](guide.md#usb-dac)).

### Ports

The **Ports** shelf (just before Muse+) opens with **PortMaster**. Run it on
Wi-Fi; ports install to `Roms/Ports/`. A port is `Name.sh` plus a `Name/`
folder, so you can also copy them on. Support is unofficial: report problems to
plorpOS, not the PortMaster team. How ports run:
[how-it-works](how-it-works.md#ports).

- Most ports fit the width, black above and below. A few stretch (6 Feet Under,
  Ren'Py games) or draw past the edge (Cave Story).
- Ports that need a game's own files (Downwell, To the Moon, Stardew Valley):
  put them where the port's page says. A port may download a runtime on first
  run, so start it on Wi-Fi.
- 32-bit ports are slow (software graphics; GameMaker ones like MEGA MAN Sunrise
  run about 15 fps). box86 ports (Braid, Shovel Knight) don't work.
- Dust Racing 2D won't start until PortMaster fixes its runtime name.

### Bluetooth

- Headphones and speakers only: Bluetooth controllers and keyboards don't work
  (the kernel lacks support).
- Switching Bluetooth on takes effect at the next boot.
- Re-flashing the card forgets paired devices: pair them again.

Pairing and use: [guide](guide.md#bluetooth).

### Limitations

- Native PICO-8 needs your own `pico8_64` and `pico8.dat` in `Bios/`. Without
  them, PICO-8 carts play on fake08.
- Plex downloads each track before playing it.

# Adding Content

| Folder | What |
|---|---|
| `Roms/<System>/` | games, one folder per system |
| `Bios/` | BIOS files, loose |
| `Music/` | albums, one folder each |
| `Audiobooks/` | books |

### Copy them to the card

Put the card in a computer and copy into the folders above.

### Over Wi-Fi

Join a network under **MENU > Wi-Fi**. Then **MENU > Network Services > Over
The Hare** shows an address and a PIN. Open the address in a browser, enter the
PIN and drag files into their folders. See the [guide](guide.md).

# Updating plorpOS

Settings, saves, play time and achievements (`.userdata/`, `Saves/`) are kept
either way.

### Over Wi-Fi

1. **MENU > Network Services > Check for update.**
2. Confirm the download, then the restart. The update installs during the
   restart. If the new version won't start, the old one comes back.

Details: [guide](guide.md#check-for-update).

### By hand

1. Unzip `plorpOS-v<version>-zero40.zip`.
2. Copy its contents onto the card, replacing what is there.

### Base system

There is no `.omupd` for the Zero 40. The base system (boot chain, kernel, DAC
drivers) updates only by flashing a new image (steps 2-5). Back up the card
first.

# Removing plorpOS

### Steps

1. Delete `plorpOS/` and `.tmp_update/` from the card.
2. Reboot. oakMOSS starts spruceOS from a second card if there is one, or shows
   "No frontend".

`Roms/`, `Saves/` and `Bios/` stay.

# FAQ

### I already run oakMOSS. Can I keep it?

Yes. Unzip `plorpOS-v<version>-zero40.zip` onto the first card's `SPRUCEOS`
partition or onto a second card (SD2, FAT32; oakMOSS can't read exFAT). If SD2
has a launcher (spruceOS, say), it wins and plorpOS on the first card won't
start. The zip is for the Zero 40 only.

### What does the image put on the card?

```
.tmp_update/updater   oakMOSS starts plorpOS through this
plorpOS/              the launcher, its cores and settings
Roms/  Saves/  Bios/  Music/  Audiobooks/
```

Only on a card with no launcher yet. A card with plorpOS, spruceOS or anything
else is left alone.
