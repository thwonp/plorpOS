# plorpOS guide

Every control, menu and setting that is the same on every device. What differs
by device is in its install guide.

# Installing

Each device has its own install guide:

- [TrimUI Brick, Brick Hammer, Brick Pro](install-brick.md)
- [Anbernic H700: RG SP, RG34xxSP, RG35XX H, RG35XX SP](install-h700.md)
- [Anbernic RG DS](install-rgds.md)
- [Anbernic RG Rotate](install-rotate.md)
- [GKD 350H Ultra](install-gkdb.md)
- [MagicX Zero 28](install-zero28.md)
- [MagicX Zero 40](install-zero40.md)
- [Miyoo Mini Flip](install-flip.md)
- [RG Nano](install-nano.md)

BIOS files per console: the README's
[Supported systems](../README.md#supported-systems). Building it yourself:
[building.md](building.md).

macOS leaves `._name` files beside everything it copies. plorpOS skips them;
`dot_clean /Volumes/YOURCARD` removes them.

# Controls

**MENU > Controls** has all of this on the device: a page per place (moving,
the shelf, a game, Muse+, anywhere; Touch on touch screens), drawn for your
Gamelist Layout.

### Buttons

| Button | Does |
|---|---|
| **D-pad** along the row | move (systems; games on Coverflow) |
| **D-pad** the other axis | on Detailed List and Art List, move down the list; on Coverflow, jump to the previous / next initial |
| **L1/R1** | jump a screenful |
| **A** | open a system, start or continue a game |
| **B** | back |
| **X** | the game's info screen (X menu) |
| **Y** | favorite. Favorites is a shelf of its own |
| **Volume** | volume, everywhere. Held: steps after 300 ms, then about 11 a second |
| **MENU + volume** | brightness, everywhere, held the same way |
| **MENU** | a menu for where you are: the plorpOS menu (systems row), the system menu (a shelf), the in-game menu (a game) |
| **MENU** inside a menu | closes the whole menu. B goes back one screen |
| **MENU**, held 0.7 s | the game switcher (or what **Menu Hold** says). A tap still opens the menu, on release |
| **SELECT** | Muse+ from anywhere but a running game (the in-game menu included). Again to close. **Select Shortcut** changes it |
| **POWER** tap | sleep: screen dark at once, suspend 10 s later. A tap wakes. In a game, the game is saved first |
| **POWER** held | save and power off |

- Left stick = d-pad, where there is one.
- Right stick and stick clicks: never seen by games, free for hotkeys.
- Device keys (Fn1/Fn2, Mute Switch, Home, side key, lid, swivel): see your
  install guide.

### Touch screens

RG DS, RG Rotate, Zero 40. Each install guide has the full touch table.

- Tap selects; a tap on the selected row opens it (starts the game).
- Swipe down from the top edge = SELECT (**Select Shortcut (Top Swipe)**).
- Swipe up from the bottom edge = MENU held (**Menu Hold (Bottom Swipe)**).
- Both work in a game too.

### Game switcher

Your recent games as cards.

- Left/Right pick, **A** plays, **X** removes, **Y** favorites.
- Picking another game from inside a game ends the first (saved).

### Never sleeps on a charger

Charging or on a computer, the device never sleeps, suspends or powers off by
itself. Auto Off, Auto Sleep and suspend wait until it is unplugged.

# The plorpOS menu

MENU on the systems row: Play Time, Wi-Fi, Bluetooth ([Audio](#bluetooth)),
Network Services, System Settings, UI Settings, Game Settings, Controls, About.

### Play Time

- Left/Right: all time, this year, month, week, today.
- **Y**: one row per game or per system.
- The selected row shows launches, longest session, last played, and (when
  not zero) sessions that ended in a flat battery or a crash.

### Wi-Fi

- The switch and the networks in range. **A** joins, **Y** rescans, **X**
  forgets (asked first; the connected one too).
- The footer shows the network and the device's address.
- Joining a network sets the clock from the internet.

### Network Services

| Row | Does |
|---|---|
| **SSH**, **Samba** | where the device has them: see your install guide |
| **Syncthing** | [below](#syncthing) |
| **Tailscale** | [below](#tailscale) |
| **Over The Hare** | the file server, [below](#over-the-hare). Needs Wi-Fi |
| **Check for update** | [below](#check-for-update) |

### Over The Hare

1. **MENU > Network Services > Over The Hare** shows an address and a PIN.
2. Open the address in a browser on the same network and type the PIN.
3. Drag games into their console's folder, albums into `Music/`, books into
   `Audiobooks/`.

- The PIN is new every time.
- What you sent is on the shelf when you leave the screen.
- It only works while its screen is open.
- **Download logs** (bottom of the first page) gives one `.tar.gz` of the last
  ten boots' logs, with network, headset and account names masked.

### Tailscale

**Network Services > Tailscale** puts the device on your tailnet. Not on the
Miyoo Mini Flip.

- First time: A shows a QR code. Scan it, or open the link under it, and sign
  in. The screen closes once the device is on.
- The row then shows the tailnet address (use it for SSH).
- It signs back in by itself, reboots included. A on the row turns it off.
- Names: `plorpos-brick`, `plorpos-brickpro`, `plorpos-h700`, `plorpos-rotate`,
  `plorpos-rgds`, `plorpos-gkd`, `plorpos-zero40`, `plorpos-zero28`. A
  duplicate gets a number; rename it in Tailscale's admin console.
- MagicDNS names and subnet routes work.
- GKD: only plorpOS's own traffic uses the tailnet. See
  [install-gkdb](install-gkdb.md#tailscale).

### Syncthing

**Network Services > Syncthing** keeps `Saves/` in sync with your other
devices. Not on the Miyoo Mini Flip.

- On, the row shows its web page (like `192.168.0.55:8384`). Open it on the
  same network, sign in as `plorpos` / `plorpos`, add your devices and share
  Saves.
- It shares Saves only.
- It stays on across reboots until you switch it off.
- Its ID lives on the card, so a reinstall keeps it.
- Anyone on the network can sign in with that password. Switch it off on
  networks you don't trust.

### Check for update

**Network Services > Check for update** looks for a newer plorpOS on GitHub.

1. It asks before downloading.
2. It asks to restart. The update installs during the restart. **Cancel**
   installs it at the next restart instead.

- Needs free space of about four times the download; it says so if there
  isn't.
- **B** stops a download. Unpacking can't be stopped.
- Settings, saves, games and anything you added to `plorpOS/` stay.
- If the new version won't start, the old one comes back and leaves a note in
  `plorpOS/update-failed.log`.
- Full releases only, no pre-releases.
- A release that needs a new card image isn't offered: flash it (see your
  install guide).

### System Settings

| Row | Does |
|---|---|
| **Date & Time** | a row per part of the clock and one for the zone (one or two cities per hour, west to east). Left/Right, set at once, kept through a power-off. Reads `System` until you pick a zone |
| **Audio Output** | `Auto` or `Speaker`, and where Auto landed. See [Audio](#audio) |
| **Volume Limiter** | the most each output may go. See [Volume Limiter](#volume-limiter) |
| **Auto Off** | idle time before the device powers off (back where you were at next start). Setting it turns Auto Sleep to `never`, and the other way round |
| **Auto Sleep** | idle time before it sleeps, as a POWER tap. Counts only on the shelf and in menus, never during play |
| **Select Shortcut** | what SELECT opens where it opened Muse+: `Muse+` (default), `Switcher` or `off`. Inside Muse+ SELECT still closes it |
| **Menu Hold** | what holding MENU opens: `Switcher` (default), `Muse+` or `off` |

- **Menu Hold** on `Muse+` works from any screen. In a game, the game pauses
  under Muse+ and another hold goes back to it. Leave Muse+ any other way and
  the game ends (saved, as Save and Quit) and you land on the main shelf.
- On touch screens the two rows are **Select Shortcut (Top Swipe)** and
  **Menu Hold (Bottom Swipe)**.
- Device rows (Mute Switch, USB Debugging, Sleep Poweroff, Lid Close, Rotate
  rows): see your install guide.

### UI Settings

| Row | Does |
|---|---|
| **UI Theme** | `Plain Jane` (a drawn card per system) or `Fancy Pants` (a photo of the console, name under it). Art is in `plorpOS/cards/` |
| **Gamelist Layout** | `Coverflow`, `Detailed List` or `Art List`. See [Shelves](#shelves) |
| **System Order** | `Year` (PICO-8 and Arcade first), `A-Z`, or `Maker` (Arcade, Nintendo, Sega, NEC, SNK, Sony, PICO-8, each by year). Favorites stays first and Muse+ last |
| **Fonts** | below |
| **Battery Percentage** | the charge in a disc top right, green ring charging, red low. Off: only a red dot when low |
| **Visible Tiles** | which shelves have a card on the systems row: Ports, Muse+, every system with games. A or Left/Right toggles; the row changes when you close the list |

- A hidden shelf's games stay in Favorites, Recents and the switcher, and
  Scrape Systems still lists it. SELECT still opens a hidden Muse+.
- **Fonts**: **Header Font** and **Body Font** pick `Fredoka`, `Josefin Sans`,
  `Quicksand`, `Lato` or `Kosugi Maru`. **Header Size** and **Body Size** go
  from `50%` to `150%` in steps of 10.
- **Kosugi Maru** is the font for Japanese (kana and kanji). With it, accented
  letters show plain (é as e).
- Sizes start at `100%`, `90%` on the RG DS, the 4:3 H700s and the Zero 28,
  `80%` on the Brick Pro.

### Game Settings

| Row | Does |
|---|---|
| **Games Auto-Resume** | `on`: a launch loads the autosave. `off`: starts fresh (the autosave is still written) |
| **Rewind Speed** | `Off` (default), `1x` to `10x` |
| **Fast-Forward Speed** | `2x`, `4x` or `8x`, for both Fast Forward hotkeys |
| **Confirm Reset/Quit** | the in-game menu asks before Reset or Save and Quit |
| **Save Per-Game Overrides** | `on` (default): changes made in a game are kept for that game. Off: they last until it quits |
| **Rumble Enabled** | `on` (default): a game that rumbles shakes the motor. Greyed where there is none (motors: Bricks, H700s, RG Rotate, Miyoo Mini Flip) |
| **RetroAchievements** | below; shows the signed-in account |
| **Scraping** | below |

**RetroAchievements:**

- **Achievements Enabled**: `off`, games start without their set.
- **RetroAchievements Sign-In**: the account, or `sign in`. Reachable without
  a network.
- **Download Achievement Sets**: every game's set at once, for offline play.
  Skips cached games; counts games with nothing to earn.

**Scraping:**

- **Scrape Systems**: check systems (A; A on **All** checks or clears all),
  then START fetches their missing art in one run. Needs Wi-Fi.
- **ScreenScraper**: the account (covers plus year, genre, synopsis). `not in
  this build` when the build has no key.
- **Import gamelist.xml metadata**: reads `gamelist.xml` or
  `miyoogamelist.xml` from each `Roms/<system>/` for year, genre and synopsis.
  No network. **Fill in missing** or **Replace all**. Titles still come from
  file names.

### About

Version, address, battery, uptime.

# Shelves

### Look

- **Coverflow**: a row of box art.
- **Detailed List**: games in a column, the art on the right with year, genre
  and your place (`11 / 156`) under it. Up/Down move, Left/Right jump by
  letter, L1/R1 a screenful.
- **Art List**: the same, with only your place under the art.
- Favorites and Ports follow Gamelist Layout. Muse+ has its own.
- A on a game with an autosave continues it.

### System menu

MENU on a shelf of games. That system only.

| Row | Does |
|---|---|
| **Sort By** | `Name`, `Play Time`, `Last Played` or `Recently Added` (file date). Per system. Unknown games sort last |
| **<Shelf> Defaults** | what every game here plays with until it has its own (e.g. "Super NES Defaults") |
| **Reset Overrides** | not on Ports. **Reset All** on top, then a row per kind with how many games have one. Asks Yes/No. The defaults stay |
| **Scrape Shelf** | fetch this system's missing art (needs Wi-Fi) |
| **Rescan Folder** | read the card again for ROMs copied since boot (Over The Hare does it for you) |

On Favorites the menu is only Sort By.

### <Shelf> Defaults

| Row | Does |
|---|---|
| **Core** | a choice on Game Boy, Game Boy Color, Game Boy Advance, Nintendo DS (and some systems on some devices); PICO-8: fake-08 or native. Elsewhere it names the core |
| **Display Mode** | `Stretch`, `Aspect`, `Integer` (and `Integer Overscale` on square screens). Nintendo DS: **Layout** and **Display** |
| **Shader** | Left/Right steps, A opens the list. Greyed on native PICO-8 |
| **Hotkeys** | the shelf's hotkeys, Button Mapping and [Turbo Assignments](#turbo) |
| **Palette** | Game Boy / Game Boy Color: the starting palette |

Changes save at once. A game with its own value keeps it whatever the default
does later.

### X menu

X on a game:

| Row | Does |
|---|---|
| **Synopsis**, **Year**, **Genre** | once scraped; A opens the synopsis |
| **Achievements** | earned / total; A opens the list |
| **Game Overrides** | this game's own settings (not Ports or native PICO-8) |
| **Rename** | the shown name, on the keyboard. The file keeps its name; a rescrape never undoes it. Empty restores the old name |
| **Get Box Art** / **Replace Box Art** | a cover for this game (needs Wi-Fi). The old one stays unless a new one is found |
| **Delete Game** | asked first. Removes the file; saves stay. On Ports, PortMaster's uninstall, saves included |

**Game Overrides** lists Core, Display Mode (Layout and Display on the DS),
Shader, Hotkeys and Palette.

- Each reads **Shelf Default** until the game has its own. Left/Right/A cycle;
  picking Shelf Default drops the game's own.
- Hotkeys opens its screen (with Button Mapping and Turbo Assignments) and
  **Use Shelf Default**; it reads `custom` once the game has its own hotkeys
  or turbo.
- From Favorites or Recents it works on the game's own console.

Changes made in a game (Display Mode, Layout, Shader, Palette, Hotkeys,
Button Mapping, turbo, a DS game's swapped screens) are saved as the game's own
while **Save Per-Game Overrides** is on. In-game changes never touch the shelf's
defaults.

### Box art

A card's picture comes from, in order:

1. `Roms/<system>/.media/<name>.png`: yours, or fetched by Scrape Shelf /
   Scrape Systems / Get Box Art.
2. The autosave's last frame.
3. A generated card: the system's color and the title.

The fetch:

- With a ScreenScraper account it asks there first (and brings year, genre,
  synopsis).
- Otherwise, or if missing there, libretro: by file name, then for zips by
  checksum. If libretro's server is down, its GitHub mirror.
- Fetched art is shrunk to screen size. Art you added is never touched.
- Ports need no network: art and details come from PortMaster's
  `gameinfo.xml`.

**Adding your own:** a PNG named like the ROM, in `Roms/<system>/.media/`
(`Black Castle.gb` wants `Black Castle.png`). About 512 px on the long side; it
is never resized. Box Art never overwrites it, and Replace only swaps it when a
new cover is found.

# In a game

### In-game menu

MENU in a game:

| Row | Does |
|---|---|
| **Continue** | back to the game |
| **Save**, **Load** | save state slots |
| **Display Mode** | Stretch, Aspect, Integer... DS: **Layout** and **Display** |
| **Shader** | the game's shader |
| **Palette** | Game Boy |
| **Disc** | multi-disc games |
| **Achievements** | the game's list |
| **Hotkeys** | below |
| **Stylus Stick** | DS on a two-stick unit: which stick moves the stylus |
| **Reset** | restart the game |
| **Save and Quit** | write the autosave and go back to the shelf |

- SELECT in this menu opens Muse+; the game waits paused.
- Native PICO-8: Continue, Display Mode, Reset, Splore, Quit. Its Display Mode
  applies at the next Reset ("Reset game to apply").
- Over a port: Continue, Reset, Quit.

### Hotkeys

**Hotkeys** in the in-game menu (or a shelf's Defaults). Each console keeps its
own; none are set for you.

- Actions: **Fast Forward (Hold)**, **Fast Forward (Toggle)**, **Rewind**,
  **Quick Save**, **Quick Load**, **Screenshot**, **Save and Quit**. DS games add Screen Layout, Swap Screens, Blow Into Mic and stylus
  actions.
- **Quick Save** writes the game's Auto state and refreshes the Auto card's
  picture. **Quick Load** loads it.
- **Save and Quit** is the menu's Save and Quit in one press, with no
  question.
- Pick an action, **A**, then press its trigger: a button alone or under a
  modifier. **X** clears. A lone MENU tap cancels.
- Rewind is greyed ("off in settings") while **Rewind Speed** is Off.
- Modifiers and triggers per device: the README's
  [FAQ](../README.md#faq).

### Button Mapping

**Hotkeys > Button Mapping** moves the game's buttons: per game (in game, or
Game Overrides) or per console (<Shelf> Defaults).

- A row per button as the console names it (A, B, C, X, Y, Z on the Genesis; A
  to D on the Neo Geo; I and II on the TurboGrafx-16).
- A on a row, then the button that should send it. A button moved leaves its
  old row; a button on no row sends nothing.
- **X** clears a row. **Reset to Default** puts them back.
- Directions, START and SELECT never move. Menus and hotkeys read the real
  buttons.

### Turbo

**Hotkeys > Turbo Assignments**: in a game (in-game menu > Hotkeys), for a
shelf (**<Shelf> Defaults > Hotkeys**) or for one game (**Game Overrides >
Hotkeys**).

- A row per button: **None**, or the button it fires as a turbo (about ten
  presses a second while held). **Left/Right/A** step through. X set to Turbo
  A: X is a turbo A, A stays plain. A set to Turbo A: A is its own turbo.
- A, B, X, Y, L1, R1, L2, R2 can be turbo. Directions, START and SELECT can't.
- In a game a change works at once. It is kept as the game's own while **Save
  Per-Game Overrides** is on; off, it lasts until the game quits.
- A game with none of its own uses the shelf's. **Use Shelf Default** (Game
  Overrides) drops the game's own.
- A turbo follows Button Mapping: with A and B swapped, X set to Turbo A fires
  the game's B.

### Nintendo DS

- `.nds` files in `Roms/Nintendo DS/`.
- **Core**: DSperate-lr (default) or PokeDaisy DS. **Nintendo DS Defaults >
  Core** for the shelf, **Game Overrides > Core** for a game. PokeDaisy DS adds
  the Pokemon companion: Party, Dex, Map, Bag, Guide.
- **Layout** sets how the two screens sit: per shelf (**Nintendo DS Defaults >
  Layout**) or per game (in-game **Layout**). **Reset Overrides > Display
  Mode** puts every game back on the shelf's.
- **Display** (under Layout, where the panel allows more than one) sets how the
  big screen fills it: Aspect, Stretch or Integer.
- **Screen Layout** hotkey steps through the layouts and keeps the swap.
  **Swap Screens** hotkey puts the other screen big, remembered per game.
- **Stylus**: a touch screen, a stick (right by default; in-game **Stylus
  Stick** picks the left; always on, hides after 2 s still; **R Stick Click**
  taps), or the d-pad (bind **Stylus Mode** and **Stylus Tap**).
- No rewind, except on the GKD and RG Rotate. Save states work.
- Which layouts, Display options and stylus your device has: its install
  guide.

### While music plays

A running game is silent while Muse+ plays (the game's sound only). Pause the
music and the game is heard again. SELECT in the in-game menu switches between
them.

# Audio

Sound goes to the first of: **USB DAC, wired headphones, Bluetooth, speaker.**

- **Audio Output** (System Settings): `Auto` follows that order; `Speaker`
  refuses a DAC and Bluetooth (a jack still works, it is switched in hardware).
  The row shows where sound went, like `auto (wired)`.
- The jack has its own volume range, remapped when a cable goes in or out, so
  plugging in mid-game is not suddenly louder.

### Volume Limiter

**System Settings > Volume Limiter**: **Bluetooth Limit**, **DAC Limit**,
**3.5mm Limit**, 10% to 100% (100% = none). The speaker has none. Only the
outputs your device has are listed.

- The volume buttons stop at the limit. Muse+ follows it.
- A headset, DAC or cable never connects above its limit (or above 30% for a
  first-time headset or DAC).
- Lowering a limit, or switching to an output over it, drops the volume.
- In a game, one press past the limit can be heard for up to 0.1 s.

### USB DAC

Any USB audio device the kernel knows: dongle DAC, USB headset, desktop DAC.

- Plugged in, game sound, Muse+ and native PICO-8 move to it within a second;
  the row reads `auto (USB DAC)`.
- The volume buttons set the DAC's own volume. A DAC with no volume control
  plays at full volume.
- Unplugged, sound falls back to the jack or speaker; Muse+ pauses first.
- Which port: your install guide (Brick: the top port; Zero 28 / Zero 40: the
  OTG port with USB Debugging off).
- **Known issue, Zero 40:** a DAC plugged in or out with a charger connected
  can slow charging to a trickle. Re-plug the charger. Fixed on the Zero 28
  image; the Zero 40 fix comes with a new image.

### Bluetooth

Not on every device: see your install guide.

1. Put the headset in pairing mode.
2. **MENU > Bluetooth**, **Y** to search, **A** to pair and connect.

- **X** forgets (the bond and its cache go). The toggle on top switches the
  radio.
- Only named devices are listed; paired ones always are.
- Connecting one here disconnects any other. One reconnecting by itself never
  pushes another off.
- A paired headset reconnects by itself, at boot and mid-session, and sound
  moves to it at once, a running game included.
- The volume keys set the headset's volume, and its buttons move the
  device's. On a few headsets the device sets only the starting volume.
- The headset's play, pause and skip control Muse+, everywhere.
- Expect 100-150 ms of latency (any Bluetooth audio).
- If the headset goes, sound moves to the speaker within a second or two,
  Muse+ pauses, and the game keeps running.

# Muse+

The music, audiobook and podcast player.

### Opening it

- **SELECT** from anywhere but a running game (the in-game menu included; the
  game waits paused). Opens Now Playing if something is playing or paused,
  else the tabs. SELECT again closes it; music keeps playing.
- Its card on the systems row (last) opens the tabs, on the playing album.
  **Visible Tiles** hides it.
- **B** goes back a screen: Now Playing > tracks > shelf > out.
- With nothing to play it says where to put files (`Music/`, `Audiobooks/`)
  or to download from a server.

### Menu

MENU anywhere in Muse+:

| Row | Does |
|---|---|
| (count) | Albums, Books or Episodes |
| **Source** | Local Music, Local Audiobooks, [Plex Music](#plex), [Jellyfin Music](#jellyfin), [Audiobookshelf](#audiobookshelf), [Podcasts](#podcasts) |
| **Sort By** | artist (or author), album title, or year (newest first) |
| **Playback Settings** | screen-off, lock and rewind settings (below) |
| **UI Settings** | **Tab Settings**, **Now-Playing Layout**, **Visible Sources** |
| **Local Music Settings** | **Album Art**, **Rescan Folder** |
| **Local Books Settings** | **Rescan Folder** |
| **<Source> Settings** | for a server Source |
| **Download Queue** | while downloads run; X on one cancels it |
| **Hotkeys** | below |

- Muse+ opens on the Source you left it on. SELECT with a book playing opens
  the books.
- **Visible Sources** hides Sources you don't use. The one in use stays on.

### Tabs

Music: **Artists**, **Albums**, **Favorites**, **Playlists**, **Coverflow**.
Books: **Authors**, **Books**, **Favorites**, **Playlists**, **Coverflow**.

- **L2/R2** step between tabs (wrapping). Muse+ opens on the last one.
- **Tab Settings**: A or Left/Right turns a tab on or off; L2/R2 move it. One
  order for music and books. At least one tab besides Playlists stays on.
- **Artists**: albums per artist. A opens them; B comes back.
- **Albums**: every album with cover, year, artist. **Y** favorites.
- **Favorites**: only favorited albums. Y takes one off.
- **Playlists**: A opens one. **X** on a playlist: Delete (asked first),
  Rename. In a playlist, X on a track: Remove from Playlist; L2/R2 move it.
  RG Rotate: a long press does what X does here.
- **Coverflow**: album covers in a row. Up/Down jump by initial, L1/R1 a
  window.
- Books' Playlists tab: **Recent Books** (last ten unfinished), **Finished
  Books**, **Random Books** (twelve, new each time). Not editable.

### Music files

`Music/<artist>/<album>/<tracks>`, e.g.
`Music/Radiohead/The Bends/01 Planet Telex.mp3`.

- MP3, M4A, M4B, AAC, FLAC, Ogg, Opus, WAV.
- Tracks play in file-name order; a leading number is hidden.
- An album folder straight in `Music/` moves under its artist when the tags
  agree on one; otherwise it stays.
- Loose songs in `Music/` move to `Music/Singles/` (cover:
  `Music/.media/Singles.png`).
- Covers: the picture in the files, kept in `Music/<artist>/.media/`; else a
  generated card.
- **Album Art** (Local Music Settings) fetches covers over Wi-Fi from
  MusicBrainz / Cover Art Archive for albums with none or a small one. About a
  request a second, so a big library takes a while.

### Playlists

- `.m3u` files in `Music/Playlists/`, readable by other players.
- **X on any track** adds it to a playlist, or a new one named on the
  keyboard.

### Audiobooks

`Audiobooks/<author>/<book>/` or `Audiobooks/<book>/`. A book is one M4B or a
folder of files (name order).

- A on a book opens Now Playing and resumes. The place is saved every 30 s, on
  pause, at file change and at power-off.
- Half circle = started, full circle = finished. A finished book restarts on
  A.
- **X on a book**: **Mark as Complete**, **Mark as Unread** (back to the
  start; the Timeline stays), **Delete Book** (asked first; removes its files).
- Cover: the one in its files, or a picture in its folder.
- Books always play in order. Sort By: author or title.

### Now Playing

Cover, track, position, what comes next, the format (FLAC 16/44.1, OPUS 128)
and the Source.

| Button | Does |
|---|---|
| **A** | pause / play |
| **L1/R1** | previous / next track (or book file). After 3 s, L1 restarts |
| **Left/Right** | back / ahead 10 s. Held: a minute a step after 1 s, five after 3 s |
| **Y** | play mode (music): in order, repeat all, repeat one, shuffle |
| **X** | **More** |
| **B** | the album's tracks, or the list the book came from |

- The play mode shows beside the track count and is kept across restarts.
  Shuffle starts from your track and reshuffles when it runs out.
- **Now-Playing Layout**: `Longer Titles` (default; title on up to two lines
  over the cover) or `Original` (text beside the cover).
- In an album's tracks, A plays from that track.

**More** (X):

- **Speed** (books, podcasts): 0.8x to 2x, kept per book.
- **Sleep Timer**: Off, 10 to 90 min. Then playback pauses and the device
  sleeps. Shows "Sleep 23m". A game pauses the countdown (it restarts at full
  length after). Play/pause restarts it unless **Games Reset Sleep Timer** is
  off. Not kept across restarts.
- **Chapters** (books): an M4B's chapters or the book's files. A jumps there.
- **Timeline** (books, podcasts): every play, pause and sync, newest first. A
  goes back to that spot.

### Hotkeys (Muse+)

MENU > **Hotkeys**: Play/Pause, Skip Forward, Skip Backward, Next Track,
Previous Track.

- **A** on one, then L1, R1, L2, R2, a d-pad or stick direction. A button
  belongs to one action at a time. **X** clears. **Reset to Default** asks
  first.
- The same for every Source. A, B, X, Y don't change.
- **Global**: the volume keys' holds and **Volume Up + Down**. A hold set to
  None steps the volume (in a game it does nothing).
- On Now Playing with the screen on, a held volume key always steps the
  volume.

### Screen off

Sleep leaves music playing: a POWER tap turns off only the screen.

**Playback Settings:**

| Row | Does |
|---|---|
| **Wake Screen On Press** | `Yes`: Now Playing's buttons work in the dark and wake it; other buttons just wake it. `No`: the screen stays dark, other buttons are ignored, only POWER wakes |
| **Auto Screen Off** | while music plays: **Follow System** (default; Auto Sleep's time, or Auto Off's), 5s to 1m, or Never |
| **Paused Sleep Delay** | after a pause in the dark, how long the buttons stay live before the device suspends (or powers off with Auto Off). **Follow System** (default) or 30s to 10m. Not on a charger |
| **Sleep Button Lock** | `On`: face and shoulder buttons ignored while music plays with the screen dark |
| **Games Reset Sleep Timer** | a play/pause restarts the Sleep Timer |
| **Auto Rewind** | books and podcasts: back 3 s after 10 s paused, 10 s after 1 min, 20 s after 5 min, 30 s after 30 min. Off by default |

- In the dark, the volume keys and headset buttons work without lighting the
  screen.
- Under Sleep Button Lock these stay live: volume keys, Vol+ and Vol- together
  (play/pause), a volume-key hold (next/previous), POWER, headset buttons.
- Brick: the Mute Switch can be the lock instead (then the row reads `lock
  switch on`). See [install-brick](install-brick.md#controls).

### Offline

- Off Wi-Fi, streams and downloads aren't tried: **Device is offline**.
- Server not answering: **Could not reach the server**.
- Either way nothing is skipped and a book's place stays.

### Plex

Not on the Miyoo Mini Flip. A free Plex account works; no Plex Pass needed.

1. **Source > Plex Music**.
2. Sign in: open plex.tv/link and enter the code on screen.
3. Pick the server and the library. The first read keeps it on the card
   (about 40 s for 47,000 tracks).

- **Streaming**: **Streaming Quality** Original or Opus 96-320 kbps (128
  default). **Download Quality** likewise.
- **X on an album** downloads it (or deletes the copy) to
  `Music/Plex/<artist>/<album>/`; it then plays from the card and shows under
  Local Music too. During the download X offers **Cancel Download**. A
  downloaded album has a ↓.
- **Favorite Plex Tracks** (first in X > Add to Playlist) syncs both ways with
  5-star / Plexamp loves. **Y on an album** favorites it. Offline changes are
  sent later.
- **Playlists** are the server's. Changes here go to the server at once (needs
  Wi-Fi). Smart playlists play but can't be changed; over 2000 tracks are left
  out.
- **X on a playlist > Download Playlist**: copies it to the card as **Plex
  <name>** under Local Music. **Keep Playlists Synced** (on) keeps copies
  current.
- **Check for Updates** reads what changed (also hourly). **Resync Library**
  reads it all.
- **Pre-fetch Tracks** (1, 3, 5, 10): upcoming tracks fetched ahead; 100 MB
  shared, oldest go first.
- **Tailnet Server**: your server's tailnet address (`100.10.0.0:32400`),
  tried first while [Tailscale](#tailscale) is on.
- Away from home without Tailscale it uses the remote address or Plex's relay.
- Shared servers work. If the owner turned off downloads for you, a download
  says so.
- Plex counts a play at 90%.
- Sign-in: `.userdata/shared/plex/`.

### Jellyfin

Not on the Miyoo Mini Flip.

1. **Source > Jellyfin Music**, type the address with port
   (`192.168.0.2:8096`).
2. **Quick Connect**: in Jellyfin's user menu, Quick Connect, enter the code.
   Or **Use Password**.
3. With several music libraries, pick one or All music. The first sync keeps
   it on the card (47,000 tracks: 3 min at home).

- Works like [Plex](#plex): qualities, Pre-fetch Tracks, Check for Updates,
  Resync Library, Keep Playlists Synced, hourly sync, Download Playlist, all in
  **Jellyfin Settings**.
- Downloads go to `Music/Jellyfin/<artist>/<album>/`.
- **Favorite Jellyfin Tracks** syncs both ways with Jellyfin favorites. Y on
  an album does albums. It can be downloaded, not renamed or deleted.
- **Tailnet Server** as Plex's (`100.10.0.0:8096`). A wrong server is refused
  ("Not a Jellyfin server", "Not the same server"). Empty turns it off.
- Server unreachable: Muse+ shows the copy on the card and retries next time.
- Server side: tag compilations with **ALBUMARTIST**, multi-disc with
  **DISCNUMBER**. Jellyfin splits artists on `/`, `;`, `|` (fix with custom
  delimiters and a whitelist). A non-admin user needs **Allow media
  downloading** and audio transcoding.
- Keep the server off the open internet (some audio plays without sign-in):
  use Tailscale or a reverse proxy.
- Sign-in: `.userdata/shared/jellyfin/`.

### Audiobookshelf

1. **Source > Audiobookshelf**.
2. Type the address (`192.168.0.2:13378`), user name and password. The library
   is kept on the card (about 10 s for 700 books).

- **Your place follows you**, here or in the app, whichever is later. Sent
  every 15 s and on pause; finished at the end. Offline places are sent later
  unless another device has moved on.
- **X on a book** downloads it (or deletes the copy) to
  `Audiobooks/<author>/<title>/`; it shows under Local Audiobooks too and still
  syncs. Mark as Complete / Unread go to the server.
- Books' Playlists tab includes what you heard elsewhere.
- **Audiobookshelf Settings**: Check for Updates, Resync Library, **Server**,
  **Tailnet Server**, Sign Out. Hourly check.
- **Tailnet Server** (`100.10.0.0:13378`) is used while
  [Tailscale](#tailscale) is on. Signing in at a tailnet address fills it in.
- Sign-in: `.userdata/shared/abs/`.

### Podcasts

**Source > Podcasts**. The first time, Podcast Settings opens:

- **Import OPML**: export from your app (AntennaPod: Settings, Import/Export),
  put the file in `Podcasts/`, choose it.
- **Search Podcasts**: Apple's directory. A subscribes.

Tabs: **Podcasts** (shows; A lists episodes, newest first), **New** (100 newest
unplayed), **Downloads**, **Favorites**, **Queue**.

- New episodes are fetched when you open it (at most every 30 min) and on
  **Check for Updates**. Nothing in the background.
- **A** plays (streams on Wi-Fi unless downloaded). If the stream drops it stops
  and keeps the place.
- Plays like a book: Speed, Sleep Timer, Timeline. Played at 95%.
- **X on an episode**: Download / Cancel / Delete, Mark Played / Unplayed, Add
  to Queue / Remove.
- **Y**: favorite.
- **Queue**: what plays next. The playing episode goes on top and leaves when
  done. L1/R1 move an episode.
- **X on a show**: unsubscribe (you choose whether its downloads go).
- Downloads: `Podcasts/<show>/`. **Auto-Delete** removes a download when
  played, or a day after. Favorites are never deleted.

### Offline Mode

Top row of Plex, Jellyfin, Audiobookshelf and Podcast Settings. Keeps that
Source off the network: only downloads show. Muse+ says **Offline Mode** when
it opens there.

- Plays and places made meanwhile go to the server once it is off and there is
  Wi-Fi.
- A place moved on on another device since is kept, not overwritten.
