# Configuration

**Settings live in a database, not in files.** Two of them, and the split is
deliberate:

| | holds | why it is separate |
|---|---|---|
| `.userdata/<platform>/tortos.db` | volume, brightness, Auto Sleep, Auto Off, audio output, Wi-Fi, Bluetooth, display mode per system, and the RetroAchievements and ScreenScraper accounts | per handheld. A card moved to another device should not carry the first one's screen and speaker settings, or its account token |
| `.userdata/shared/.tortos/library.db` | timezone, startup system, core options | per card. It travels with the library, the same way favorites and earned achievements do |

The shipped defaults are **compiled into the launcher** and seed whichever
database is missing them, so there is no config file to ship, none to drift from
the code that reads it, and a deleted database comes back working.

## Reading it

Nothing on the device can open a database - there is no `sqlite3` binary - so
the launcher prints it:

```
tortos.elf --dump
```

That is deliberately read-only. Settings are not hand-edited any more; every one
of them is reachable from a menu.

## What the boot script reads

`launch.sh` needs five values before `tortos.elf` exists - the panel brightness
for before the boot animation, the timezone, and whether each radio should come
up. It is POSIX shell and cannot read a database, so the launcher exports
`.userdata/<platform>/boot.env` and the script sources it.

That file is **derived, never authoritative**. Delete it and the next boot runs
at the shipped defaults, then the next settings change rewrites it. It replaced
six `sed` invocations across four files, each one a fork, so the boot path got
shorter rather than longer.

## `systems.cfg`, which is still a file

`TortOS/systems.cfg` is the one that did not move, because it is **build input
rather than a setting**. `mk/payload.sh` reads it twice on the host: once to
refuse a card whose `systems.cfg` names cores that `vendor/` does not have -
which is how a card was nearly built with four of nine systems dead - and once
to create the ROM folders. Neither can wait for a database that only exists on
the device.

```
sys | display name | Roms/ folder | core | tag | card art | accent | extensions | disc bios
```

The **tag** keys the per-system display mode and the favorites list. It is not
what saves and states hang off - states are keyed on the folder and `.srm` files
are named after the ROM, in `Saves/<folder>/`. `system_cfg` declares `tag[8]`, so up
to seven characters, and changing a tag orphans that system's display mode and
favorites.

## Core options

They are entries in the library database rather than files, seeded from the
values compiled into the launcher.

**Core options** are keyed `coreopt.<tag>.<option>`, with an empty tag for a
global - so `coreopt..mgba_sgb_borders` applies everywhere and
`coreopt.GB.mgba_gb_model` to Game Boy alone. A tagged entry overrides a global
of the same name. A core that does not declare a key ignores it, so a key meant
for one core is harmless everywhere else. They are sent **before** the game
loads, because a core reads its `(Restart)` options during load and one set
afterwards does nothing until the next launch.

**Game Boy palettes** are per game on the Game Boy and Color shelves (the Color
shelf's original Game Boy games only): the in-game menu's Palette row stores
`palette.<TAG>.<rom file>` (the label, e.g. `GB Pocket`), and a game with no row
plays the shelf menu's **Default Palette** (`palettedef.<TAG>`, **Auto** until
set) - Auto is what a Game Boy Color does with nothing held, its own colours for
the games in its boot ROM and Dark Green for the rest. The labels are one list
for every core: each launch sends the game's palette as its core reads it
(Gambatte's `gambatte_gb_colorization` and `gambatte_gb_internal_palette`, or
mGBA's `mgba_gb_colors_preset` and `mgba_gb_colors`) after the core options
above, so a game keeps its palette when its Core changes. A save state does not
carry the palette.

**Display defaults and per-game overrides.** The shelf menu's **Default Display
Mode** (the DS's **Default Layout** and **Default Display**) and **Default
Shader** are the shelf's (`display.<TAG>`, `shader.<TAG>`, `dsfill`); the in-game
menu's Display, Shader and the DS's Layout and Display are the one game's
(`display.<TAG>.<rom file>`, `shader.<TAG>.<rom file>`, `dsfill.<TAG>.<rom
file>`), and a DS game keeps the screen **Swap Screens** left big
(`dsswap.<TAG>.<rom file>`), all in `tortos.db`. A game without its own plays at
the shelf's default. The shelf menu's **Reset Display Overrides** deletes that
shelf's per-game rows (and, on the Game Boy shelves, its per-game palettes).

**Cores per game.** The Game Boy, Color and Advance shelves' Core row turns the
game under the cursor to another core (`engine.<TAG>/<rom file>`): Gambatte
(Game Boy and Color) or armGBA (Advance) by default, PokeDaisy, or mGBA. A game
with no row plays the default. A save state is its core's own, so a game played
on mGBA before can be moved: turn it to mGBA, load its state, save in the game,
then turn it back - the in-game save (`.srm`) is the same file for every core.

**Multi-disc games** are a folder holding the discs and an `.m3u` that lists
them, one file name per line (`Roms/PlayStation/Policenauts/Policenauts.m3u`);
the folder shows on the shelf as one game. A game launched from an `.m3u` gets
a **Disc** row in the in-game menu, under Palette, whenever the core reports
more than one disc. Picking a disc opens the virtual tray, swaps, and closes it
after a second of play, which is when a game waiting for "insert disc 2" notices.
The choice is stored as `disc.<TAG>.<m3u file>` (0-based) and sent at the next
launch, so a game resumed from its auto-state finds the disc it was saved on.
Every disc shares the one memory card, named after the `.m3u`.

**Nintendo DS (DSperate)** has its own options (dsperate-lr v3.3.0-lr.1; the
full table is in [its README](https://github.com/thwonp/dsperate-lr#core-options)).
There is no menu for them yet (plorpos-l07.6), so they are set as
`coreopt.NDS.<option>` rows, which apply to every DS game. Defaults in bold:

| option | values | what |
|---|---|---|
| `dsperate_frameskip` | **auto**, slotplus, disabled | skip 3D rendering when the frontend says a frame will run late; never touches audio. Brick and H700 only (the other ports never ask) |
| `dsperate_slot2` | **empty**, rumble pak | the Rumble Pak is opt-in because a device in slot 2 costs CPU in games that poll the slot (Golden Sun: Dark Dawn on a Brick: 0 frame skips without it, 456 with it). Needs a device with a motor: only the Brick has one wired so far |
| `dsperate_gba_game` | **none**, or the name of a GBA game (no `.gba`) | the GBA game for the dual-slot DS games (Pokemon, Kirby Canvas Curse, ...), looked up in `Roms/Game Boy Advance`. A name the DS game does not accept is refused and the option keeps `none`, so a console-wide value only takes effect in the games that accept it. Its save is `<name>.sav` in the save directory |
| `dsperate_guitar_grip` | **auto**, off | the Guitar Hero games get their Grip by themselves; frets are L, L2, R2, R |
| `dsperate_analog` | **off**, or a CPAD patch name | feeds the sticks and ZL/ZR to one of the built-in analog patches for the game (the log lists the names). On the GKD the stick also moves the pen cursor, so both would move |

Slot 2 is set before the first frame, so these need a fresh launch. A resume
state remembers what was in slot 2: if it differs, the state loads with slot 2
starting afresh (the log says so).

One trap worth repeating: **a resume state beats these.** A save state carries
the machine it was made on, so changing an option that selects hardware will not
appear to work on a game you have already played. Test on a game that has never
been launched, or delete its `.auto.state`.

**Turbo** has no entries any more: it is chosen in the game and lasts for that
game only. See [turbo.md](turbo.md).

## What the launcher still writes as files

Two things, each for a reason:

| | |
|---|---|
| `cheevos-active.set` | Diatom reads it, handed over as a path on RUN under its ADR-0026. Moving it would mean diatom linking sqlite and learning the schema |
| `systems.cfg` | build input rather than a setting, as above |

Everything else is in one of the two databases. Favorites are rows keyed
`fav.<tag>\t<file>` in the library, earned achievements are `chv.<game>.<id>`,
and the RetroAchievements account is `ra.user` and `ra.token` in the device
database.
