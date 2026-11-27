# Installing plorpOS on the MagicX Zero 28

You need the Zero 28, a microSD card and a computer.

### 1. Back up the stock card

It is the only way back to stock.

### 2. Reformat the card

Leftover partitions can break the flash. Start from a freshly formatted card.

### 3. Extract the image

Unpack `plorpOS-v<version>-zero28.img.xz` to `plorpOS-v<version>-zero28.img`
first, with [7-Zip](https://www.7-zip.org/) for example. Some imagers fail on
`.xz` files.

### 4. Flash the image

Write `plorpOS-v<version>-zero28.img` to the card with your imager of choice
([Etcher](https://etcher.balena.io/), [Rufus](https://rufus.ie/), `dd`, etc).

### 5. Boot

The first boot can take a minute. It adds a FAT32 partition, `SPRUCEOS`, in the
card's free space and puts plorpOS on it. That is the drive a computer sees.

# Device specific notes for the Zero 28

### Base system

plorpOS runs on [oakMOSS](https://github.com/spruceUI/oakMOSS), spruceOS's base
system for this handheld. The panel is mounted sideways; oakMOSS turns it, so
the 640x480 screen looks like the RG35XX H's.

### Controls

- Left stick = d-pad.
- Right stick, L3 and R3 are free for hotkeys (game menu > Hotkeys).
- DS games: R3 taps the stylus until you give it a hotkey.

### Display

- Games start in **Aspect**. Change it per system (system menu > Defaults) or
  per game (game menu > Display Mode).
- DS games start in **Overlay**; Layout also has Single Screen and Hybrid.

### SSH

**Network Services > SSH** starts a server on port 22 (off on a fresh card).

- Log in as `root` with a key: add your public key to
  `plorpOS/ssh/authorized_keys`, one per line. No password login.
- The host key is made on first start and kept in `plorpOS/ssh/`.
- No sftp, so `scp` fails. Use `ssh root@<zero28> 'cat > /path' < file`.

### USB and DACs

- **System Settings > USB Debugging** keeps the USB port in device mode for adb
  (from the next boot). Off on a fresh card. Without it, a computer's cable can
  flip the port to host mode and drop adb.
- A USB DAC needs USB Debugging **off**. Reboot after changing it.

### Ports

The **Ports** shelf (just before Muse+) opens with **PortMaster**. Run it on
Wi-Fi; ports install to `Roms/Ports/`. A port is `Name.sh` plus a `Name/` folder,
so you can also copy them on. Support is unofficial: report problems to plorpOS,
not the PortMaster team. How ports run: [how-it-works](how-it-works.md#ports).

- 32-bit ports are slow (software graphics). box86 ports (Braid, Shovel Knight)
  don't work.
- Dust Racing 2D won't start until PortMaster fixes its runtime name.

### Limitations

- No Bluetooth (no antenna), touch screen or rumble.
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
The Hare** shows an address and a PIN. Open the
address in a browser, enter the PIN and drag files into their folders. See the
[guide](guide.md).

# Updating plorpOS

Settings, saves, play time and achievements (`.userdata/`, `Saves/`) are kept
either way.

### Over Wi-Fi

1. **MENU > Network Services > Check for update.**
2. Confirm the download, then the restart. The update installs during the
   restart. If the new version won't start, the old one comes back.

Details: [guide](guide.md#check-for-update).

### By hand

1. Unzip `plorpOS-v<version>-zero28.zip`.
2. Copy its contents onto the card, replacing what is there.

### oakMOSS

The base system (oakMOSS, kernel, DAC drivers) updates separately:

1. Copy `plorpOS-v<version>-zero28.omupd` to the root of `SPRUCEOS`.
2. Reboot. It installs, restarts and deletes itself. It waits while the battery
   is under 30% with no charger.

A new image or `.omupd` never replaces plorpOS on a card that already has it.

# Removing plorpOS

### Steps

1. Delete `plorpOS/` and `.tmp_update/` from the card.
2. Reboot. oakMOSS starts spruceOS from a second card if there is one, or shows
   "No frontend".

`Roms/`, `Saves/` and `Bios/` stay.

# FAQ

### I already run oakMOSS. Can I keep it?

Yes. Either copy the `.omupd` ([oakMOSS](#oakmoss)), or unzip `plorpOS-v<version>-zero28.zip`
onto the first card's `SPRUCEOS` partition or onto a second card (SD2,
FAT32; oakMOSS can't read exFAT). If SD2 has a launcher (spruceOS, say), it
wins and plorpOS on the first card won't start. The zip is for the Zero 28 only.

### What does the image put on the card?

```
.tmp_update/updater   oakMOSS starts plorpOS through this
plorpOS/              the launcher, its cores and settings
Roms/  Saves/  Bios/  Music/  Audiobooks/
```

Only on a card with no launcher yet. A card with plorpOS, spruceOS or anything
else is left alone.

### Charging slowed to a trickle after I used a DAC

The image has the fix. On an older oakMOSS (the zip route), plugging a DAC in or
out with a charger connected can do this. Re-plug the charger
([guide](guide.md#usb-dac)).
