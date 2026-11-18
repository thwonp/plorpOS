# Installing on the GKD 350H Ultra

*v1.9.x installs to `TortOS/`; `plorpOS/` starts with the next release.*

plorpOS runs on top of the GKD's own system, ROCKNIX, and changes nothing in
it. It needs two things: its files on the microSD card, and three small service
files on the GKD's internal storage that tell ROCKNIX to start plorpOS instead
of EmulationStation. Take the card out and the GKD starts EmulationStation as
before.

You need the GKD with its stock ROCKNIX, a microSD card and a computer on the
same Wi-Fi network.

Download the GKD zip from
[Releases](https://github.com/thwonp/plorpOS/releases/latest) and unzip it. It
has two folders, and this guide as `INSTALL.md`:

```
copy_to_sd/   plorpOS and the card's empty folders      -> the card
system.d/     the three service folders                 -> the GKD's storage
```

## 1. Connect the GKD to Wi-Fi

In EmulationStation, press START and open **Network Settings**. Turn Wi-Fi on,
choose your network and enter its password. Note the **IP address** it shows,
for example `192.168.0.55`.

ROCKNIX shares its storage on the network out of the box (Samba), with no
password. Leave the card out for now.

## 2. Put the services on the GKD

Do this before the card goes in.

1. On the computer, connect to the GKD's **`config`** share:
   - Windows: type `\\192.168.0.55\config` in File Explorer's address bar.
   - Mac: Finder, *Go > Connect to Server*, `smb://192.168.0.55/config`,
     and connect as **Guest**.
   - Linux: `smb://192.168.0.55/config` in the file manager.
2. Open the **`system.d`** folder in that share. Create it if it isn't there.
3. Copy the three folders from the build's `system.d/` into it:

```
essway.service.d/plorpos.conf              starts plorpOS in place of EmulationStation
input.service.d/plorpos.conf               keeps ROCKNIX's hotkeys off while plorpOS runs
rocknix-automount.service.d/plorpos.conf   no empty system folders on the card
```

Copy the folders themselves, not just the `.conf` files in them: each file only
works inside its folder.

<details>
<summary>Over SSH instead</summary>

SSH is also on out of the box, as `root`, password `rocknix` unless you've
changed it (EmulationStation's settings show it).

```sh
ssh root@192.168.0.55 mkdir -p /storage/.config/system.d
scp -r system.d/* root@192.168.0.55:/storage/.config/system.d/
```

</details>

## 3. Prepare the card

1. **Format it as exFAT,** with a Master Boot Record partition scheme. On a
   Mac, open Disk Utility, choose *View > Show All Devices*, and erase the card
   itself rather than its volume.
2. **Copy everything inside `copy_to_sd/` to the root of the card.**
3. **Add games** to their console's folder in `Roms/`, albums to `Music/` and
   books to `Audiobooks/`. BIOS files go loose in `Bios/`; which ones each
   console needs is in
   [Supported systems](https://github.com/thwonp/plorpOS/blob/main/README.md#supported-systems).

```
plorpOS/     tortos.elf  diatom  muse  musectl  launch.sh  systems.cfg
             menu.ttf  cacert.pem  LICENSE  NOTICE  ...
             cards/  cores/  res/  shaders/  LICENSES/
TortOS/      launch.sh only, for a GKD set up before v1.10 (see Updating)
Roms/        Pico-8/  Arcade/  NES/  SNES/  Game Boy/ ...   one per console,
             Nintendo DS/ and Ports/ among them
Music/
Audiobooks/
Bios/
Saves/       one folder per console, ready for saves from another device
```

`Roms/Pico-8/` also holds a hidden file, `.disable_splore`. Keep it: without it
ROCKNIX puts an empty `Splore.png` there at every boot, for its own
EmulationStation, and it would show up as a broken cart. It doesn't turn off
Splore in plorpOS.

## 4. Turn it off, insert the card, turn it on

Turn the GKD off from EmulationStation (START, **Quit**, **Shutdown System**).
Put the card in while it's off, then turn it on. It comes up in plorpOS, and
does from now on.

The services from step 2 take effect at the next boot, so a card put into the
running GKD is one ROCKNIX fills with empty system folders. If this happened to you, you can leave the folders, or delete them if they bother you.

## Updating from an earlier version

Copy everything inside the new `copy_to_sd/` onto the card, replacing what's
there. Your settings, saves, play time and achievements live in `.userdata/`
and `Saves/` and stay as they are. Leave the `system.d/` files alone.

Before v1.10 the launcher's folder was `TortOS/`. The first start after
updating moves whatever is still in it that the new `plorpOS/` does not have
(your own PICO-8 files, for one) and deletes the rest, except
`TortOS/launch.sh`: older `system.d/` files start that, and it starts plorpOS.

## Adding games over Wi-Fi

1. In **Menu > Network Services**, turn Wi-Fi on and pick your network.
2. Open **Over The Hare**. It shows an address and a PIN.
3. Open that address in a browser on your phone or computer and type the PIN.
4. Drag games into their console's folder, albums into `Music` or books into
   `Audiobooks`. They're on the shelf when you leave the screen.

The PIN is new every time you open Over The Hare.

With Samba on (Network Services), the card is also the **`games-external`**
share.

## Nintendo DS

Put `.nds` files in `Roms/Nintendo DS/`. The shelf's Core row picks
DSperate-lr (the default) or PokeDaisy DS for the game under the cursor (the
Pokemon games' companion: Party, Dex, Map, Bag and Guide from the game).

The layouts are **Overlay**, **Offset** (one screen big, the other small),
**Even Split** and **Single Screen**: the shelf menu's **Default Layout** sets
the shelf's, the in-game **Layout** row the game's own. The **Screen Layout**
hotkey (MENU + X by default) steps through them; the **Swap Screens** hotkey
puts the other screen in the big place (nothing to swap on Even Split) and is
remembered for each game. The Display row sets Single Screen and the Overlay's
big screen to **Aspect**, **Stretch** or **Integer**, centered. The stick moves
a stylus cursor over the bottom screen and R2 touches; the cursor hides after
2 s still.

## Ports

PortMaster is the first entry on the Ports shelf. Open it on Wi-Fi to install
ports; they land in `Roms/Ports/` and on the shelf. PortMaster itself is kept in
`.userdata/shared/portmaster/`. This is unofficial support: report problems to
plorpOS, not the PortMaster team. Ports open fullscreen; fixed-resolution SDL
games are scaled to fit.

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

## If it starts EmulationStation instead

- **The card isn't in, or isn't readable.** ROCKNIX mounts it at
  `/storage/games-external`; check it's exFAT and that `plorpOS/launch.sh` is on
  it (and `TortOS/launch.sh`, on a GKD set up before v1.10).
- **The services aren't in place.** The `config` share should hold
  `system.d/essway.service.d/plorpos.conf` and the other two, each in its own
  folder.
- **plorpOS failed to start.** After five quick failures in a row it hands the
  GKD to EmulationStation until the next restart, so you're never left with a
  black screen. Its log is `.userdata/gkd/logs/tortos.log` on the card.

## What's on the GKD itself

Only the three `plorpos.conf` files in `/storage/.config/system.d/`. They
replace nothing: each adds to one of ROCKNIX's services. The first two do
nothing when plorpOS isn't on the card; the third keeps ROCKNIX from creating
empty system folders on any card. plorpOS's Wi-Fi, SSH, Samba and Syncthing
switches change ROCKNIX's own settings, the ones EmulationStation shows.

SSH and Syncthing's web page (port 8384) both sign in as `root` with ROCKNIX's
root password, `rocknix` unless you've changed it.

## Removing plorpOS

Take the card out: the GKD starts EmulationStation. To remove it for good,
delete `plorpOS/` and `TortOS/` from the card and the three folders
(`essway.service.d`, `input.service.d`, `rocknix-automount.service.d`) from
the `config` share's `system.d`, and restart.
