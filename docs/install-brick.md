# Installing on the TrimUI Brick

plorpOS runs from the microSD card. The first boot puts a small hook on the
Brick that looks for the card; take the card out and the Brick starts its stock
system as before.

It runs on the **TrimUI Brick**, the **Brick Hammer** (the same board) and the
**Brick Pro**, from the same zip. One card works in all of them: set it up once
and move it between a Brick and a Brick Pro, saves and all.

You need the Brick, a microSD card and, just this once, a computer.

Download the Brick zip from
[Releases](https://github.com/thwonp/plorpOS/releases/latest) and unzip it.
It goes on the card as it is, this guide (`INSTALL.md`) included:

```
plorpOS/     tortos.elf  diatom  muse  launch.sh  systems.cfg
             cards/  cores/  res/  LICENSES/  ...
.tmp_update/ and trimui/   hand the Brick's boot to plorpOS
Roms/        NES/  SNES/  Game Boy/ ...   one per console
Music/
Audiobooks/
Bios/
Saves/       one folder per console, ready for saves from another device
```

## 1. Prepare the card

1. **Format it as exFAT,** with a Master Boot Record partition scheme. On a
   Mac, open Disk Utility, choose *View > Show All Devices*, and erase the card
   itself rather than its volume.
2. **Copy everything inside the zip to the root of the card.**
3. **Add games** to their console's folder in `Roms/`, albums to `Music/` and
   books to `Audiobooks/`, now or over Wi-Fi later. BIOS files go loose in
   `Bios/`; which ones each console needs is in
   [Supported systems](https://github.com/thwonp/plorpOS/blob/main/README.md#supported-systems).

> [!IMPORTANT]
> `.tmp_update` starts with a dot, so most computers hide it and it gets left
> behind. Without it the first boot powers off and the Brick keeps starting its
> stock system. On a Mac, press <kbd>Cmd</kbd>+<kbd>Shift</kbd>+<kbd>.</kbd> in
> Finder to show hidden files before you copy.

## 2. Put the card in and turn the Brick on

The first boot installs the hook, and every boot after that comes up in
plorpOS.

## Updating from an earlier version

Copy everything in the new zip onto the card, replacing what's there. Your
settings, saves, play time and achievements live in `.userdata/` and `Saves/`
and stay as they are.

## Adding games over Wi-Fi

1. In **Menu > Network Services**, turn Wi-Fi on and pick your network.
2. Open **Over The Hare**. It shows an address and a PIN.
3. Open that address in a browser on your phone or computer and type the PIN.
4. Drag games into their console's folder, albums into `Music` or books into
   `Audiobooks`. They're on the shelf when you leave the screen.

The PIN is new every time you open Over The Hare.

## Nintendo DS

Put `.nds` files in `Roms/Nintendo DS/`. The shelf's Core row picks
DSperate-lr (the default) or PokeDaisy DS for the game under the cursor (the
Pokemon games' companion: Party, Dex, Map, Bag and Guide from the game).

The Brick has three layouts: **Overlay**,
one screen filling the display and the other, 80% opaque, over its bottom-right
corner; **Single Screen**, one screen fullscreen; and **Hybrid**, one screen at
three times the size of the other, which sits against its right edge. The shelf
menu's **Default Layout** sets the shelf's; the in-game **Layout** row sets the
game's own. The **Swap Screens** hotkey puts the other screen in the big place and is remembered for each game; **Screen Layout** steps through the layouts and keeps the screen you swapped to. The shelf menu's **Reset Display Overrides**
puts every game back on the shelf's defaults.
(The other handhelds' Aspect/Stretch/Integer Display row would change nothing
on the Brick's 1024x768 panel, where 4x is the whole screen, so it is not
shown.)

The Brick has no touch screen, so the D-pad can be the stylus. In
Hotkeys, bind **Stylus Mode** (turns the D-pad into a cursor over the bottom
screen and back; the cursor shows only while it is on and the bottom screen is
up, and it starts off in every game) and **Stylus Tap** (hold to touch where
the cursor is). No keys are bound for you. The cursor starts slowly, so a
menu item can be aimed at, and speeds up while the D-pad is held; the face
buttons and shoulders still reach the game.

On the Brick Pro a stick is the stylus instead and the D-pad stays the game's:
the right stick by default, or the left one, picked with the in-game menu's
**Stylus Stick** row (shown only while a DS game runs). Its cursor is always
on - there is no Stylus Mode - and hides after 2 s without a move or a tap;
**R Stick Click** is Stylus Tap until you bind another.

In the heaviest 3D scenes plorpOS skips drawing a frame rather than let the
game slow down (never two in a row); sound and speed stay full.

Rewind is off for the DS on the Brick: each capture is a 6 MB state that takes
about 20 ms, more than a busy frame can spare. Saving and loading a state
works. The microphone is not verified.

## Ports

The Ports shelf includes PortMaster. Its support on plorpOS is unofficial;
**report problems to plorpOS rather than the PortMaster team.**
Existing ports go in `Roms/Ports/`, with each `.sh` launcher beside its data
folder. The first launch unpacks PortMaster and can take longer than later
starts. Wi-Fi is needed to browse or install ports from the app.

Press A at plorpOS's support notice. PortMaster then presents its own terms
for you to review. If it offers to upgrade itself, choose Cancel. MENU opens Continue / Reset / Quit while a
port runs. Deleting a port removes its data folder, including its saves.

Ports work on the Brick and Brick Pro, but support is still experimental: a
port that runs on another handheld may not run here. 32-bit (box86) ports such
as Shovel Knight don't run on the Brick.

## Native PICO-8

plorpOS can run PICO-8 itself, with Splore, instead of the fake-08 core.
PICO-8 is sold by Lexaloffle and is not included.

1. From your PICO-8 download for **Raspberry Pi**, copy `pico8_64` and
   `pico8.dat` into `Bios/`.
2. On the PICO-8 shelf, open the shelf menu and set **Core** to **Pico-8
   Native**. fake-08 stays the default; the same row switches back.
3. **Splore** is the first entry on the PICO-8 shelf whenever `Bios/pico8_64`
   is there. It needs Wi-Fi.

Native PICO-8 has no save states or rewind.

## Audio output

On the plain Brick, a plugged-in 3.5 mm cable takes priority even when Audio
Output is set to Speaker. A speaker override with the jack inserted has not
worked with the firmware's exposed audio controls.

## Updating from an earlier version

Copy everything in the new zip onto the card, replacing what's there. Your
settings, saves, play time and achievements live in `.userdata/` and `Saves/`
and stay as they are.

Before v1.10 the launcher's folder was `TortOS/`. The first start after
updating moves whatever is still in it that the new `plorpOS/` does not have
(your own PICO-8 files, for one) and deletes the rest.

## Removing plorpOS

Take the card out. The Brick boots its own system again.

Before you reformat the card or give the Brick away:

- **Copy `plorpOS/bootlogo.stock.bmp` and `plorpOS/splash.stock.png` somewhere
  safe.** They're the Brick's original boot pictures, and the card holds the
  only copy.
- **Forget your Wi-Fi networks** with X in Network Services. Their passwords
  are stored on the Brick, not on the card.

<details>
<summary>What plorpOS leaves on the Brick itself</summary>

A boot hook that looks for the card, and hands back to the stock system when
plorpOS isn't on it:

```
/usr/trimui/bin/runtrimui.sh            the hook
/usr/trimui/bin/runtrimui-original.sh   the stock hook, moved aside
/usr/trimui/bin/setbright               brightness before the boot animation,
/usr/trimui/bin/tortos-bootbright.sh    both installed on the first boot
/etc/init.d/runtrimui                   patched to call the line above
/etc/init.d/runtrimui.tortos-bak        the version before that patch
/etc/splash.png                         the loading splash
/mnt/boot/bootlogo.bmp                  the boot logo
```

None of them is a driver, and none replaces anything the system needs to run.

</details>
