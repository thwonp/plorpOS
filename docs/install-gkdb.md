# Installing on the GKD 350H Ultra (BasedOS)

plorpOS on the GKD runs on **BasedOS**, a small system that takes the place of
ROCKNIX's userland while using the GKD's own kernel, drivers and firmware. It
starts faster than ROCKNIX and needs no compositor. This replaces the older
plorpOS build that ran on top of ROCKNIX ([install-gkd.md](install-gkd.md)),
which gets no more updates.

ROCKNIX stays on the GKD and isn't changed: BasedOS is installed beside it, and
you can start ROCKNIX for one boot, switch BasedOS off, or remove it.

You need:

- the GKD with its stock ROCKNIX
- a computer with SSH on the same Wi-Fi network (or a spare microSD card for
  setup without a computer, below)
- a microSD card for plorpOS, FAT32 or exFAT
- a second card you can erase for the recovery card (recommended)

Download two things:

- **BasedOS for plorpOS**:
  [thwonp/BasedOS releases](https://github.com/thwonp/BasedOS/releases). Use
  this fork's release, not upstream BasedOS: plorpOS needs its sound,
  Bluetooth and 32-bit sound support.
- **The GKD BasedOS zip** (`gkdb`) from
  [plorpOS releases](https://github.com/thwonp/plorpOS/releases/latest). It is
  the card's root: `System/launch_frontend.sh`, `plorpOS/` and the empty folders,
  plus this guide as `INSTALL.md`.

## 1. Install BasedOS

1. Take the plorpOS card out and turn the GKD on: it starts EmulationStation.
2. Press START, open **Network Settings**, turn Wi-Fi on and join your network.
   Note the **IP address**, for example `192.168.0.55`.
3. On the computer, run (one line; the release page has it for each release):

   ```sh
   ssh -t root@192.168.0.55 "curl -fsSL -o /tmp/basedos-setup.sh https://github.com/thwonp/BasedOS/releases/download/v0.0.1-pre.8.plorpos.1/basedos-setup.sh && sh /tmp/basedos-setup.sh"
   ```

   At the password prompt press Enter: stock ROCKNIX's root has none.
4. Choose **Install**. Setup offers a **recovery card** first: a backup of the
   GKD's built-in storage with a small system that puts it back. Make one.
5. Setup checks everything, shows a summary and asks you to type `INSTALL`.
   Then it downloads, checks and installs. If it stops part-way (the connection
   drops), run it again: until it finishes, ROCKNIX still starts.

**No computer?** Each BasedOS release also has `basedos-setup-card-<release>.zip`.
Unzip it onto a spare card, put it in, start ROCKNIX, and run **BasedOS** from
Ports. Confirmations are A, B, X, Y in that order.

## 2. Prepare the card

1. Copy everything inside the GKD BasedOS zip to the root of the card,
   replacing what's there.
2. Add games to their console's folder in `Roms/`, albums to `Music/`, books to
   `Audiobooks/` and BIOS files to `Bios/`
   ([Supported systems](https://github.com/thwonp/plorpOS/blob/main/README.md#supported-systems)).

**Coming from plorpOS on ROCKNIX:** rename the card's old `plorpOS/` folder
to `plorpOS-old/` before copying, and delete it once everything works. Your
settings, saves, play time and achievements are in `.userdata/` and `Saves/`
and carry over. Wi-Fi has to be joined again and Bluetooth headsets paired
again: BasedOS doesn't see ROCKNIX's settings. The three files on the GKD's
`system.d` share can stay: with no `plorpOS/launch.sh` on the card, ROCKNIX
starts EmulationStation as usual.

## 3. Start it

Put the card in and turn the GKD on. It shows the plorpOS logo, a second or so
of black while the kernel starts, the plorpOS logo again, then the shelf.

The first start replaces the GameKiddy boot logo with plorpOS's and keeps the
original beside it (see Boot logo).

## Boot keys

Hold a button while turning the GKD on. The second plorpOS logo labels them.

| Hold | What happens |
|---|---|
| **Start** | starts ROCKNIX for this boot only; the next start is BasedOS again |
| **Select** | BasedOS's maintenance mode: Wi-Fi and SSH on, and a menu to start ROCKNIX once, install a BasedOS update from the card, switch BasedOS off, remove it, restart or power off |

Switching BasedOS off or removing it asks for A, B, X, Y in that order. If
BasedOS fails to start three times in a row, the GKD starts ROCKNIX by itself;
running setup again from ROCKNIX can **Repair** BasedOS.

## Updating

**plorpOS:** MENU > Network Services > **Check for update**, over Wi-Fi
([guide](guide.md#check-for-update)). By hand: copy the new zip's contents onto
the card, replacing what's there.

**BasedOS:** download the `.bosupd` from the
[BasedOS release](https://github.com/thwonp/BasedOS/releases), put it on the
card's root, hold **Select** at power-on and choose **Install update**. Or run
the new release's setup from ROCKNIX and choose **Repair**.

## Wi-Fi and SSH

Wi-Fi is set up in **MENU > Network Services**. **SSH** is off on a fresh card;
turn it on there. Sign in as **`root`**, password **`plorpos`**. Change it with
`passwd` over SSH; plorpOS keeps yours. Maintenance mode uses the same login.

## Adding games over Wi-Fi

1. In **MENU > Network Services**, turn Wi-Fi on and pick your network.
2. Open **Over The Hare**. It shows an address and a PIN.
3. Open that address in a browser on your phone or computer and type the PIN.
4. Drag games into their console's folder, albums into `Music` or books into
   `Audiobooks`. They're on the shelf when you leave the screen.

## Nintendo DS

Put `.nds` files in `Roms/Nintendo DS/`. The shelf's Core row picks
DSperate-lr (the default) or PokeDaisy DS. The **Screen Layout** hotkey
(MENU + X by default) steps through Overlay, Offset, Even Split and Single
Screen. The stick moves a stylus cursor over the bottom screen and R2 touches.

## Ports

PortMaster is the first entry on the Ports shelf. Open it on Wi-Fi to install
ports; they land in `Roms/Ports/`. 64- and 32-bit ports both work, and small
fixed-size games are scaled to the screen. This is unofficial support: report
problems to plorpOS, not the PortMaster team.

Known limits:

- Qt6 ports (Dust Racing 2D) draw sideways.
- A 32-bit port that plays sound straight to the hardware, without SDL or
  OpenAL, is silent. Most use one of the two.

## Native PICO-8

Copy `pico8_64` and `pico8.dat` from PICO-8's **Raspberry Pi** download into
`Bios/`, then set **Core** to **Pico-8 Native** in the PICO-8 shelf's menu.
**Splore** is the first entry on the PICO-8 shelf; it needs Wi-Fi.

## Boot logo

The GameKiddy logo is kept on the GKD as
`/boot/.backup/logo_mini_plus.bmp.gz.stock`. To put it back, over SSH:

```sh
rm /mnt/SDCARD/plorpOS/gkdb/bootlogo.bmp.gz /mnt/SDCARD/plorpOS/gkdb/bootlogo-keys.bmp.gz
mount -o remount,rw /boot
cp /boot/.backup/logo_mini_plus.bmp.gz.stock /boot/.backup/logo_mini_plus.bmp.gz
rm /boot/basedos/boot-logo.bmp.gz
mount -o remount,ro /boot
```

The first line stops plorpOS from putting its logo back; a plorpOS update brings
those two files back, so repeat it after updating.

## If it doesn't start

- **"Waiting for a card with a frontend":** the card has no
  `System/launch_frontend.sh`. Copy the zip's contents to its root.
- **plorpOS keeps closing:** after five quick failures BasedOS shows a screen
  saying so, with Wi-Fi and SSH on. The log is `.userdata/gkd/logs/tortos.log`
  on the card.
- **The GKD starts ROCKNIX:** BasedOS failed to start three times, or was
  switched off. Run setup from ROCKNIX and choose **Repair**.

## Removing it

Hold **Select** at power-on and choose **Uninstall BasedOS**, or run setup from
ROCKNIX and choose **Uninstall**. ROCKNIX is as it was. plorpOS's logo stays
until you put the GameKiddy one back (above).
