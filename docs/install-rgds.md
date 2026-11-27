# Installing plorpOS on the Anbernic RG DS

You need the RG DS, a microSD card of 16 GB or more and a computer. The RG DS's
own system on its internal storage is never touched: take the card out and it
starts as before.

### 1. Extract the image

Unpack `plorpOS-v<version>-rgds.img.xz` to `plorpOS-v<version>-rgds.img`
first, with [7-Zip](https://www.7-zip.org/) for example. Some imagers fail on
`.xz` files.

### 2. Flash the image

Write `plorpOS-v<version>-rgds.img` to the card with your imager of choice
([Etcher](https://etcher.balena.io/), [Rufus](https://rufus.ie/), `dd`, etc).
Everything on the card is erased.

### 3. Boot

Put the card in while the RG DS is off, then turn it on. The first boot takes
about a minute: it makes a **PLORPOS** partition (exFAT) from the free space,
copies plorpOS onto it and restarts. Don't turn it off. That is the drive a
computer sees.

# Device specific notes for the RG DS

### Base system

plorpOS runs on a trimmed ROCKNIX (no EmulationStation or emulators).

- The card's other two partitions are the system's. If Windows offers to
  format them, say no.
- Don't install ROCKNIX updates (its **update** network share): they replace
  plorpOS's system with stock ROCKNIX.

### Screens

plorpOS uses the main screen and puts something useful on the other.

- **Anbernic + Home** swaps them, on the shelf or in a game. Remembered.
- Second screen on the shelf: **Details** for the game under the cursor
  (cover, scrape info, play time, achievements). Swipe sideways for its three
  pages.
- Second screen in a game: **Quick Settings**, the in-game menu's rows by
  touch (no Hotkeys). The game keeps running: Display, Shader and the rest apply
  live; **Reset** and **Save and Quit** need a second tap.
- **Quick Save** / **Quick Load** use the game's Auto state, like the hotkeys,
  and the game keeps running. Quick Load needs a second tap ("Tap again") and
  is greyed until there is an Auto state. The row reads "Saved" / "Loaded" for
  a moment. Numbered save slots stay in the regular in-game menu.
- DS games use both screens: top on top, bottom below, and the bottom one takes
  your finger or stylus. PokeDaisy DS keeps its own picture on the top screen.

Edge swipes on the second screen, anywhere:

- **Down from the top**: **Muse+** (Now Playing). It stays through games until
  you swipe down again. Tap it to browse Muse+ there. While Muse+ is open on
  the main screen, the second screen shows the queue: drag to scroll, tap a
  track to play it.
- **Up from the bottom**: **Recent games**. Pick one to play it, even from
  inside another game.

### Buttons and brightness

The **Anbernic** button is Menu. It acts on release, so it can be held for
brightness. Each screen has its own:

| Hold + Volume up/down | Screen |
|---|---|
| Anbernic | top |
| Home | bottom |

- The level bar shows on the screen you are changing, in games too.
- The second screen goes down to **off**; touching it then does nothing. The
  main screen never goes fully dark.
- Swapping screens swaps their brightness too.
- A DS game lights a screen that is off, and turns it off again after.

### Lid

Closing the lid for 1 s sleeps, like a POWER tap.

### Touch

Both screens are touchscreens. The one plorpOS is on does what the buttons do:

| Touch | Does |
|---|---|
| tap | selects; A on the selected item |
| swipe | d-pad (longer goes further, a flick coasts) |
| swipe in from the left or right edge | B |
| swipe down from the top edge | Muse+ (or what Select Shortcut says) |
| swipe up from the bottom edge | Recent games (or what Menu Hold says) |
| long press | Y (favorite) |
| two-finger tap | X (game info) |
| three-finger tap | Menu |

- In a game, the game's screen takes the edge swipes and a long press opens
  the in-game menu. In a DS game that is the top screen; the bottom is the
  game's.
- No tap-in needed: whichever screen your finger lands on takes over, and a
  swipe carries on there.
- Muse+, Quick Settings and Recent games on the second screen are touch only.
  Beside a game they stay open while you play: the Anbernic button or a side
  edge swipe (Back) closes them. On the shelf any button or a touch on the main
  screen hands control back. Muse+ stays where you left it and a touch picks
  up there; once the screen turns off it rests on Now Playing. Back at Muse+'s
  top level goes to Now Playing.

### Display

- Header Size and Body Size start at `90%`.

### SSH and Samba

**Network Services** has **SSH** and **Samba** switches. With Samba on, the
card is the **`games-external`** share.

### USB Debugging

**System Settings > USB Debugging** turns on adb over USB-C, as a root shell,
with no authorisation. It stays on across restarts until you turn it off.

### Ports

The **Ports** shelf opens with **PortMaster**. Run it on Wi-Fi; ports install
to `Roms/Ports/` with the runtimes they need. Install through PortMaster, not
by copying: a copied port can be missing its runtime. Support is unofficial:
report problems to plorpOS, not the PortMaster team. How ports run:
[how-it-works](how-it-works.md#ports).

- Select + Start quits a port. In PortMaster's menus, B selects and A goes
  back.
- **Braid** doesn't start (its port needs the Panfrost driver; the RG DS uses
  Mali).
- **Dust Racing 2D** closes at once until PortMaster fixes its Qt6 runtime
  name.
- **Balatro**: with the port's Performance setting off, keep the CRT effect
  at 0, or it draws black patches.

# Adding Content

| Folder | What |
|---|---|
| `Roms/<System>/` | games, one folder per system ([Supported systems](https://github.com/thwonp/plorpOS/blob/main/README.md#supported-systems)) |
| `Bios/` | BIOS files, loose |
| `Music/` | albums, one folder each |
| `Audiobooks/` | books |

Leave `plorpOS/` as it is.

### Copy them to the card

Put the card in a computer and copy into the folders above on the
**PLORPOS** drive.

### Over Wi-Fi

Join a network under **MENU > Wi-Fi**. Then **MENU > Network Services > Over
The Hare** shows an address and a PIN. Open the address in a browser, enter
the PIN and drag files into their folders. See the [guide](guide.md). The
Samba share works too ([SSH and Samba](#ssh-and-samba)).

# Updating plorpOS

Settings, saves, play time and achievements (`.userdata/`, `Saves/`) are kept
either way.

### Over Wi-Fi

1. **MENU > Network Services > Check for update.**
2. Confirm the download, then the restart. If the new version won't start, the
   old one comes back.

Details: [guide](guide.md#check-for-update).

### By hand

1. Unzip `plorpOS-v<version>-rgds.zip`.
2. Copy its contents onto the **PLORPOS** drive, replacing what is there.

### New image

A release that changes the system underneath comes as a new image, not over
Wi-Fi:

1. Copy `Saves/` and the hidden `.userdata/` off the card.
2. Flash the new image and let the first boot finish.
3. Copy them back.

# Removing plorpOS

### Steps

Take the card out. To reuse it, format it.

# FAQ

### It starts the RG DS's own system

The card isn't in or wasn't read. Reseat it and restart; if that doesn't help,
flash it again.

### The screen stays black

plorpOS failed to start and retries every 10 seconds. The log is
`.userdata/rgds/logs/tortos.log` on the card: read it in a computer, or over
Wi-Fi if the RG DS was already connected.
