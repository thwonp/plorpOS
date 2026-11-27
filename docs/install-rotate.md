# Installing plorpOS on the RG Rotate

You need the Rotate, a microSD card (8 GB or more) and a computer. plorpOS runs
from the card on [base618](https://github.com/thwonp/base618) (Armbian, Debian
trixie). Android (stock or GammaOS) stays on the device: with the card out, it
starts Android as before.

### 1. Flash the SPL (once per unit)

The Rotate only starts a card with base618's SPL in its boot loader slot. Get
`base618-v0.2-spl.zip` from
[base618's v0.2 release](https://github.com/thwonp/base618/releases/tag/v0.2).
Only `splloader` (boot0) is written; `splloader_bak` keeps the unit's own SPL.
Skip this step if the unit already runs base618. Pick one way:

- **From GammaOS** (adb on any PC, USB debugging on):
  ```sh
  adb root
  adb push spl-cm4.img flash-spl-android.sh /data/local/tmp/
  adb shell sh /data/local/tmp/flash-spl-android.sh
  adb pull /sdcard/base618-spl-backup
  ```
  Keep the pulled backup.
- **From stock or GammaOS, Linux over USB:** get `spl-flasher.zip` from
  [beebono's ROCKNIX-T618 release](https://github.com/beebono/distribution/releases/tag/beta-20260818)
  and unzip it next to the `base618-v0.2-spl` folder, then:
  ```sh
  SPD_DUMP=$PWD/spl-flasher/spd_dump-linux ./base618-v0.2-spl/flash-spl.sh spl-flasher
  ```
  Enter download mode: unplug the unit, power it off, slide the screen open,
  hold Home/Back (front, between the sticks) and plug it in. The screen stays
  dark; keep it plugged in for about a minute. USB access needs `sudo` or a udev
  rule (base618's
  [install.md](https://github.com/thwonp/base618/blob/main/docs/install.md#1-the-spl-once-per-unit)).
- **From stock or GammaOS, Windows (untested):** copy `spl-cm4.img` into
  beebono's `spl-flasher` folder and run there:
  ```bat
  spd_dump.exe --wait 300 keep_charge 1 fdl fdl1-dl.bin 0x5500 fdl fdl2-dl.bin 0x9EFFFE00 exec write_part splloader spl-cm4.img poweroff
  ```
  Then enter download mode as above. Don't use the zip's `flash-spl.bat`: it
  writes both slots.

### 2. Extract the image

Download `plorpOS-v<version>-rotate.img.xz` from
[Releases](https://github.com/thwonp/plorpOS/releases) and check it against
`SHA256SUMS`. Unpack it to `plorpOS-v<version>-rotate.img` first, with
[7-Zip](https://www.7-zip.org/) for example. Some imagers fail on `.xz` files.

### 3. Flash the image

Write `plorpOS-v<version>-rotate.img` to the card with your imager of choice
([Etcher](https://etcher.balena.io/), [Rufus](https://rufus.ie/), `dd`, etc).

### 4. Boot

Put the card in and turn the Rotate on. With a USB cable plugged in it stops at
the charging screen: hold Power to go on.

The first boot can take a minute. It copies the unit's own audio and sleep
firmware off its storage (nothing there is written), makes an exFAT partition,
`PLORPOS`, across the rest of the card and puts plorpOS on it. It shows
"plorpOS: setup complete" and powers off, once. Turn it on again.

# Device specific notes for the RG Rotate

### Swivel

Closed (screen over the buttons), the picture turns with it. System Settings:

- **Rotate (Closed)**: `nothing`, `sleep` or `open Muse+`, in a game too.
- **Rotate (Open)**: `wake only`, `main shelf`, `recents` or `resume last game`.
  Opening always wakes a sleeping Rotate.
- **Rotate Open Always Resumes Game** (on to start): with a game running,
  opening goes back to the game; only `recents` acts (the game switcher). Off,
  Rotate (Open) applies as set: `main shelf` ends the game (saved as Quit saves
  it).

No **Auto Off** row on the Rotate.

### Touch

Open or closed, touch does what the buttons do. **MENU > Controls** has a Touch
page.

| Touch | Does |
|---|---|
| tap | selects what is under it; A on the selected item |
| swipe | d-pad (longer moves further, a flick coasts) |
| swipe in from the left or right edge | B |
| swipe down from the top edge | Muse+ (or what **Select Shortcut (Top Swipe)** says), in a game too |
| swipe up from the bottom edge | game switcher (or what **Menu Hold (Bottom Swipe)** says), in a game too |
| long press | Y on what is under it (favorite a game) |
| two-finger tap | X on what is under it (game info) |
| three-finger tap | Menu |
| long press in a game | the in-game menu |

### Buttons

- Menu held + Vol+/Vol- = brightness.
- The **side key** controls Muse+, on the shelf and in a game: tap = play/pause,
  hold = previous track, double tap = next.
- **Sleep Button Lock** also ignores the touch panel. The side key stays live.

### Display

- **Integer Overscale** (next whole size, edges cropped) is offered on top of
  Stretch, Aspect and Integer.
- DS layouts: **Overlay**, **Offset** (one screen big, one small), **Even
  Split**, **Single Screen**. Set them per system (Nintendo DS Defaults >
  Layout) or per game (in-game Layout row). The **Screen Layout** hotkey
  (MENU + X by default) steps through them. **Swap Screens** (remembered per game) puts the
  other screen in the big place. Display sets Single Screen and Overlay's big
  screen to Aspect, Stretch or Integer.
- DS stylus: touch the bottom screen. Or bind **Stylus Mode** (d-pad moves a
  cursor) and **Stylus Tap** (hold to touch) in Hotkeys. The cursor hides after
  2 s still.
- DS core: **Nintendo DS Defaults > Core** picks DSperate-lr (default) or
  PokeDaisy DS; a game's X menu > **Game Overrides > Core** sets it per game.

### SSH

**Network Services > SSH** starts it (off on a fresh card). Log in as `root`,
password `plorpos`.

### Audio

- No 3.5mm jack, so Volume Limiter has no 3.5mm row.
- Bluetooth headsets: see the [guide](guide.md#bluetooth).

### Ports

The **Ports** shelf opens with **PortMaster**. Run it on Wi-Fi; ports install
to `Roms/Ports/`. Support is unofficial: report problems to plorpOS, not the
PortMaster team. How ports run: [how-it-works](how-it-works.md#ports).

### Limitations

- Native PICO-8 needs your own `pico8_64` and `pico8.dat` (Raspberry Pi build)
  in `Bios/`; then set the PICO-8 shelf menu's **Core** to **Pico-8 Native**.
  **Splore** (first entry on the shelf) needs Wi-Fi. No save states or rewind.
  Without the files, PICO-8 carts play on fake-08.

# Adding Content

| Folder | What |
|---|---|
| `Roms/<System>/` | games, one folder per system (see `plorpOS/systems.cfg`) |
| `Bios/` | BIOS files, loose |
| `Saves/` | saves, one folder per system |
| `Music/` | albums, one folder each |
| `Audiobooks/` | books |

### Copy them to the card

Turn the Rotate off, put the card in a computer and copy onto the `PLORPOS`
partition.

### Over Wi-Fi

Join a network under **MENU > Wi-Fi**. Then **MENU > Network Services > Over
The Hare** shows an address and a PIN. Open the address in a browser, enter the
PIN and drag files into their folders. See the [guide](guide.md).

# Updating plorpOS

Settings and saves are kept either way.

### Over Wi-Fi

1. **MENU > Network Services > Check for update.**
2. Confirm the download, then the restart. The update installs during the
   restart. If the new version won't start, the old one comes back.

Details: [guide](guide.md#check-for-update).

### By hand

1. Unzip `plorpOS-v<version>-rotate-update.zip`.
2. Copy its `plorpOS/` folder over the one on `PLORPOS`.

### Base system

A release that changes the system underneath (the kernel, say) comes only as a
new card image: flash it (steps 2-4).

2.0 also updates the device tree so opening the swivel wakes the Rotate from
sleep. The first start after updating a 1.9.x card shows "System updated" and
restarts once by itself.

# Removing plorpOS

### Steps

1. Take the card out. The Rotate starts Android.

The SPL from step 1 stays in `splloader`; without a plorpOS or base618 card the
unit boots Android as before.

# FAQ

### What does the image put on the card?

The base618 system, then at first boot the `PLORPOS` partition:

```
plorpOS/                      the launcher, its cores and settings
Roms/  Saves/  Bios/  Music/  a folder per system in Roms/ and Saves/
```

A wiped `PLORPOS` partition is set up again at the next boot.
