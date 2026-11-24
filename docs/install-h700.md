# Installing on an Anbernic H700 (RG SP, RG34xxSP, RG35XX H, RG35XX SP)

*v1.9.x installs to `TortOS/`; `plorpOS/` starts with the next release.*

plorpOS runs on top of **BaseOS**, a small system for Anbernic's H700
handhelds by Prashant Vaibhav (<https://github.com/pvaibhav/BaseOS>). BaseOS
goes on one microSD card and starts whatever frontend it finds on the other;
plorpOS is that frontend. plorpOS does not include or change BaseOS.

One zip runs on the **RG SP**, **RG34xxSP**, **RG35XX H** and **RG35XX SP**. You need the
device, two microSD cards and a computer.

## 1. BaseOS on the first card (TF1)

Use [BaseOS v1.4.0](https://github.com/pvaibhav/BaseOS/releases/tag/v1.4.0)
for your H700 device (newer 1.5.x builds are pre-releases and can fail to
wake from sleep), and write it to a card as BaseOS's own instructions say. Put it in the slot
marked **TF1** and turn the device on once: it grows its storage, then says
there is no frontend on the card. Turn it off.

## 2. plorpOS on the second card (TF2)

1. **Format the card as FAT32 or exFAT,** with a Master Boot Record partition
   scheme, not GPT. BaseOS only reads the card's first partition, and a GPT card
   from a Mac starts with a hidden EFI partition. On a Mac, open Disk Utility, choose *View > Show All Devices*, and erase the card
   itself rather than its volume.
2. Download the H700 zip from
   [Releases](https://github.com/thwonp/plorpOS/releases/latest) and unzip it.
3. **Copy everything inside the zip to the root of the card,** not into a
   folder. `System/` must be at the top level:

```
System/       the frontend hook BaseOS looks for (launch_frontend.sh)
plorpOS/      plorpOS itself
Roms/ Bios/ Saves/ Music/ Audiobooks/   for your games and files; Saves/ has a folder per console
INSTALL.md    this guide
```

Put your games in `Roms/` (one folder per system, as `plorpOS/systems.cfg`
names them) and BIOS files in `Bios/`.

## 3. Turn it on

Put the card in the slot marked **TF2** and turn the device on. BaseOS starts
plorpOS in a few seconds. Join Wi-Fi from **Menu > Network Services**.

If it shows **ADD FRONTEND TO SD CARD**, BaseOS did not find `System/` on
TF2: check that the card uses Master Boot Record (not GPT) and that `System/`
sits at the root, not inside a folder.

## Updating from an earlier version

From v1.10, **MENU > Network Services > Check for update** does this over
Wi-Fi ([guide](guide.md#check-for-update)). By hand:

Copy everything in the new zip onto the plorpOS card (TF2), replacing what's
there. Your settings, saves, play time and achievements live in `.userdata/`
and `Saves/` and stay as they are.

## Adding games over Wi-Fi

1. In **Menu > Network Services**, turn Wi-Fi on and pick your network.
2. Open **Over The Hare**. It shows an address and a PIN.
3. Open that address in a browser on your phone or computer and type the PIN.
4. Drag games into their console's folder, albums into `Music` or books into
   `Audiobooks`. They're on the shelf when you leave the screen.

The PIN is new every time you open Over The Hare.

Before v1.10 the launcher's folder was `TortOS/`. The first start after
updating moves whatever is still in it that the new `plorpOS/` does not have
(your own PICO-8 files, for one) and deletes the rest.

## Nintendo DS

Put `.nds` files in `Roms/Nintendo DS/`; no BIOS is needed for most games.
The shelf's Core row picks DSperate-lr (the default) or PokeDaisy DS for the
game under the cursor (the Pokemon games' companion: Party, Dex, Map, Bag and
Guide from the game).

There are three layouts: **Overlay**, one screen filling the display at 4:3 and
the other, 80% opaque, over its bottom-right corner; **Single Screen**, one
screen at full height; and **Hybrid**, one screen at twice the DS's size and the
other in the width left over at its right. The shelf menu's **Default Layout**
sets the shelf's; the in-game **Layout** row sets the game's own. The **Swap Screens** hotkey puts the other screen in the big place and is remembered for each game; **Screen Layout** steps through the layouts and keeps the screen you swapped to.
The **Display** row under Layout picks how Single Screen and the
Overlay's big screen fill the display: **Aspect** (4:3, the default),
**Stretch** (the whole panel, on the 720x480 RG SP and RG34xxSP) or
**Integer** (2x, 512x384). The RG35XX H and RG35XX SP have 4:3 panels, so
they offer Aspect and Integer only.

On the RG SP and RG35XX SP the D-pad moves the stylus cursor: in Hotkeys, bind **Stylus
Mode** (turns the cursor on and off; it shows only while the bottom screen is
up) and **Stylus Tap** (hold to touch where the cursor is). On a unit with two
sticks (RG34xxSP, RG35XX H) a stick moves it and the D-pad stays the game's:
the right stick by default, or the left one, picked with the in-game menu's
**Stylus Stick** row. That cursor is always on, hides after 2 s still, and
**R Stick Click** touches until you bind another Stylus Tap.

Most games run at full speed. In the heaviest 3D scenes the frontend asks the
core to leave a frame's drawing out rather than slow down (an approach from
[slotPlus](https://github.com/nelynes/slotPlus)); Golden Sun: Dark Dawn still
runs a few percent slow in its busiest stretches. Rewind is off for the DS
(each state is about 6 MB); saving and loading a state works.

## Ports

The **Ports** shelf is last. Its first entry is **PortMaster**; open it with
the device on Wi-Fi to install ports, which land in `Roms/Ports/` and appear
on the shelf. A port is a script (`Name.sh`) beside a folder of the same name,
so ports can also be copied onto the card. PortMaster itself lives in
`.userdata/shared/portmaster/`. This is unofficial support: report problems
to plorpOS, not the PortMaster team.

What to expect:

- Ports run fullscreen at the panel's size and play through the speaker or
  Bluetooth like a game. **Menu** opens the plorpOS menu over a port (Continue,
  Reset, Quit); Quit ends the port and returns to the shelf.
- PortMaster needs python and bash, which BaseOS lacks, so they ship as one
  read-only image, `plorpOS/h700/portmaster/pmtools.squashfs`, mounted only
  while a port runs.
- Ports that need a game's own files (Downwell, for one, wants your GOG
  installer or Steam `data.win`) have to be given them: put them where the
  port's page says. A port may also download a runtime the first time (Java
  for Mindustry, about 47 MB; LOVE ships with PortMaster), so do that on Wi-Fi.
- 32-bit ports (Shovel Knight) run through box86 and use plorpOS's own 32-bit
  SDL2, in `plorpOS/lib32/`.
- First launches can be slow: Balatro patches the game for about 30 seconds,
  Mindustry takes about 20.
- Some ports are built for a 720 px wide panel and crop the edges on a 640 px
  one (the RG35XX H and RG35XX SP). Apotris does: switch its screen shape from 3:2 to 4:3 in
  its settings.
- The RG35XX H's two sticks and their clicks (L3/R3) reach ports as an
  ordinary gamepad's. The RG SP and RG35XX SP have no sticks, so a port that needs one
  (Mindustry steers its cursor with the left stick) cannot be played there.
- A port that is killed mid-run (power button hold) leaves a mount or two
  behind; the next port clears them.

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

## Notes

- **Saved Wi-Fi networks** live on the plorpOS card, in
  `.userdata/<device>/wpa_supplicant.conf` (`<device>` is
  `rgsp` on the RG SP, `rg34xx` on the RG34xxSP, `rg35xx` on the RG35XX H
  and RG35XX SP), with their passwords.
- **Brightness:** hold Menu and press Volume +/-.
- **The clock** sets itself from the network while Wi-Fi is on. A new device
  without Wi-Fi starts at 1970: set it in System Settings > Date & Time, or
  secure downloads fail (Splore's carts, for one).
- **USB DACs** work on every model. BaseOS on the RG35XX H and RG35XX SP has no
  USB audio driver, so plorpOS loads its own when it starts.
- **The lid** (RG SP, RG34xxSP, RG35XX SP): closing it sleeps, like a tap of POWER, and opening it wakes
  the device from that sleep. Once it has gone on into a deep sleep, only
  POWER wakes it.
- **Logs** are in `.userdata/<device>/logs/` on the plorpOS card.
- **Muse+** plays mp3, aac, m4a/m4b, flac, ogg, opus and wav from `Music/` and
  `Audiobooks/`.
- **BaseOS updates** (`.bosupd` files) go on the root of the plorpOS card;
  BaseOS installs them on the next start. plorpOS leaves unknown files there
  alone.
- **Over USB** (for developers): BaseOS offers `adb`, but only with a
  USB-A to USB-C cable attached before the device is turned on. A USB-C to
  USB-C cable from a USB-C-only computer powers nothing and connects nothing.

## Removing plorpOS

Delete `System/` and `plorpOS/` from the TF2 card. BaseOS then says there is no
frontend, as it did before.
