# Installing plorpOS on the GKD 350H Ultra

Starting in plorpOS 2.0, GKD 350H Ultra is built on top of BasedOS, while
earlier builds were built over stock ROCKNIX.

BasedOS installs beside the GKD's stock ROCKNIX and leaves it unchanged. You
can start ROCKNIX for one boot, switch BasedOS off, or remove it.

You need:

- the GKD with its stock ROCKNIX
- a computer with SSH on the same Wi-Fi network (or a spare card, see step 1)
- a microSD card for plorpOS, FAT32 or exFAT
- a second card you can erase, for the recovery card (recommended)

Download:

- **BasedOS for plorpOS** from
  [thwonp/BasedOS releases](https://github.com/thwonp/BasedOS/releases). Not
  upstream BasedOS: plorpOS needs this fork's sound, Bluetooth and 32-bit sound.
- **`plorpOS-v<version>-gkd350based.zip`** from
  [plorpOS releases](https://github.com/thwonp/plorpOS/releases/latest).

### 1. Install BasedOS

1. Take the plorpOS card out and turn the GKD on. It starts EmulationStation.
2. START > **Network Settings**: turn Wi-Fi on, join your network and note the
   **IP address** (say `192.168.0.55`).
3. On the computer, run the line from the BasedOS release page, like:

   ```sh
   ssh -t root@192.168.0.55 "curl -fsSL -o /tmp/basedos-setup.sh https://github.com/thwonp/BasedOS/releases/download/v0.0.1-pre.8.plorpos.1/basedos-setup.sh && sh /tmp/basedos-setup.sh"
   ```

   At the password prompt press Enter (stock ROCKNIX's root has none).
4. Choose **Install**. Make the **recovery card** it offers: a backup of the
   GKD's built-in storage with a small system that puts it back.
5. Type `INSTALL` at the summary. If setup stops part-way (the connection
   drops), run it again. Until it finishes, ROCKNIX still starts.

**No computer?** Unzip the release's `basedos-setup-card-<release>.zip` onto a
spare card, put it in, start ROCKNIX and run **BasedOS** from Ports.
Confirmations are A, B, X, Y in that order.

### 2. Prepare the card

Copy everything inside the zip to the root of the plorpOS card, replacing what
is there.

### 3. Boot

Put the card in and turn the GKD on: plorpOS logo, a second of black, the logo
again, then the shelf. The first boot swaps the GameKiddy boot logo for
plorpOS's ([Boot logo](#boot-logo)).

# Device specific notes for the GKD 350H Ultra

### Base system

plorpOS runs on [BasedOS](https://github.com/thwonp/BasedOS): the GKD's own
kernel, drivers and firmware, without ROCKNIX's userland or a compositor.

Hold a button while turning the GKD on (the second logo labels them):

| Hold | Does |
|---|---|
| **Start** | starts ROCKNIX for this boot only |
| **Select** | maintenance mode: Wi-Fi and SSH on, and a menu to start ROCKNIX once, install a BasedOS update from the card, switch BasedOS off, remove it, restart or power off |

- Switching BasedOS off or removing it asks for A, B, X, Y in that order.
- If BasedOS fails to start three times in a row, the GKD starts ROCKNIX. Run
  setup from ROCKNIX and choose **Repair**.

### Controls

- Home + volume = brightness.

### Display

- **Display Mode** adds **Integer Overscale** (the next whole size, edges
  cropped) for the square screen.

### SSH

**Network Services > SSH** (off on a fresh card). No Samba.

- Log in as `root`, password `plorpos`. Change it with `passwd`; plorpOS keeps
  yours.
- Maintenance mode uses the same login.

### Tailscale

Only plorpOS's own traffic uses the tailnet (the kernel has no tunnel driver).

- Muse+ streams, Plex, Audiobookshelf, podcasts and plorpOS's other web
  requests go through it.
- Other tailnet devices can still reach the GKD: SSH and the Syncthing web
  page.
- Anything else on the GKD can't reach tailnet devices: SSH or ping from the
  GKD, Syncthing with a tailnet-only device, ports.

### Nintendo DS

Shared DS controls: [guide](guide.md#nintendo-ds).

- Layouts: **Overlay**, **Offset** (one screen big, one small), **Even Split**,
  **Single Screen**. **Screen Layout** hotkey: MENU + X by default.
- The stick moves a stylus cursor over the bottom screen; R2 touches. The
  cursor hides after 2 s still.

### Native PICO-8

1. Copy `pico8_64` and `pico8.dat` from PICO-8's **Raspberry Pi** download
   into `Bios/`.
2. PICO-8 shelf menu > **Core** > **Pico-8 Native** (fake-08 is the default).
3. **Splore** is the first entry on the PICO-8 shelf. It needs Wi-Fi.

No save states or rewind on native PICO-8.

### Ports

The **Ports** shelf opens with **PortMaster**. Run it on Wi-Fi; ports install
to `Roms/Ports/`. 64- and 32-bit ports both work. Support is unofficial: report
problems to plorpOS, not the PortMaster team. How ports run:
[how-it-works](how-it-works.md#ports).

- Qt6 ports (Dust Racing 2D) draw sideways.
- A 32-bit port that plays sound straight to the hardware (no SDL or OpenAL)
  is silent.

### Boot logo

The GameKiddy logo is kept as `/boot/.backup/logo_mini_plus.bmp.gz.stock`. To
put it back, over SSH:

```sh
rm /mnt/SDCARD/plorpOS/gkdb/bootlogo.bmp.gz /mnt/SDCARD/plorpOS/gkdb/bootlogo-keys.bmp.gz
mount -o remount,rw /boot
cp /boot/.backup/logo_mini_plus.bmp.gz.stock /boot/.backup/logo_mini_plus.bmp.gz
rm /boot/basedos/boot-logo.bmp.gz
mount -o remount,ro /boot
```

A plorpOS update brings the two `gkdb/` files back: repeat the first line
after updating.

# Adding Content

| Folder | What |
|---|---|
| `Roms/<System>/` | games, one folder per system ([Supported systems](https://github.com/thwonp/plorpOS/blob/main/README.md#supported-systems)) |
| `Bios/` | BIOS files, loose |
| `Music/` | albums, one folder each |
| `Audiobooks/` | books |

### Copy them to the card

Put the card in a computer and copy into the folders above.

### Over Wi-Fi

Join a network under **MENU > Wi-Fi**. Then **MENU > Network Services > Over
The Hare** shows an address and a PIN. Open the address in a browser, enter
the PIN and drag files into their folders. See the [guide](guide.md).

# Updating plorpOS

Settings, saves, play time and achievements (`.userdata/`, `Saves/`) are kept
either way.

### Over Wi-Fi

1. **MENU > Network Services > Check for update.**
2. Confirm the download, then the restart. If the new version won't start, the
   old one comes back.

Details: [guide](guide.md#check-for-update).

### By hand

1. Unzip `plorpOS-v<version>-gkd350based.zip`.
2. Copy its contents onto the card, replacing what is there.

### BasedOS

Either:

- Put the release's `.bosupd` on the card's root, hold **Select** at power-on
  and choose **Install update**, or
- run the new release's setup from ROCKNIX and choose **Repair**.

# Removing plorpOS

### Steps

1. Hold **Select** at power-on and choose **Uninstall BasedOS**, or run setup
   from ROCKNIX and choose **Uninstall**.
2. ROCKNIX is as it was. plorpOS's boot logo stays until you put the GameKiddy
   one back ([Boot logo](#boot-logo)).

# FAQ

### I ran plorpOS on ROCKNIX. How do I move to BasedOS?

1. Install BasedOS (step 1).
2. Rename the card's `plorpOS/` (or `TortOS/` on a 1.9.x card) to
   `plorpOS-old/`.
3. Copy the new zip onto the card. Delete `plorpOS-old/` once everything works.

- Settings, saves, play time and achievements (`.userdata/`, `Saves/`) carry
  over.
- Join Wi-Fi and pair Bluetooth headsets again: BasedOS doesn't see ROCKNIX's
  settings.
- The three folders on the GKD's `system.d` share can stay. The first boot
  removes the old build's `launch.sh`, so ROCKNIX (Start at power-on) starts
  EmulationStation.

### It says "Waiting for a card with a frontend"

The card has no `System/launch_frontend.sh`. Copy the zip's contents to its
root.

### plorpOS keeps closing

After five quick failures BasedOS shows a screen saying so, with Wi-Fi and SSH
on. The log is `.userdata/gkd/logs/tortos.log` on the card.

### The GKD starts ROCKNIX

BasedOS failed to start three times, or was switched off. Run setup from
ROCKNIX and choose **Repair**.
