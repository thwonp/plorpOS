# Installing plorpOS on the TrimUI Brick

One zip runs on the **TrimUI Brick**, **Brick Hammer** (same board) and **Brick
Pro**. One card works in all of them, saves and all. You need the Brick, a
microSD card and a computer (just this once).

### 1. Format the card

exFAT, with a Master Boot Record partition scheme. On a Mac: Disk Utility,
*View > Show All Devices*, and erase the card itself, not its volume.

### 2. Copy the zip onto the card

Unzip `plorpOS-v<version>-brick.zip` and copy everything inside it to the root
of the card.

> [!IMPORTANT]
> `.tmp_update/` starts with a dot, so most computers hide it and it gets left
> behind. Without it the first boot powers off and the Brick keeps starting its
> stock system. On a Mac, press <kbd>Cmd</kbd>+<kbd>Shift</kbd>+<kbd>.</kbd> in
> Finder to show hidden files before you copy.

### 3. Boot

Put the card in and turn the Brick on. The first boot installs a small hook on
the Brick; every boot after that comes up in plorpOS.

# Device specific notes for the Brick

### Base system

plorpOS runs from the card on the Brick's stock system. Take the card out and
the Brick starts stock as before.

### Controls

- **Fn1 / Fn2** (Fn1 is the left): nothing until you bind them as hotkeys,
  alone or with a modifier.
- **Mute Switch** (System Settings): `mute`, or `muse button lock`, an
  iPod-style hold switch for Muse+ with the screen dark.
- Brick Pro: left stick = d-pad. Right stick and stick clicks are free for
  hotkeys (game menu > Hotkeys).

### Nintendo DS

- Put `.nds` files in `Roms/Nintendo DS/`.
- **Core:** DSperate-lr (default) or PokeDaisy DS, per shelf (shelf menu >
  **Nintendo DS Defaults > Core**) or per game (X menu > **Game Overrides >
  Core**). PokeDaisy DS adds the Pokemon companion: Party, Dex, Map, Bag, Guide.
- **Layouts:** **Overlay** (one screen fills the display, the other 80% opaque
  over its bottom-right corner), **Single Screen**, **Hybrid** (one screen 3x
  the other, which sits against its right edge). Set per shelf (**Nintendo DS
  Defaults > Layout**) or per game (in-game **Layout** row). **Reset Overrides
  > Display Mode** puts every game back on the shelf's.
- **Swap Screens** hotkey puts the other screen in the big place, remembered
  per game. **Screen Layout** steps through the layouts and keeps the swap.
- No **Display** row: 4x already fills the 1024x768 panel.
- **Stylus, Brick:** no touch screen, so the d-pad can be the stylus. Bind
  **Stylus Mode** (d-pad becomes a cursor over the bottom screen; off at the
  start of every game) and **Stylus Tap** (hold to touch) in Hotkeys. Nothing
  is bound for you. The cursor speeds up while the d-pad is held.
- **Stylus, Brick Pro:** a stick is the stylus and the d-pad stays the game's.
  Right stick by default; the in-game **Stylus Stick** row picks the left. The
  cursor is always on and hides after 2 s still. **R Stick Click** is Stylus
  Tap until you bind another.
- The heaviest 3D scenes skip drawing a frame (never two in a row) instead of
  slowing down.
- No rewind (each state is 6 MB and takes about 20 ms). Save states work. The
  microphone is not verified.

### Audio

- Plain Brick: a 3.5 mm cable wins even with Audio Output on Speaker.
- **USB DAC: use the USB-C port on top.** The bottom port is for charging and
  a computer; a DAC there gets no power.
- Wi-Fi can cause brief dropouts on a DAC, more with a weak signal. Turn Wi-Fi
  off under **MENU > Wi-Fi** while you listen.

### Date & Time

The time zone is New York until you pick one.

### Ports

The **Ports** shelf opens with **PortMaster**. Run it on Wi-Fi; ports install to
`Roms/Ports/`. A port is `Name.sh` plus a `Name/` folder, so you can also copy
them on. Support is unofficial: report problems to plorpOS, not the PortMaster
team. How ports run: [how-it-works](how-it-works.md#ports).

- The first launch unpacks PortMaster and takes longer.
- Press A at plorpOS's support notice, then review PortMaster's terms. If it
  offers to upgrade itself, choose **Cancel**.
- Deleting a port deletes its folder, saves included.
- Support is experimental: a port that runs on another handheld may not run
  here. 32-bit (box86) ports such as Shovel Knight don't run.

### Native PICO-8

PICO-8 is sold by Lexaloffle and not included. fake-08 plays carts without it.

1. From the **Raspberry Pi** download, copy `pico8_64` and `pico8.dat` into
   `Bios/`.
2. PICO-8 shelf menu > **Core** > **Pico-8 Native** (the same row switches
   back).
3. **Splore** is the shelf's first entry while `Bios/pico8_64` is there. It
   needs Wi-Fi.

No save states or rewind.

### Limitations

- No touch screen.
- No rewind on the DS.

# Adding Content

| Folder | What |
|---|---|
| `Roms/<System>/` | games, one folder per system |
| `Bios/` | BIOS files, loose ([which ones](../README.md#supported-systems)) |
| `Music/` | albums, one folder each |
| `Audiobooks/` | books |

### Copy them to the card

Put the card in a computer and copy into the folders above.

### Over Wi-Fi

Join a network under **MENU > Wi-Fi**. Then **MENU > Network Services > Over
The Hare** shows an address and a PIN (new every time). Open the address in a
browser, enter the PIN and drag files into their folders. They are on the shelf
when you leave the screen. See the [guide](guide.md).

# Updating plorpOS

Settings, saves, play time and achievements (`.userdata/`, `Saves/`) are kept
either way.

### Over Wi-Fi

1. **MENU > Network Services > Check for update.**
2. Confirm the download, then the restart. The update installs during the
   restart (a minute or two to unpack). If the new version won't start, the
   old one comes back.

Details: [guide](guide.md#check-for-update).

### By hand

1. Unzip `plorpOS-v<version>-brick.zip`.
2. Copy its contents onto the card, replacing what is there.

# Removing plorpOS

### Steps

Take the card out. The Brick boots its own system again.

Before you reformat the card or give the Brick away:

- **Copy `plorpOS/bootlogo.stock.bmp` and `plorpOS/splash.stock.png`
  somewhere safe.** They are the Brick's original boot pictures, and the card
  holds the only copy.
- **Forget your Wi-Fi networks** (X in the Wi-Fi menu). Their passwords are
  stored on the Brick, not the card.

# FAQ

### I have a 1.9.x card (`TortOS/`). How do I update?

Update by hand. The first start moves anything in `TortOS/` that `plorpOS/`
lacks (your PICO-8 files, say) and deletes the rest.

### What does the zip put on the card?

```
.tmp_update/  trimui/   hand the Brick's boot to plorpOS
plorpOS/                the launcher, its cores and settings
Roms/  Saves/  Bios/  Music/  Audiobooks/
INSTALL.md              this guide
```

`Saves/` has a folder per console, ready for saves from another device.

### What does plorpOS leave on the Brick itself?

A boot hook that starts plorpOS from the card, or stock when plorpOS isn't on
it. None of it is a driver or replaces anything the system needs.

```
/usr/trimui/bin/runtrimui.sh            the hook
/usr/trimui/bin/runtrimui-original.sh   the stock hook, moved aside
/usr/trimui/bin/setbright               brightness before the boot picture
/usr/trimui/bin/tortos-bootbright.sh
/etc/init.d/runtrimui                   patched to call the line above
/etc/init.d/runtrimui.tortos-bak        the version before that patch
/etc/splash.png                         the loading splash
/mnt/boot/bootlogo.bmp                  the boot logo
```

It also sets the boot setting `loglevel` from 8 to 4 (quieter kernel, about 2 s
faster boot). To undo it, run `fw_setenv loglevel 8` on the Brick.
