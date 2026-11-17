# Installing on the Anbernic RG DS

On the RG DS, plorpOS is a whole microSD card image: flash it, put the card in,
turn the RG DS on. Underneath is a trimmed ROCKNIX (its emulators and
EmulationStation removed). The RG DS's own system on its internal storage is
never touched: take the card out and it starts as it always did.

You need the RG DS, a microSD card of 16 GB or more (everything on it is
erased) and a computer that can write images to a card.

## 1. Flash the card

Download the RG DS image from
[Releases](https://github.com/thwonp/plorpOS/releases/latest) and write it to
the card with [balenaEtcher](https://etcher.balena.io/), Rufus, or `dd`:

```sh
sudo dd if=plorpOS-rgds.img of=/dev/sdX bs=4M conv=fsync status=progress
```

`/dev/sdX` is the card itself, not a partition on it. Check it twice: `dd`
erases whatever it is pointed at.

## 2. First boot

Put the card in the RG DS while it's off, then turn it on. The first boot
prepares the card: it makes a **PLORPOS** partition from all the free space,
copies plorpOS onto it and restarts. That takes about a minute; don't turn it
off. Every boot after that goes straight to the shelf.

## 3. Add games

Turn the RG DS off and put the card in your computer. The **PLORPOS** drive has:

```
Roms/        Pico-8/  Arcade/  NES/  SNES/  Game Boy/ ...   one per console,
             Nintendo DS/ and Ports/ among them
Bios/        BIOS files, loose
Saves/       one folder per console
Music/       albums for Muse
Audiobooks/
plorpOS/     the launcher itself - leave it as it is
```

Games go in their console's folder in `Roms/`. Which BIOS files each console
needs is in
[Supported systems](https://github.com/thwonp/plorpOS/blob/main/README.md#supported-systems).

The card's other two partitions are the system's. Windows may offer to format
them: say no.

You can also copy files without taking the card out: connect the RG DS to
Wi-Fi in **System Settings**, then use the **`games-external`** Samba share
or Over the Hare.

## Screens

plorpOS uses one screen and puts something useful on the other. Choose which
in Menu > **Dual Screen Settings**:

- **Primary Screen**: Top or Bottom - where plorpOS and your games go.
- **Secondary Screen**: on or off.
- **Secondary Function**: **Details** shows the game under the cursor (cover,
  what the scrape knows, play time, achievements; swipe for its three pages).
  **Muse** puts Muse's Now Playing there, also beside your game. Touch it and
  the whole of Muse runs on that screen (see Touch). While Muse is open on
  the main screen, the other one shows the queue: drag to scroll, tap a track
  to play it.

Nintendo DS games always use both screens: the top one on top, the bottom one
below, and the bottom screen takes your finger or stylus. PokeDaisy DS keeps
its own picture on the top screen.

## Touch

Both screens are touchscreens. The one plorpOS is on does what the buttons do:

| Touch | Does |
|---|---|
| tap | selects what is under it; A on the selected item |
| swipe | the d-pad (a longer swipe moves further, a flick coasts) |
| swipe in from the left or right edge | B (back) |
| swipe down from the top edge | Muse |
| swipe up from the bottom edge | Recent games |
| long press | Y on what is under it (favorite a game) |
| two-finger tap | X on what is under it (a game's info) |
| three-finger tap | Menu |

In a game, the game's screen takes the edge swipes (Muse, Recent games) and a
long press opens the in-game menu. In a DS game that is the top screen: the
bottom one is only ever the game's.

With Secondary Function set to Muse, touch works on both screens without
tapping in first. Whichever screen your finger lands on takes over, and the
same swipe carries on there. Muse on the second screen is touch only: the
buttons, a touch on the main screen, or playing your game hand control back
and return it to Now Playing. Swiping back at Muse's top level goes to Now
Playing there.

## Ports

Open PortMaster, the first entry on the Ports shelf, with the RG DS on Wi-Fi.
The ports it installs land in `Roms/Ports/` and on the shelf, with the
runtimes they need. Install ports through PortMaster rather than copying them
onto the card: a copied port can be missing its runtime. It's unofficial
support: report problems to plorpOS, not the PortMaster team.

## USB debugging

**System Settings > USB Debugging** turns on adb over the USB-C port, as a
root shell. It stays on across restarts until you turn it off.

## Updating

Don't install ROCKNIX updates (its **update** network share): they replace
plorpOS's system with stock ROCKNIX. A new plorpOS release is a new image. Copy `Saves/` and the hidden `.userdata/` folder
off the card first (your settings, play time and achievements are in
`.userdata/`), flash the new image, let the first boot finish, then copy them
back.

## If something goes wrong

- **It starts the RG DS's own system.** The card isn't in or wasn't read.
  Reseat it and restart; if that doesn't help, flash it again.
- **The screen stays black.** plorpOS failed to start and is trying again
  every 10 seconds. Its log is `.userdata/rgds/logs/tortos.log` on the card;
  put the card in a computer to read it, or reach it over Wi-Fi if the RG DS
  was connected before.

## Removing plorpOS

Take the card out. To reuse the card for something else, format it.
