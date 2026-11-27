# Installing plorpOS on an Anbernic H700 (RG SP, RG34xxSP, RG35XX H, RG35XX SP)

One zip runs on the **RG SP**, **RG34xxSP**, **RG35XX H** and **RG35XX SP**. You
need the device, two microSD cards and a computer.

### 1. BaseOS on the first card (TF1)

Write [BaseOS v1.4.0](https://github.com/pvaibhav/BaseOS/releases/tag/v1.4.0)
for your model to a card, as BaseOS's instructions say. Newer 1.5.x builds are
pre-releases and can fail to wake from sleep.

Put it in the slot marked **TF1** and turn the device on once. It grows its
storage, then says there is no frontend. Turn it off.

### 2. Format the second card (TF2)

FAT32 or exFAT, with a **Master Boot Record** partition scheme, not GPT. BaseOS
reads only the first partition, and a GPT card from a Mac starts with a hidden
EFI one. On a Mac: Disk Utility, *View > Show All Devices*, and erase the card
itself, not its volume.

### 3. Copy the zip onto it

Unzip `plorpOS-v<version>-h700.zip` and copy everything inside it to the root of
the card, not into a folder. `System/` must be at the top level.

### 4. Boot

Put the card in the slot marked **TF2** and turn the device on. BaseOS starts
plorpOS in a few seconds.

# Device specific notes for the H700s

### Base system

plorpOS is the frontend for [BaseOS](https://github.com/pvaibhav/BaseOS), by
Prashant Vaibhav. BaseOS lives on TF1 and starts the frontend it finds on TF2.
plorpOS does not include or change BaseOS.

### Models

| Model | Screen | Sticks | Lid | `<device>` folder |
|---|---|---|---|---|
| RG SP | 720x480 | none | yes | `rgsp` |
| RG34xxSP | 720x480 | two | yes | `rg34xx` |
| RG35XX H | 640x480 | two | no | `rg35xx` |
| RG35XX SP | 640x480 | none | yes | `rg35xx` |

Saved Wi-Fi networks (with passwords) are in
`.userdata/<device>/wpa_supplicant.conf` on TF2, logs in
`.userdata/<device>/logs/`.

### Controls

- **Brightness:** hold Menu and press Volume +/-.
- **Lid:** closing it sleeps, like a tap of POWER; opening wakes from that
  sleep. After it has gone on into deep sleep, only POWER wakes it.
- Two-stick models: left stick = d-pad. Right stick and stick clicks are free
  for hotkeys (game menu > Hotkeys).

### Clock

The clock sets itself from the network while Wi-Fi is on. A new device without
Wi-Fi starts at 1970: set **System Settings > Date & Time**, or secure
downloads (Splore's carts, for one) fail.

### USB DACs

Work on every model. BaseOS on the RG35XX H and RG35XX SP has no USB audio
driver, so plorpOS loads its own at start.

### Nintendo DS

Shared DS controls: [guide](guide.md#nintendo-ds).

- Most games need no BIOS.
- **Layouts:** **Overlay** (one screen fills the display at 4:3, the other 80%
  opaque over its bottom-right corner), **Single Screen** (full height),
  **Hybrid** (one screen at 2x, the other in the width left at its right).
- **Display**: **Aspect** (4:3, default), **Stretch** (RG SP and RG34xxSP
  only) or **Integer** (2x, 512x384).
- **Stylus, no-stick models:** bind **Stylus Mode** (d-pad moves a cursor,
  shown while the bottom screen is up) and **Stylus Tap** (hold to touch) in
  Hotkeys.
- **Stylus, two-stick models:** the right stick (see guide).
- The heaviest 3D scenes skip drawing a frame instead of slowing down (from
  [slotPlus](https://github.com/nelynes/slotPlus)). Golden Sun: Dark Dawn still
  runs a few percent slow in its busiest stretches.
- No rewind (each state is about 6 MB).

### Ports

The **Ports** shelf (last) opens with **PortMaster**. Run it on Wi-Fi; ports
install to `Roms/Ports/`. A port is `Name.sh` plus a `Name/` folder, so you can
also copy them on. Support is unofficial: report problems to plorpOS, not the
PortMaster team. How ports run: [how-it-works](how-it-works.md#ports).

- Some ports need the game's own files (Downwell wants your GOG installer or
  Steam `data.win`). Put them where the port's page says.
- Some download a runtime the first time (Java for Mindustry, about 47 MB).
  Do that on Wi-Fi.
- First launches can be slow: Balatro about 30 s, Mindustry about 20 s.
- 32-bit (box86) ports such as Shovel Knight run.
- Ports built for a 720 px panel crop on the 640 px models. Apotris: switch its
  screen shape from 3:2 to 4:3 in its settings.
- Ports that need a stick (Mindustry) can't be played on the RG SP or RG35XX
  SP. On the RG35XX H the sticks and L3/R3 reach ports as a gamepad's.

### Native PICO-8

PICO-8 is sold by Lexaloffle and not included. fake-08 plays carts without it.

1. From the **Raspberry Pi** download, copy `pico8_64` and `pico8.dat` into
   `Bios/`.
2. PICO-8 shelf menu > **Core** > **Pico-8 Native** (the same row switches
   back).
3. **Splore** is the shelf's first entry while `Bios/pico8_64` is there. It
   needs Wi-Fi.

No save states or rewind.

### adb (developers)

BaseOS offers `adb`, but only with a USB-A to USB-C cable attached before the
device is turned on. A USB-C to USB-C cable from a USB-C-only computer does
nothing.

# Adding Content

| Folder | What |
|---|---|
| `Roms/<System>/` | games, one folder per system (named as in `plorpOS/systems.cfg`) |
| `Bios/` | BIOS files, loose ([which ones](../README.md#supported-systems)) |
| `Music/` | albums, one folder each |
| `Audiobooks/` | books |

### Copy them to the card

Put the TF2 card in a computer and copy into the folders above.

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
   restart. If the new version won't start, the old one comes back.

Details: [guide](guide.md#check-for-update).

### By hand

1. Unzip `plorpOS-v<version>-h700.zip`.
2. Copy its contents onto the TF2 card, replacing what is there.

### BaseOS

BaseOS updates (`.bosupd` files) go on the root of the TF2 card. BaseOS
installs them on the next start. plorpOS leaves them alone.

# Removing plorpOS

### Steps

Delete `System/` and `plorpOS/` from the TF2 card. BaseOS then says there is no
frontend, as before.

# FAQ

### It says ADD FRONTEND TO SD CARD

BaseOS did not find `System/` on TF2. Check that the card is Master Boot Record
(not GPT) and that `System/` is at the root, not inside a folder.

### I have a 1.9.x card (`TortOS/`). How do I update?

Update by hand. The first start moves anything in `TortOS/` that `plorpOS/`
lacks (your PICO-8 files, say) and deletes the rest.

### What does the zip put on the card?

```
System/       the frontend hook BaseOS looks for (launch_frontend.sh)
plorpOS/      the launcher, its cores and settings
Roms/  Saves/  Bios/  Music/  Audiobooks/
INSTALL.md    this guide
```

`Saves/` has a folder per console, ready for saves from another device.
