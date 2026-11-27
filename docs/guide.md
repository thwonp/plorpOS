# plorpOS guide

The [README](../README.md) keeps each topic short. This is the long version:
every control, menu row and setting, and the reasons behind them.

## Installing

Each handheld has its own guide: [TrimUI Brick](install-brick.md),
[GKD 350H Ultra](install-gkdb.md), [RG Rotate](install-rotate.md),
[H700](install-h700.md) (RG SP, RG34xxSP, RG35XX H),
[Miyoo Mini Flip](install-flip.md) and [RG Nano](install-nano.md). The BIOS files each
console needs are in the README's
[Supported systems](../README.md#supported-systems). Building it yourself is in
[building.md](building.md).

Copying from macOS leaves `._name` files beside everything. plorpOS skips
them, and `dot_clean /Volumes/YOURCARD` removes them.

## Controls and menus

| | |
|---|---|
| **Left/Right** | move along the row (systems, and games on Coverflow) |
| **Up/Down** | move down the list on the Detailed List and Art List layouts - **down advances** |
| **the other axis** | jump to the previous / next initial (games) |
| **L1/R1** | jump a screenful (games) |
| **A** | open a system, or start a game |
| **B** | back to the systems row |
| **X** | game info for the card under the cursor |
| **Y** | favorite it - Favorites is a shelf of its own. In Muse+, Y favorites the album under the cursor (or takes it off), and Muse+'s Favorites tab shows only those |
| **Volume rocker** | volume, everywhere, including in game. Held, it keeps stepping: 300 ms, then about eleven steps a second |
| **MENU + volume** | brightness, everywhere, including in game, held the same way (Home with the volume rocker on the GKD) |
| **Fn1 / Fn2** (Brick, Brick Pro) | the two function keys, Fn1 the left: nothing until bound as hotkeys, alone or with a modifier |
| **Left stick** (Brick Pro, RG34xxSP, RG35XX H) | the d-pad, in menus and in game |
| **Right stick, stick clicks** (Brick Pro, RG34xxSP, RG35XX H) | never seen by games (the bundled cores are all digital, except DSperate's opt-in analog patches, which read the sticks), except as the Nintendo DS stylus when **Stylus Stick** picks it, but hotkeys: the left click can be a modifier, and the right stick's directions and click are triggers with a modifier or alone (no stick direction on the Nintendo DS, where R Stick Click is Stylus Tap by default). The left stick's triggers read "L Stick" |
| **MENU** (on the systems row) | the plorpOS menu - settings that are about the firmware |
| **MENU** (inside a system) | that system's menu, below |
| **MENU** (in game) | the in-game menu: Continue, Save, Load, Display, Shader (Scanline Filter on the Miyoo Mini Flip), Palette (Game Boy), Disc (multi-disc games), Achievements, Hotkeys, Reset, Quit. Native PICO-8 has Continue, Display, Reset, Splore, Quit (PortMaster ports: Continue, Reset, Quit); its Display is the same per-system Display Mode (Stretch, Aspect, Integer), saved at once but read only when PICO-8 starts, so a changed value shows "Reset game to apply" until the game is reset. On the DS, Display is **Layout**, with a **Display** row under it; a Nintendo DS game on a unit with two sticks adds **Stylus Stick** (Right Stick by default / Left Stick, which stick moves the stylus cursor) below Hotkeys |
| **MENU, held** (on a shelf or in game) | the game switcher: your recent games as cards, left/right to pick, **A** to play, **X** to remove one, **Y** to favorite it. Hold it about 0.7 s; a tap is still the menu above, on release. Another game picked from inside a game ends the first (it is saved). The RG Rotate also opens it with a swipe up from the bottom edge |
| **MENU** (inside any menu) | closes the whole menu, however deep in it you are. B goes back one screen |
| **SELECT** | Muse+, the music and audiobook player, from anywhere but a running game - the in-game menu included. Again to close it |
| **POWER** | a tap sleeps: the screen goes dark at once, and the device suspends 10 seconds later (the Miyoo Mini Flip has no suspend: it powers off after **Sleep Poweroff**); a tap wakes it. A hold powers off. In a game, the game is saved first either way |

MENU means three different menus depending on where you are, and each one is
about the thing you are looking at: the firmware on the systems row, one
console inside it, the running game in a game. Inside any of them MENU closes
the whole menu at once - from Play Time or Wi-Fi straight back to where you
were, or back into the game - and B goes back one screen at a time.

**plorpOS carries this table itself**, under **MENU > Controls**: five pages
that left and right step through - moving, the shelf, a game, Muse+, and the
buttons that work anywhere (the RG Rotate adds a sixth, Touch). It is the copy that is always to hand, and the one
that follows your Gamelist Layout, so the page shows the axes as they are on
your device. This guide is the long version of the
same thing.

The plorpOS menu opens from the systems row and is about the device rather
than any one console:

| | |
|---|---|
| **Play Time** | per game or per system, by day, week, month, year or all time |
| **Wi-Fi** | the network's name when connected, or why it is not. Inside: the switch and the networks the radio hears - A joins one, Y rescans, X forgets a saved one. The footer names the network and the device's address while connected |
| **Bluetooth** | the connected headset, or `not connected` - pair, connect and forget. Not on the Miyoo Mini Flip |
| **Network Services** | what the network is for: **SSH** and **Samba** switches on the RG DS (and the GKD's older ROCKNIX build), **SSH** on the GKD, RG Rotate, Zero 40, Zero 28 and Miyoo Mini Flip; **Syncthing** and **Tailscale** (see below) on every device with Wi-Fi but the Flip; **Over The Hare**, the file server, which needs Wi-Fi and says so when there is none; **Check for update** (see below) |
| **System Settings** | **Date & Time**: the clock and the time zone - see below. **Audio Output**: `Auto` or `Speaker`, and where Auto landed (`speaker`, `wired`, `bluetooth`, `USB DAC`) - see **Audio** below. **Auto Off** (not on the RG Rotate or Miyoo Mini Flip): how long without a button before the device powers itself down instead of sleeping; setting it turns Auto Sleep to `never`, and the other way round. **Auto Sleep**: how long without a button before the device sleeps, the same as a tap of POWER - not during play, only on the shelf and in the menus, the in-game menu included. **Battery Percentage**: the charge in a gray disc in the top-right corner, ringed green while charging and red when low; off, only a red dot when low. **Mute Switch** (Brick): what the side switch does, `mute` or `muse button lock`, an iPod-style hold switch for music with the screen dark (see Muse+). **Sleep Poweroff** (Miyoo Mini Flip, in place of Auto Off): how long a sleeping Flip waits before it powers off, 30 s to 10 min, 2 min to start. **Lid Close** (Miyoo Mini Flip): `sleep` or `nothing`. **Rotate (Closed)** (RG Rotate): what turning the screen closed does - `nothing`, `sleep`, or `open Muse+` (Now Playing, or the Muse+ tabs when nothing is playing), in a game too. **Rotate (Open)** (RG Rotate): what turning it open does - `wake only`, `main shelf`, `recents` or `resume last game`. Opening always wakes a sleeping Rotate. **Rotate Open Always Resumes Game** (RG Rotate): `on` to start, so with a game running, opening goes back to the game from its menu or from Muse+, and only `recents` acts on the game itself (the game switcher). `off`, Rotate (Open) is done as written with a game running too: `main shelf` ends the game (saved as Quit saves it; a port or PICO-8 just closes) and `recents` opens the switcher |
| **UI Settings** | **UI Theme**: `Plain Jane` or `Fancy Pants`, which art the shelves wear. **Gamelist Layout**: `Coverflow`, `Detailed List` or `Art List`, how a game shelf is drawn. **System Order**: `Year`, `A-Z` or `Maker` - see below. **Header Font** and **Body Font**: `Fredoka`, `Josefin Sans`, `Quicksand`, `Lato` or `M PLUS 1p`, for the headings and for everything else. Pick M PLUS 1p for Japanese song tags and game names: it is the only one with kana and kanji. **Ports Tile**: `Visible` or `Hidden`, whether the Ports card is on the systems row (only listed on devices that have one; hiding it does not delete anything, and Ports games in Favorites and the switcher wait for it to come back) |
| **Game Settings** | for every console. **Games Auto-Resume**: `on`, a launch loads the game's autosave; `off`, it starts fresh and the autosave is still written. **Rewind Speed**: `1x` to `10x`, or `Off` (the default). **Fast-Forward Speed**: `2x`, `4x` or `8x`, what both Fast Forward hotkeys (Hold and Toggle) run at. **Confirm Reset/Quit**: the in-game menu asks before either. **Persistent Turbo Assignments**: each game keeps its Turbo Assign buttons - see below. **Rumble Enabled**: `on` (the default), a game that rumbles shakes the device's motor; greyed on a device without one. **Achievements Enabled**: `off`, a game starts without its set; off by default on the Miyoo Flip, where watching a set can cost a heavy game its full speed (turning it on there says so). **Achievements Sign-In**: the RetroAchievements account, or `sign in`. **Download Achievement Sets**: every game's set onto the card at once, for playing offline; games already cached are skipped, and the panel counts the games with no achievements to earn. **Scraping**: Box Art, ScreenScraper and Import gamelist.xml metadata, below |
| **Game Settings > Scraping** | **Box Art**: fetch what the whole library is missing. **ScreenScraper**: the account that brings covers with each game's year, genre and synopsis, `sign in`, or `not in this build`. **Import gamelist.xml metadata**: see below |
| **Controls** | every button and what it does, a page per place |
| **About** | version, address, battery, uptime |

Charging, or plugged into a computer, the device never sleeps, suspends or
turns itself off: Auto Off, Auto Sleep and suspend all wait until it is
unplugged. As in NextUI - suspending on external power hangs the Brick.

Date & Time has a row for each part of the clock and one for the time zone,
each changed with left and right and set the moment it changes, so the time
holds through a power-off. The zones are one or two cities per hour of offset,
west to east. On the Brick the zone is New York until you choose one; elsewhere it
reads `System`, the device's own, until you do.

Over The Hare and Box Art need a network, and go quiet without one rather than
disappearing - a row that vanishes teaches nobody why. Achievements Sign-In,
in Game Settings, stays reachable either way, because signing in is the thing
you go there to do, and Bluetooth and Play Time need no network at all.

**Import gamelist.xml metadata** reads the `gamelist.xml` (or
`miyoogamelist.xml`) an EmulationStation-style scraper left in each
`Roms/<system>` folder, and gives those games their year, genre and synopsis -
no network, no account. **Fill in missing** leaves a game that already has
metadata alone; **Replace all** writes over it. Titles still come from the file
name, not the gamelist's `<name>`.

On the Play Time screen, **left and right** change the window - all time, this
year, this month, this week, today - and **Y** switches between one row per
game and one per system. The row under the cursor gets its own line at the
bottom: how many times it has been launched, its longest single session, when
it was last played, and how many of those sessions ended in a flat battery or
a crash rather than a quit. That last number only appears when it is not zero.

On the Wi-Fi screen, **Y** rescans and **X** forgets the network under the
cursor, behind a confirm. Forgetting the one you are connected through is
allowed - refusing would leave a row that is visibly saved and visibly
un-forgettable, which is worse to explain than the consequence.

The system menu opens on a shelf of games and applies to that system alone.
It holds system options only; a game's own settings are in its X menu.

| | |
|---|---|
| **Sort By** | left/right; `Name`, `Play Time`, `Last Played` or `Recently Added` |
| **<Shelf> Defaults** | what every game on the shelf plays with until it has its own (below) |
| **Reset Overrides** | every shelf but Ports: one row per kind of per-game setting, with how many games on this shelf have one. A asks Yes/No, then clears that kind for every game here. The defaults stay |
| **Scrape Shelf** | fetch the art this system is missing, and nothing else (needs Wi-Fi) |
| **Rescan Folder** | read the card again, for ROMs that arrived since boot |

**<Shelf> Defaults** (named for the shelf, e.g. "Super NES Defaults"):

| | |
|---|---|
| **Core** | which core runs the shelf. A choice on Game Boy, Game Boy Color, Game Boy Advance and Nintendo DS (and the Miyoo Mini Flip's SNES and Neo Geo Pockets); on PICO-8, fake-08 or native. Elsewhere it only names the core |
| **Display Mode** | `Stretch`, `Aspect`, `Integer`, and `Integer Overscale` (the next whole size, edges cropped) on the square GKD and RG Rotate screens. On the DS it is **Layout**, with **Display** (Aspect, Stretch or Integer for the big screen) under it where the panel offers more than one |
| **Shader** | the shelf's shader (**Scanline Filter** on the Miyoo Mini Flip): left/right steps through the list, A opens it; greyed on PICO-8 running native |
| **Hotkeys** | the shelf's hotkeys and Button Mapping, the same screen as in a game |
| **Palette** | Game Boy and Game Boy Color: the palette a game starts with |
| **Turbo Assignments** | the shelf's turbo buttons (below) |

Left/right (and A) change a value, saved at once. A game that has its own
keeps it whatever the default does later; Reset Overrides or the game's
Game Overrides puts it back on the default.

On Favorites the menu is only Sort By. The rest is about one
console, and a game there keeps its own settings, or its console's defaults,
wherever it was started from.

**X** on a game opens its info screen:

| | |
|---|---|
| **Synopsis** | the scraped description; A opens it. Synopsis, Year and Genre appear only once the game has been scraped |
| **Year**, **Genre** | from the scrape |
| **Achievements** | earned / total; A opens the list |
| **Get Box Art** / **Replace Box Art** | fetch a cover for this game alone (needs Wi-Fi); a cover already there stays unless a new one is found |
| **Rename** | the name shown on the shelf, typed on the keyboard; the file keeps its name, and a rescrape never undoes it. Empty gives back the old name |
| **Game Overrides** | this game's own settings (games played in diatom; not Ports or native PICO-8) |
| **Delete Game** | asked first; removes the file from the card (saves stay). On Ports it runs PortMaster's uninstall, saves included |

**Game Overrides** lists Core, Display Mode (Layout and Display on the DS),
Shader, Hotkeys, Palette (Game Boy games without their own colours) and Turbo
Assignments. Each reads **Shelf Default** until the game has its own. Left,
right and A cycle through Shelf Default and the real values; choosing Shelf
Default drops the game's own. Hotkeys and Turbo Assignments open their
screens for this game, with a **Use Shelf Default** row; they read `custom`
once the game has its own. From Favorites or Recents it works on the game's
own console.

Changes made in a game (the in-game menu's Display, Layout, Shader and
Palette, the Hotkeys screen and Button Mapping, Turbo Assign, a DS game's
swapped screens) are saved as that game's own while **Save Per-Game
Overrides** is on in Game Settings, the default. Off, they last until the game
quits. Saved ones always apply. The in-game screens never change the shelf's
defaults: those are set from <Shelf> Defaults.

Sort By is per-system for the same reason. Alphabetical is the only sane way
to find a title you can name, and no help at all when you are coming back to
the two or three you are actually playing, which on a large shelf are
scattered through the alphabet. Play Time and Last Played read the same
session rows the Play Time screen does; Recently Added is the ROM's timestamp
on the card. Whichever order you pick, games it knows nothing about sort to
the bottom rather than into the middle, and two games it cannot tell apart
fall back to name - a shelf that reshuffles itself when nothing has changed
reads as broken even when the top of it is right.

Rescan Folder is what makes a ROM that arrived after boot appear without a
restart. Over The Hare already does it for you on the way out of the transfer
screen; this is the same thing by hand, for a card written some other way.

**Turbo is a hotkey, Turbo Assign,** bound under Hotkeys like the others. In a
game, press it, then the button to turbo, then the button to put it on:
Turbo Assign + A + X makes X a turbo A, pressing A about ten times a second
for as long as you hold it; Turbo Assign + A + A makes A its own turbo. The
same again turns it back. Hold
Turbo Assign two seconds to clear every turbo button. With **Save Per-Game
Overrides** on, the game keeps its turbo buttons for next time; off, they last
until it quits. **Turbo Assignments** (in <Shelf> Defaults, or a game's Game
Overrides) sets the same thing as a list: each button, None or the turbo it
fires. A game with none of its own uses the shelf's. How it works is in
[turbo.md](turbo.md).

**Button Mapping** (Hotkeys > Button Mapping, in a game or from <Shelf>
Defaults) moves the game's buttons: for this game from in-game or Game
Overrides, for the whole console from <Shelf> Defaults. Each row is a button as that console's pad
names it - A, B, C, X, Y, Z on the Genesis, A to D on the Neo Geo, I and II on the
TurboGrafx-16 -
and holds the button that sends it: press A on a row, then the button. A
button moved to a row leaves its old one, and a button on no row sends the
game nothing. X clears a row; Reset to Default puts them back. Two-button
consoles have two rows. Directions, START and SELECT never move, and the
menus and hotkeys always read the real buttons.

The other hotkeys are in the README's [FAQ](../README.md#faq). Nintendo DS
layouts, the stylus and Ports differ by device; each install guide has them.

Volume and brightness draw the same thin line across the top of the screen in
the launcher, in a game, and in the in-game menu. One firmware, one piece of
feedback - tinted by which of the two it is, warm for brightness and cyan for
volume, so the line says what it is without a glyph or a number on it.

### Tailscale

*Coming in the next release.* On every device with Wi-Fi except the Miyoo
Mini Flip, for now.

**MENU > Network Services > Tailscale** puts the device on your tailnet. The
first time, A shows a QR code: scan it with a phone, or open the link under
it, and sign in. The screen closes by itself once the device is on, and the
row shows its tailnet address, the one to type into SSH on another machine.
It signs back in by itself after that, a reboot included.

It joins as `plorpos-` and the device: `plorpos-brick`, `plorpos-brickpro`,
`plorpos-h700` (every H700 model), `plorpos-rotate`, `plorpos-rgds`,
`plorpos-gkd`, `plorpos-zero40` or `plorpos-zero28`. A second device with the same name gets a
number after it; rename any of them in Tailscale's admin console. It can reach
MagicDNS names and uses routes from your subnet routers. A on the row turns it
off.

**On the GKD, only plorpOS's own traffic uses the tailnet.** Its kernel has no
driver for a network tunnel, so Tailscale runs without one:

- Audiobookshelf, Plex, podcasts, Muse+ streams and plorpOS's other web
  requests go through Tailscale while it is connected.
- From another device on your tailnet you can still reach the GKD: SSH,
  Samba and the Syncthing web page.
- Anything else started on the GKD can't reach tailnet devices: SSH or ping
  from the GKD, Syncthing syncing with a tailnet-only device, ports.

### Syncthing

*Coming in the next release.* On every device with Wi-Fi except the Miyoo
Mini Flip, for now.

**MENU > Network Services > Syncthing** keeps your **Saves** folder in sync
with your other devices. On, the row shows the address of its web page, like
`192.168.0.55:8384`. Open it in a browser on the same network and sign in as
`plorpos` / `plorpos`. Add your other devices there and share Saves with them.

- It shares Saves and nothing else.
- It starts again after a reboot until you switch it off.
- Its Syncthing ID lives on the card, so reinstalling plorpOS keeps it.
- Anyone on the same network can sign in with that password too. Switch it
  off on networks you don't trust.

### Check for update

*Coming in the next release.* On every device with Wi-Fi.

**MENU > Network Services > Check for update** looks for a newer plorpOS on
GitHub. If there is one, it asks before downloading it, then asks to restart.
The update goes in during that restart. Pick **Cancel** at that point and it
goes in at the next restart instead.

- It needs room on the card for the download and what it unpacks to, about
  four times the download. It says so if there isn't.
- **B** stops a download. Unpacking can't be stopped; it takes a minute or two
  on the Bricks.
- Your settings, saves, games and anything you added to `plorpOS/` stay.
- If the new version won't start, the device goes back to the one it had and
  leaves a note in `plorpOS/update-failed.log`.
- Only full releases are offered, not pre-releases. A release that needs a
  new card image (the RG Rotate's or RG DS's system, say) isn't offered;
  flash that one.

## Audio

Sound can come out of four places, and plorpOS picks in a fixed order:

**a USB DAC, then wired headphones, then Bluetooth, then the speaker.**

A cable wins outright over a headset. Someone who physically plugged
something in has said what they want more plainly than any menu can, and a
headset that merely happens to be connected has not said anything at all.
A USB DAC is a cable too, and the more deliberate one, so it comes first.

The **Audio Output** row (in System Settings) has two positions rather than three. `Auto` follows
the rule above; `Speaker` refuses a DAC and Bluetooth - the jack still works
through it, because the jack is switched in hardware. There is no third "Headset" position because it would do
nothing `Auto` does not already do: `Auto` takes a headset whenever one is
connected, and neither setting can route to one that is not there. The row
shows where the sound actually went - `auto (wired)` - because `Auto` on its
own names a rule, not a place you can hear.

The wired jack has its own volume range, not the speaker's. The two are about
9 dB apart, and the level is re-mapped the moment a cable goes in or out, so
plugging in mid-game does not arrive at nine decibels louder than you left it.

### USB DAC

Any USB audio device the kernel recognises works: a USB-C dongle DAC, a
USB headset, a desktop DAC. Plug it in and game sound, Muse+ and native
PICO-8 move to it within about a second; the row reads `auto (USB DAC)`.
The volume buttons set the DAC's own volume. Pull it out and sound falls
back to the jack or the speaker - Muse+ pauses first, so an album does not
carry on out loud.

**On the Brick and Brick Pro, use the USB-C port on top.** The bottom port
is for charging and a computer, and a DAC plugged in there gets no power.
Elsewhere, use the device's USB-C port.

**On the MagicX Zero 40 and Zero 28, the DAC goes in the OTG port** (the one
adb uses; the other port is for charging, and it keeps charging while the DAC
plays). Turn USB Debugging off first (System Settings, then restart): with it on
the port stays in computer mode and gives a DAC no power. With a DAC in the OTG
port, adb is gone. After waking from sleep the DAC takes about five seconds to
come back; sound played before then comes out of the speaker.

A DAC with no volume control of its own plays at full volume; the device's
buttons can't turn it down.

**Known issue, Zero 40 and Zero 28: charging can slow to a trickle after a
DAC.** Plug a DAC in or pull it out while a charger is connected, and the
battery may stop charging properly - at worst it drains while the charger is
in. Unplugging and re-plugging the charger should bring it back. The fix is a
kernel change in oakMOSS, and it arrives with an oakMOSS update.

**On the Brick and Brick Pro, Wi-Fi can cause brief dropouts on a DAC** -
a few milliseconds of silence, more often the weaker the signal. It is the
radio itself, not the music player, and nothing in software has cured it.
If you hear them, turn Wi-Fi off under **MENU > Wi-Fi** while you
listen.

### Bluetooth

A paired headset reconnects by itself at boot and mid-session, and game audio
follows it without relaunching anything. The bond survives a reboot.

**Pairing is on the Bluetooth screen.** Put the headset in pairing mode, press
**Y** to search, **A** to pair and connect, **X** to forget. The toggle at the
top turns the radio on and off. Connecting a headset there disconnects any
other, because choosing one is the point; a headset that reconnects by itself
never pushes another off.

Only devices that advertise a name are listed. A scan in an ordinary room finds
a dozen BLE beacons and somebody's television, and BlueZ names everything it
cannot identify after its own address - so the list would otherwise be
unusable. A device you have already paired is always shown, named or not.

**Forget means forget.** The bond, the ALSA PCM and BlueZ's cached copy of the
device all go. Scanning leaves a cache entry for everything in range, so
leaving the screen sweeps the ones you never paired with.

The one thing that matters underneath is the agent: headsets pair "Just Works"
and need `NoInputNoOutput`. With the default agent every attempt fails with an
authentication error that looks like a broken key, a broken chip, or broken
headphones, and is none of them.

**A headset paired now carries sound straight away**, a running game's
included.

**The volume keys set the headset's own volume**, through its absolute volume
control, so there is still one volume and not two in series. The headset's
volume buttons move the device's level the other way. On a few headsets, the
device's buttons set only the starting volume; after that, change it on the
headset itself. In a game, a press on the headset changes the level without
showing the bar.

**The headset's play, pause and skip buttons control Muse+**, wherever you are,
a game included.

One thing behaves differently on a Bluetooth sink and is not a bug:

- **there is roughly 100-150 ms of latency**, from SBC, the radio and the
  headset's own buffer. That is what Bluetooth audio costs on any device and
  nothing here can tune it away.

If a headset is switched off or walks out of range mid-game, sound falls back
to the speaker within a second or two and the game keeps running. It never ends
a game to report an audio problem. That fallback is the emulator's own - it
does not wait for the launcher to notice the headset is gone.

## Muse+

Muse+ plays the music and the audiobooks on the card. Its card at the end of
the shelf opens Muse+ on its tabs (below), on the album that is playing if one
is. SELECT opens it too: Now Playing when something is playing or paused, and
otherwise the tabs. A on an album opens its tracks. MENU, anywhere in
Muse+, has the album count, Source, Sort By, Playback Settings (not on the
Miyoo Mini Flip), UI Settings and the Source's own settings. **UI Settings**
holds Tab Settings, Now-Playing Layout and Visible Sources. **Local Music
Settings** holds Album Art (below) and Rescan Folder, for music copied on
while the device was running; **Local Books Settings** holds Rescan Folder.

**Visible Sources** (in UI Settings) switches Sources you don't use out of
the Source list. The one in use stays on; when it is the only one left, the
Source row is grayed out.

**Source** picks what the shelf holds: Local Music, Local Audiobooks,
[Plex Music](#plex), [Jellyfin Music](#jellyfin),
[Audiobookshelf](#audiobookshelf) or [Podcasts](#podcasts). (Before the next release this was two rows, Show and
Source.) Muse+ opens on whichever you left
it on, except that SELECT with a book playing lands on the books, on that
book.

**Sort By** puts the shelf in order by artist, the way the folders are, by
album title, two of the same title going in their artists' order, or by year,
newest first and albums with no year last. The jump goes by the initial of
whichever it is - an artist's, or an album's - and by year, a year at a time.

Muse+ has five tabs, **Artists**, **Albums**, **Favorites**, **Playlists** and
**Coverflow** (on audiobooks **Authors**, **Books**, **Favorites**,
**Playlists** and **Coverflow**),
named along the bottom with L2 and R2 at its ends. L2 and R2 step between them,
round from the last to the first, and Muse+ opens on the tab you left it on.
**Tab Settings** (MENU, then UI Settings) lists the tabs in order with On or Off: A (or
left/right) switches one, and L2 and R2 move it up and down, which is the
order along the bottom too. One order for music and audiobooks; at least one
tab besides Playlists stays on.

- **Artists** lists the artists with how many albums each has. Up and down
  step, left and right jump by initial, L1 and R1 a screenful. A opens that
  artist's albums as a list, and B comes back to the artist.
- **Albums** lists every album with its cover on the right and its year and
  artist under it, as a Detailed List shows a game. Y favorites the album
  under the cursor, or takes it off.
- **Favorites** is the same list with only the albums you favorited
  (**Favorite Albums**; on audiobooks **Favorite Books**). Y on one takes it
  off, and it leaves the list.
- **Playlists** lists your playlists with how many tracks each has; A opens
  one. X (or a long press on the RG Rotate) on a playlist offers **Delete**
  (asked first) and **Rename** (on the keyboard); MENU is the Muse+ menu, as on
  every tab. In a playlist, X on a track offers **Remove from Playlist** (asked first), and L2 and R2
  move it up and down a place. On audiobooks it holds three lists Muse+ makes
  itself: **Recent Books** (the ten books you played last and have not
  finished), **Finished Books** (every finished book, newest first) and
  **Random Books** (twelve books, different each time you open it). On Audiobookshelf they
  include what you listened to elsewhere, such as on your phone. A on a book
  plays it; these lists can't be edited.
- **Coverflow** is the shelf of album covers, the way a console's opens onto
  its games: left and right move, up and down jump from one initial to the
  next, L1 and R1 a window of covers. Y favorites as on Albums. Leaving an
  artist's albums for Albums or Coverflow keeps the album you were on.

A on an album opens its tracks, the same list with the album's cover. With
something playing, SELECT opens Now Playing and B goes back to that album's
tracks and then its artist's albums. Kept across restarts.

**Playlists** are `.m3u` files in `Music/Playlists/`, so other players and a
computer can read and edit them. **X on any track** - an album's or a
playlist's - adds it to a playlist: one you have, or a new
one named on the keyboard. In a playlist, X offers **Remove from Playlist**
as well.

**SELECT** opens Muse+ from anywhere that is not a running game: the
shelf, every menu, and the in-game menu, where the game waits paused
underneath. SELECT again closes it from any screen in Muse+, and the music keeps
playing. B goes back one screen: Now Playing to the album's tracks, the tracks
to the shelf, and the shelf out of Muse+. The exceptions are Over The Hare and a
Box Art fetch, which only keep working while they are on screen.

Albums go in `Music/`, a folder per artist and one inside it per album:
`Music/Radiohead/The Bends/01 Planet Telex.mp3`. A folder of
tracks straight under `Music/` is filed under its artist when its tags say
clearly who that is (the album artist, or the artist every track agrees on),
moving to `Music/<artist>/<album>/` with its cover; one with no clear answer -
a podcast, a mix - stays an album of its own. Songs straight in `Music/`, in
no folder at all, are moved into `Music/Singles/` the next time the shelf is
read, a name already there getting " (2)", and Singles has a cover of its own
at `Music/.media/Singles.png` until you replace it. Over The Hare
reaches `Music/` too, and albums sent that way are on the shelf when you leave
its screen. MP3, M4A, M4B, AAC, FLAC, Ogg, Opus and WAV play. Tracks play in
file-name order, and the number at the front of a file name is left off the
name shown.

**Audiobooks** go in `Audiobooks/`, in the same two shapes: an author's folder
with a folder per book, or a book's folder on its own. A book is one M4B or a
folder of files, which play in name order. A on a book goes straight to Now
Playing and carries on where you left it: its place is kept every thirty
seconds while it plays, whenever it pauses or moves to the next file, and when
the device turns off. B from there goes back to the list you picked the book
from. A book played to its end says Finished under its cover,
and A on it starts it over. In the lists and on Coverflow a book you have
started has a half-filled circle before its title, and a finished one a
filled circle. X on a book has **Mark as Complete** and **Mark as Unread**;
Unread puts it back at the start, and its Timeline (below) stays, so a place
lost to a slip can be gone back to. **Delete Book** (asked first) removes the
book's files and its folder from the card, stopping it if it is playing; a
folder that holds another book keeps that book. Books always play in order, and on books Sort By
is by author or by title and there is no Album Art. A book's cover is the one its files carry, or the picture in its
folder: one named cover or folder, or the only one there.

With something playing or paused, SELECT goes straight to **Now Playing**: the
cover, the track, where in it you are, and what comes next. From the next
release it also shows the format in the cover's corner (such as FLAC 16/44.1
or OPUS 128) and where the track comes from (Source: Local, Plex or
Audiobookshelf).

| | |
|---|---|
| **A** | pause, or play |
| **L1/R1** | previous / next track, or a book's previous / next file; more than three seconds in, L1 starts it over |
| **Left/Right** | back or ahead ten seconds; held, a minute a step after a second and five minutes after three |
| **Y** | the play mode (music) |
| **X** | **More** (books): Speed, Sleep Timer, Chapters, Timeline |

**Now-Playing Layout** in UI Settings picks how the screen is laid out.
`Longer Titles` (the default) puts the title, artist and album across the top,
the title on up to two lines, with the cover and the progress bar under them.
`Original` keeps the words beside the cover, sliding when they are too long.

**Hotkeys** in Playback Settings lists Play/Pause, Skip Forward, Skip
Backward, Next Track and Previous Track, each with its buttons. Press **A**
on one, then L1, R1, L2, R2, a D-pad direction or a stick direction to add
it (B cancels); a button belongs to one action at a time, so it moves from
any action that had it. **X** clears an action's buttons. They are the same
for every Source, and A, B, X and Y stay as they are. **Global** there has
the volume keys' two holds and Volume Up + Down together; a hold set to None
goes back to stepping the volume while it is held. (In a game a hold set to
None does nothing.) **Reset to Default** asks first, then puts them all back.
A headset's own buttons are not changed by any of this.
| **B** | the album's tracks; for a book, the list it was picked from |
| **MENU** | Muse+'s menu |
| **SELECT** | close Muse+ |

**More** (X on a book's Now Playing):

- **Speed**: 0.8x to 2x in steps of 0.1, kept for each book. Any speed but 1x
  shows on Now Playing.
- **Sleep Timer**: Off, or 10 to 90 minutes. When it runs out the book pauses
  and the device sleeps, even mid-book. Now Playing shows the minutes left
  ("Sleep 23m"). A game stops the countdown, and it starts over at its full
  length when the game closes. Every play or pause starts it over too, unless
  **Play Resets Sleep Timer** (Playback Settings) is turned off. It is not
  kept across restarts.
- **Chapters**: an M4B's chapters, or a book's files. A plays from the start
  of the one you pick; the one already playing just carries on. L1/R1 and a
  volume-key hold change a book's file as they change a track; a headset's
  next/previous seek instead.
- **Timeline**: every play, pause and sync of the book, newest first: when,
  what, and where you were - including those made with the screen off. A sync
  is the place taken from Audiobookshelf because another device was further
  on. A goes back to that spot.

**Y changes the play mode on Now Playing**, and only there: in order, then
repeat all, then repeat one, then shuffle, and round again. Its mark sits
beside the track count on that screen, which is the one place a mode is shown -
so the mode is set where it can be read. Shuffle starts from the track you
chose and plays the rest in a random order, and when
it runs out it shuffles again and carries on - never starting the new round
with the song that ended the last. The mode is kept across restarts.

**While music plays, a running game is silent.** Only the game's own sound:
the music, the volume keys and the mute switch all carry on as before. Pause
the music, or let the album end, and the game is heard again within a tenth of
a second. SELECT in the in-game menu is how to switch between them mid-game.

An album's tracks are a list over its shelf, with what is playing under a rule
at the foot. A on a track plays the album from there and opens Now Playing; on
the track already playing it just opens Now Playing. The shoulders and the
d-pad's sides do what they do on Now Playing.

Sleep leaves music playing. A tap of POWER while an album plays turns only the
screen off, and the album plays on. So does leaving it alone: while music
plays, how soon the screen goes off is **Auto Screen Off** in Playback Settings:
**Follow System** (the default) uses Auto Sleep's time, or Auto Off's when sleep
is off; or pick 5s to 1m, or Never.

In the dark the volume keys change the volume and a headset's buttons work,
both without lighting the screen. Now Playing's buttons (A, Y, the shoulders, the d-pad's
sides) do what they do there and wake the screen, like an iPod's. Any other
button, or a tap of POWER, just wakes it. Set **Wake Screen On Press** to `No`
(MENU in Muse+, then Playback Settings) and the screen stays dark: Now Playing's
buttons still work, every other button is ignored, and only POWER wakes it.
When the music stops, paused or at the album's end, the screen stays dark and
the same buttons keep working for the **Paused Sleep Delay** (Playback
Settings; not on the Miyoo Mini Flip), so a pause in the dark can be undone in
the dark and a Bluetooth headset stays connected. **Follow System** (the
default) waits as long as Auto Sleep, or Auto Off when sleep is off; or pick
30s to 10m. Then the device suspends, or with Auto Off it powers down. On a
charger it doesn't suspend. NextUI pauses the music to sleep; this is plorpOS's own.

**Auto Rewind** (Playback Settings, off to begin with) starts a book or a
podcast episode a little back when it has been paused a while: 3 seconds
after ten seconds paused, 10 after a minute, 20 after five minutes and 30
after half an hour. It is not offered on Local Music or Plex.

For a pocket, lock the buttons. On every device but the Miyoo Mini Flip that is **Sleep Button Lock** in
Playback Settings (Off/On); while it is on and the music plays with the screen dark, the face
and shoulder buttons (and the Rotate's touch panel) are ignored. The volume keys,
Vol+ and Vol- together (pause/play), a volume-key hold (next/previous track) and the
Rotate's side media key stay live, and so do POWER and a headset's buttons. On the
Brick, set **Mute Switch** (System Settings) to `muse button lock` instead: the switch
then no longer mutes; it works like an iPod's hold switch, locking the same buttons
while it is down. Playback Settings' row reads `lock switch on`, grayed out, while
the Mute Switch has it. With the screen on, the
switch locks nothing; Now Playing shows a padlock beside the mode while it is
down, so you can see it is set before the screen goes dark.

Covers come from the music. The first time an album is shown, Muse+ takes the
picture its files carry and keeps it beside the album, in
`Music/<artist>/.media/`, the way box art sits in `Roms/<system>/.media`. An
album whose files carry none gets a generated card.

**Album Art**, in Local Music Settings, fetches covers over Wi-Fi for every
album with none, or with one smaller than the shelf draws it - 461px. It asks
MusicBrainz which record the album is, from the artist and album folder names
and the number of tracks - the track count is what tells a same-named single,
or another band, from the album - and takes that record's front cover from
the Cover Art Archive at 500px. An album it has supplied is remembered and not
asked about again; one it could not find is asked about on every run, since a
cover may be added later. MusicBrainz takes a request a second, so a large
library takes a while: seven albums took a minute on the Brick.

Off Wi-Fi, a stream or a download from Plex, Audiobookshelf or Podcasts is
not tried: Muse+ says **Device is offline**. On Wi-Fi with the server not
answering it says **Could not reach the server**. Either way nothing is
skipped and a book's place stays where it was.

### Plex

*Coming in the next release; not in v1.9.x.*

Muse+ can play a Plex music library instead of the card's (not on the Miyoo
Mini Flip: a big library doesn't fit in its memory). Set **Source** to
Plex in Muse+'s MENU. The first time it asks you to sign in: open
plex.tv/link on a phone or computer and enter the code on the screen. Then
pick the server and the music library. Muse+ reads the whole library once
(about 40 seconds for 47,000 tracks) and keeps it on the card, so it opens at
once after that. Plex needs Wi-Fi. The tabs, Now Playing, the queue and the
second screen work as they do for the card's music.

- **Streaming** plays at the quality in **Plex Settings**: Original, or an
  Opus transcode at 96 to 320 kbps (128 by default; transcoding needs Plex
  Pass). On a server with no Plex Pass, Muse+ says so once, shows it beside
  the server's name in Plex Settings, and plays and downloads the original
  files; if the server's owner has turned downloads off for your account,
  a download says so instead of starting.
- **X on an album** (in any album list, Coverflow, or its track list)
  downloads it, or deletes the copy. Progress shows under the cover. While
  it downloads, X offers **Cancel Download**, which stops it and removes
  what it got so far. Downloads go in
  `Music/Plex/<artist>/<album>/` with the cover, so they are ordinary music
  when Source is the card too. A downloaded track plays from the card, even
  on Plex. A fully downloaded album has a ↓ mark in the lists.
- **Download Queue** shows in Muse+'s menu, under Plex Settings, while albums
  are downloading: the one downloading, then the rest. X on one cancels it.
- **X on a track** (or a long press on the RG Rotate), then **Add to Playlist**, lists **Favorite Plex Tracks** first: A adds the track, or
  removes it if it's in already. It's a playlist on the Playlists tab, and it
  follows your server both ways: a track in it is rated 5 stars on Plex (a
  Plexamp love), and a track you love elsewhere shows up in it. Changes made
  offline are sent later. **Y on an album** does the same for albums
  (Favorites tab).
- **Playlists** are your Plex server's: every sync (Check for Updates, or
  the hourly one) brings them in, and a playlist you make or change here -
  add, remove, move, rename, delete - is changed on the server at once, so
  it needs Wi-Fi. A smart playlist (one Plex fills by a rule)
  can be played but not changed, and one longer than 2000 tracks is left out.
  Plex's own ❤️ Tracks is left out too: Favorite Plex Tracks takes its place.
- **X on a playlist** adds **Download Playlist**: its tracks are copied to
  the card and it shows under Local Music as **Plex <name>**, the "Plex" in
  gray, with a ↓ mark here. **Keep Playlists Synced** in Plex Settings (on to begin with) keeps
  those copies in step with the playlist at every sync; Remove Download
  takes them off again.
- **Check for Updates** (in Plex Settings) reads only what changed on the
  server since the last sync, in seconds. Muse+ also does this every hour.
- **Resync Library** (in Plex Settings) reads the whole library again.
- **Pre-fetch Tracks** (in Plex Settings; 1, 3, 5 or 10) is how many of the
  next tracks Muse+ fetches while one plays, so they start at once with no
  loading. Fetched tracks share 100 MB on the card; the oldest go first.
- **Away from home, with [Tailscale](#tailscale)**: put your server's tailnet
  address (such as `100.10.0.0:32400`) in **Tailnet Server**, in Plex Settings.
  Muse+ tries it first while Tailscale is connected. If you already typed a
  Tailnet Server for another Source, the keyboard opens with that host.
- **Plex Settings** has the server, the library, both qualities and Sign Out.

Away from home, Muse+ reaches the server the way Plexamp does: its remote
address, or Plex's relay. It finds the way again by itself when the network
changes; a song cut off by that ends, and the next one plays.

Plex sees what Muse+ plays and counts a play at 90%, as Plexamp does. The
sign-in is kept on the card in `.userdata/shared/plex/`.

### Jellyfin

*Coming in the next release; not in v1.9.x.*

Muse+ can also play a Jellyfin music library (not on the Miyoo Mini Flip).
Set **Source** to Jellyfin Music and type the server's address with its port,
such as `192.168.0.2:8096`. Then sign in with **Quick Connect**: in Jellyfin
on a phone or computer, open your user menu, choose Quick Connect and enter
the code on the screen. **Use Password** signs in with your user name and
password instead. With more than one music library, pick one, or All music.

The first sync reads the whole library (47,000 tracks took 3 minutes on
home Wi-Fi, 12 over Tailscale) and keeps it on the card. After that Jellyfin
Music works the way [Plex](#plex) does:

- **Streaming Quality**, **Download Quality**, **Pre-fetch Tracks**, **Check
  for Updates**, **Resync Library**, **Keep Playlists Synced** and the hourly
  sync are the same, in **Jellyfin Settings**.
- **X on an album** downloads it to `Music/Jellyfin/<artist>/<album>/`.
  **Download Playlist** works the same way too.
- **Favorite Jellyfin Tracks** is the favorites playlist: adding a track marks
  it a favorite in Jellyfin, and your Jellyfin favorites show up in it. Y on an
  album does the same for albums (Favorites tab). It can be downloaded but not
  renamed or deleted.
- **Tailnet Server** works as Plex's: your server's tailnet address (such as
  `100.10.0.0:8096`), tried first while Tailscale is connected. An address that
  is not your Jellyfin server is refused ("Not a Jellyfin server", or "Not the
  same server" for a different one). Leave it empty to turn it off.
- If the server can't be reached when you switch to it, Muse+ says "Server
  offline. Retrying in 5 min" and keeps showing the copy on the card.

Jellyfin reads your files' tags, so a few server settings make a big
difference:

- Tag compilations with **ALBUMARTIST**, or they split into one album per
  artist. Multi-disc albums need **DISCNUMBER**.
- Jellyfin splits artist names on `/`, `;` and `|` ("AC/DC" becomes "AC" and
  "DC"). Fix it in the library's settings: custom tag delimiters plus a
  delimiter whitelist.
- A shared (non-admin) user needs **Allow media downloading** for downloads
  and audio transcoding allowed for the quality settings.
- Jellyfin plays some audio without asking for a sign-in, so keep the server
  off the open internet: use Tailscale or a reverse proxy to reach it away
  from home.

The sign-in is kept on the card in `.userdata/shared/jellyfin/`.

### Audiobookshelf

*Coming in the next release.*

Muse+ can play the books on an Audiobookshelf server on your home network.
Set **Source** to Audiobookshelf. The first time, type the server's address
(such as `192.168.0.2:13378`), then your username and password. Muse+ reads
the library once (about 10 seconds for 700 books) and keeps it on the card.
Books stream on every device; they need Wi-Fi and the server on the same
network.

- **Your place follows you.** Opening a book picks up where you left it,
  here or in the Audiobookshelf app, whichever was later. While a book plays,
  Muse+ sends its place every 15 seconds and when you pause, and marks it
  finished at the end. A downloaded book played without Wi-Fi sends its
  place once it's back online, unless another device has moved on since.
- **X on a book** downloads it, or deletes the copy; Mark as Complete and
  Mark as Unread there go to the server too. Downloads go in
  `Audiobooks/<author>/<title>/`, so they show under Local Audiobooks too,
  and still send their place to the server. A downloaded book has a ↓ mark
  in the lists, and while books download, **Download Queue** shows in
  Muse+'s menu under Audiobookshelf Settings (X on one cancels it).
- **Audiobookshelf Settings** has Check for Updates (only what changed),
  Resync Library (everything) and Sign Out. Muse+ checks every hour by itself.
  **Server** changes the address.
- **Away from home, with [Tailscale](#tailscale)**: put the
  server's tailnet address (such as `100.10.0.0:13378`) in **Tailnet Server**.
  Muse+ uses it while Tailscale is connected and the home address otherwise.
  Signing in at a tailnet address fills in Tailnet Server; add the home
  address under Server later. An address that doesn't answer where you are is
  saved anyway.

The sign-in is kept on the card in `.userdata/shared/abs/`.

### Podcasts

*Coming in the next release.*

Set **Source** to Podcasts. The first time, Podcast Settings opens so you can
add some:

- **Import OPML** brings in the shows you follow in another app. Export an
  OPML file from it (AntennaPod: Settings, Import/Export), put it in
  `Podcasts/` on the card, and choose it.
- **Search Podcasts** finds shows by name in Apple's podcast directory. A on
  one subscribes to it.

Muse+ fetches every show's new episodes when you open it (at most every half
hour) and when you choose **Check for Updates**. Nothing is fetched in the
background.

The tabs are **Podcasts** (your shows; A shows a show's episodes, newest
first), **New** (the 100 newest you have not played), **Downloads**, **Favorites**
and **Queue**. A row shows a dot and the length for an episode you have not
started, the time left, or Played. A played episode's title is grey, and one
that is in the Queue says **Queued** on its cover on the other tabs.

- **A** plays an episode. It streams, so it needs Wi-Fi, unless it is
  downloaded. If the stream drops, it stops and keeps your place; A carries
  on from there. It plays like a book: X has Speed, Sleep Timer and
  Timeline, and each episode keeps its own place.
- An episode counts as **played** at 95%.
- **X on an episode**: Download (or Cancel, or Delete the copy), Mark Played
  or Unplayed, Add to Queue or Remove.
- **Y** marks a favorite.
- **Queue** is what plays next. The episode you play goes to the top. When it
  ends it leaves the Queue and the next one starts; when the Queue is empty,
  playback stops. In the Queue, L1/R1 move an episode up or down.
- **X on a show** unsubscribes. If you downloaded some of its episodes, you
  choose whether they go too.

Downloads go in `Podcasts/<show>/` and play without Wi-Fi. A downloaded
episode has a ↓ mark, and while episodes download, **Download Queue** shows
in Muse+'s menu under Podcast Settings (X on one cancels it).
**Auto-Delete** in Podcast Settings removes a download when it is played, or
a day after; favorites are never deleted.

### Offline Mode

*Coming in the next release.*

**Offline Mode**, at the top of Plex Settings, Audiobookshelf Settings and
Podcast Settings, keeps that Source off the network: only what is downloaded
shows, and nothing is asked of the server. Muse+ says *Offline Mode* when it
opens on that Source.

Audiobookshelf places and Plex plays made meanwhile wait on the card and go
to the server once Offline Mode is off and there is Wi-Fi. A place moved on
another device since is kept, not overwritten.

## The shelf

A single row of cards in perspective, with reflections - Cover Flow, carried
over from an earlier project by the same author and retuned. The focused card
sits in a soft glow tinted with its system's color, and the whole background
carries a wash of that color that eases as you move between systems.

Three things about that are yours to choose, and they are independent.

**UI Theme** picks the art. `Plain Jane` is a drawn card per system, with the
name on it. `Fancy Pants` is a photograph of the console itself, background
removed, and because a photograph does not name itself the shelf writes the
name underneath. The art is in `plorpOS/cards/` on the card.

**System Order** picks the order of the systems. `Year` is release order, with
PICO-8 and Arcade first. `A-Z` is by name. `Maker` groups them - Arcade,
Nintendo, Sega, NEC, SNK, Sony, then PICO-8 - each group in release order.
Favorites stays first and Muse+ last whichever you pick, and the shelf reorders
the moment you change it.

**Gamelist Layout** picks how a game shelf looks. The systems row is always
the row above; this is about the games inside a system, and Favorites
and Ports follow it too. Muse+ keeps its own shelf.

`Coverflow` is the row above, with box art.

`Detailed List` puts the games in a column on the left and the focused game's
art on the right, with its year and genre under it and your place in the
shelf (`11 / 156`) under that; a game nobody has scraped leaves that line blank. Up and down
move, left and right jump by letter, L1/R1 a screenful, and the cursor rides
the middle of the column once the list is long enough to scroll.

`Art List` is the same list with only your place in the shelf under the art.

Card art comes from, in order:

1. box art in `Roms/<system>/.media/<name>.png`, whether you put it there or
   the Box Art row fetched it. With a ScreenScraper account the fetch asks
   ScreenScraper first, and brings the game's year, genre and synopsis with the
   cover. Without one, or for a game ScreenScraper does not have, it asks
   libretro: by file name, and where no name matches, by checksum: a zip
   records its ROM's CRC, which names the game in No-Intro's list - the catalog
   libretro files its covers under - so a file still carrying an older name
   finds its box anyway. On the main card that found 29 more, every one the
   right game. When libretro's server is down, the run fetches the same
   covers from libretro's copy on GitHub instead. Fetched art is shrunk to the size it is drawn at - libretro
   ships covers at its own resolution, which on some systems is two to three
   times the pixels this screen can show, and every one of those pixels is
   decode time on every scroll past. Art you put there yourself is never
   touched, whatever size it is - only what the fetch brings in;
2. **the autosave preview** - the frame you were looking at when you stopped,
   which for a game in progress is a better card than any box;
3. a generated slab: the system's color, the title, and the title's first
   letter enormous and barely there behind it.

On Ports, Box Art needs no network: it takes each port's picture and
details from the `gameinfo.xml` PortMaster installed with it.

**Adding a cover by hand.** Some games have no cover anywhere the fetch looks -
fan translations, homebrew, a dump no catalog lists. Put one in yourself over
Over The Hare: open `Roms/<system>/.media/` in its file browser and upload a
PNG named exactly like the ROM file, with `.png` in place of its extension -
`Black Castle.gb` wants `Black Castle.png`. Box Art never overwrites a cover
that is already there, and Replace only swaps yours out when ScreenScraper or
libretro has a cover to put in its place. Keep it near 512 pixels on the long
side: a cover you add is drawn as it is and never resized, so a 2000-pixel scan
is decoded at full size whenever its card comes into view.

Pressing A on a game with an autosave does not start it, it continues it.
