# How plorpOS works

Developer notes for plorpOS 2.0. The launcher (`tortos.elf`) and diatom run on
the Brick, H700 handhelds, Rotate, Mini Flip, RG DS, GKD (BasedOS), Zero 28
and Zero 40. The RG Nano runs nanoshelf and PicoArch instead
([install-nano.md](install-nano.md)).

Numbers are measured on a TrimUI Brick unless a device is named. Each has its
date: older ones show the shape, not today's reading.

# Resident emulator

**plorpOS never starts a process to run a game.** A cold start on the Brick
costs ~1100 ms, and almost none of it is the game:

| step | cost |
|---|---|
| `GFX_init` (SDL video + EGL/GL context) | ~620 ms |
| `dlopen` of the core | ~170 ms (179 ms `genesis_plus_gx`, cold) |
| audio and settings | ~140 ms |
| **opening the ROM** | **~36 ms** |

### The socket

[Diatom](https://github.com/thwonp/diatom) (MIT libretro frontend, originally
by Eric Reinsmidt) starts at boot on a Unix socket and stays up all session.

- The launcher sends `RUN` with the core, the ROM, and the paths for the
  resume state and card preview.
- Diatom answers `RUNNING`, `EXIT reason=` or `ERROR code=`.
- Warm launch: 26-44 ms `RUN` to `RUNNING`, ~70 ms from pressing A (Contra).
- No socket (first second after boot, or diatom died): the launcher runs the
  same `diatom` binary standalone for that game, then starts a fresh resident
  once the display is back.

### Cores

- Every core is `dlopen`ed at startup and never unloaded, `RTLD_LOCAL` so the
  shared `retro_*` symbols don't collide. Exception: the Mini Flip (101 MB)
  maps a core on first use.
- Six mapped + one running: 15.0 MB of the Brick's 975 MB. The nine cores of
  the time cost 31 MB free at boot; least free in any game was 604 MB. The DS,
  Gambatte, armGBA and PokeDaisy cores came later and are not in these figures.
- Mapping replaced `cat` into the page cache (480 ms cold, I/O only);
  `dlopen` is 382 ms and is paid in idle seconds at boot (2026-09-08).

### In-game menu

- The launcher's own. MENU makes diatom hand over the display with a preview
  of the paused frame; Continue, Save, Load, Reset and Save and Quit are one
  protocol line each.
- **Display** cycles the running game's mode live. **Achievements** shows
  progress, or `none` (unselectable) with no set.
- Volume and brightness share one levels channel, so changes in a game come
  back to the launcher.

### The launcher stays up

The launcher keeps its GL context through the game (Mini Flip: its software
renderer). Returning from a game is one frame, not a re-init.

# Boot

### Brick: one still picture

- u-boot draws the wordmark from `bootlogo.bmp`; `pic2fb` redraws it from
  `splash.png` once Linux is up. No boot animation (it held the shelf back).
- Cuts (Brick and Brick Pro, 2026-11-27, plorpos-act):

| cut | saved |
|---|---|
| `loglevel=8` -> 4 (kernel printing to 115200-baud serial) | 2.2 s |
| boot hook mounts the card itself | ~1.4 s |
| scan cached per system while its folder is unchanged | cold scan 4.3 s -> 0.5 s (big card) |
| Brick Pro: one `grep` of 8 MB `bluetoothd`, not two | 3.4 s |

Reset to kernel is still ~7 s, all in the stock bootloader.

### Startup phases

Logged, cumulative from process start. One Brick boot, 1708 games in eleven
systems, 2026-09-13; a second boot matched within 5%. Cold = first launcher of
the boot; warm = restarted with a hot page cache.

```
                        cold      warm
boot: scan              249 ms    103 ms
boot: video+input       977 ms    524 ms
boot: font+settings    1040 ms    529 ms
boot: card assets      1181 ms    641 ms
```

- `card assets` alone is 141 ms cold (was 480 ms): opening covers are queued
  for the workers instead of decoded before the first frame.
- `scan` grows with the library (64 ms when first recorded); the rest is fixed
  cost.
- Every phase but `card assets` is slower than the 2026-09-07 figures on the
  same library. Unexplained; recorded, not rounded away.

### Card art off the render thread

- Decoding a card is 14-60 ms against a 16.7 ms frame; inline, a scroll into
  uncached art cost 100-400 ms frames.
- Two worker threads decode (one still dropped cards on the largest covers);
  they sleep when idle. Only the GPU upload (1-2 ms) stays on the main thread.
- A card not yet decoded is not drawn: an empty slot, never a frozen shelf.
- A system's cards start decoding when the systems row lands on it, not when
  it opens (2026-09-14).

# Saves

- **Autosave:** every exit (Quit, power button, launcher stop) writes the state
  and preview at the paths from `RUN`. One funnel: no exit forgets, none saves
  twice.
- **Auto-resume:** every launch hands over `<ROM file>.auto.state`; present =
  resume, absent = fresh start. Games Auto-Resume off hands none over; the
  autosave is still written.
- **Manual:** six slots in the in-game menu, no confirm pop-up.
- States are keyed on the system's `Roms/` folder; battery `.srm` files are
  named after the ROM, in `Saves/<Roms folder>/`, beside the core's own files
  (memory cards, `fbneo/`). Same title on two shelves = two saves. The
  `systems.cfg` tag does not key saves (checked 2026-09-01).
- **Neo Geo Pocket Color** reports zero bytes of save memory, so there is no
  `.srm`; progress lives only in the autosave state. Loading an older slot
  rewinds the cartridge save too.

# Achievements

### Diatom evaluates

Conditions compare against the previous frame (`0xH06f0<d0xH06f0`). The
launcher polls the socket every 100 ms (six frames at 60 Hz) and would miss
unlocks, so it declares the console and hands over the set, and diatom checks
every frame (diatom
[ADR-0025](https://github.com/thwonp/diatom/blob/main/docs/decisions/0025-achievements-belong-to-the-frontend.md),
[ADR-0026](https://github.com/thwonp/diatom/blob/main/docs/decisions/0026-achievements-on-the-launcher-protocol.md)).

### Sets

- Sign in under MENU > Game Settings > RetroAchievements > RetroAchievements
  Sign-In. Only the password is typed; the returned token is stored.
- First launch online: hash the ROM, ask RA for the game, cache the set at
  `Roms/<System>/.cheevos/<name>.set`. After that: instant, works offline.
- **Download Achievement Sets** (plorpos-3yv.107.3) does this for the whole
  library: one game at a time, one request started or polled per frame, hashing
  on a thread (a GBA cart is 32 MB). A cached set costs one `access()`.
  - "No achievements": no game (hacks, translations), RA's "Unsupported Game
    Version", or a set with nothing to earn (many arcade).
  - Five real failures in a row stop the run as offline.
- DS games hash like RA does (header, ARM9, ARM7, icon) via rcheevos'
  `hash_rom.c`, not the whole 128 MB cart.
- HTTPS: the Brick's `curl 7.54.1`/`OpenSSL 1.1.0i` has no trust store, the
  Mini Flip and GKD ship their own curl; `res/ssl/cacert.pem` ships on every
  card ([README](../res/ssl/README.md)).

### Host tool and agreement checks

- `tools/ra-sets.py` fetches the same set files from a host (seeding,
  offline). Over the 180-ROM test library: 166 games with sets, 8,297
  achievements, every condition parses.
- Hash rules, JSON-to-set conversion and the set format exist twice (device and
  host). `make check-rahash` and `make check-raset` require both to agree: a
  wrong hash fails silently, as an unknown game.

### Unlocks

- Sent after the game ends, never from the frame loop. Unsent ones stay queued
  on the card and retry next time online.
- At game start the account's record is merged in: the account wins on what
  exists, the local store on what is still owed.
- RA's `Warning: Unknown Emulator` entry (unregistered client) is dropped, never
  stored.

# Settings

### Two databases

Settings are database rows, not files. Defaults are compiled into the launcher
and seed a missing database, so a deleted one comes back working.

| file | scope | holds |
|---|---|---|
| `.userdata/<platform>/tortos.db` | per device | volume, brightness, Auto Sleep/Off, audio output, radios, UI and Muse+ prefs, display mode / shader / core per shelf and game, RA and ScreenScraper accounts (`ra.user`, `ra.token`) |
| `.userdata/shared/.tortos/library.db` | per card | timezone, startup system, core options, hotkeys, button maps, favorites (`fav.<tag>\t<file>`), recents, play time, achievements (`chv.<game>.<id>`), palettes, discs, turbo maps |

- No `sqlite3` on the device: `tortos.elf --dump` prints both, read-only. Every
  setting is reachable from a menu.
- `.userdata/<platform>/boot.env` is what `launch.sh` sources before the
  launcher exists: `BRIGHTNESS`, `TIMEZONE`/`TZRULE`, `WIFI`, `BLUETOOTH`,
  `SSH` (Flip). Derived, never authoritative: rewritten when one changes;
  deleted = one boot at defaults (`src/db.h`).

### systems.cfg

`plorpOS/systems.cfg` (source `config/systems.cfg`) stays a file because it is
build input: `mk/plorpos-files.sh` refuses a card whose cores `vendor/` lacks
and creates the ROM folders.

```
sys | display name | Roms/ folder | core | tag | card art | accent | extensions | disc bios | maker
makers|Arcade|Nintendo|Sega|...
```

- `maker` and the one `makers|` line drive System Order's Maker grouping.
- `tag` (`char tag[8]`, max seven chars) keys display mode, sort order,
  favorites and per-game settings. Changing it orphans them.

### Core options

- `coreopt.<tag>.<option>` in the library db; empty tag = global
  (`coreopt..mgba_sgb_borders`). Tagged beats global. A core ignores keys it
  doesn't declare.
- Sent **before** the game loads: `(Restart)` options are read during load.
- **A resume state beats hardware options:** it carries the machine it was made
  on. Test on a never-launched game, or delete its `.auto.state`.

### Shelf defaults and per-game overrides

| setting | shelf default | game's own | db |
|---|---|---|---|
| Display Mode | `display.<TAG>` | `display.<TAG>.<rom>` | device |
| DS Layout / Display | `display.<TAG>` / `dsfill` | `dsfill.<TAG>.<rom>`, `dsswap.<TAG>.<rom>` | device |
| Shader | `shader.<TAG>` | `shader.<TAG>.<rom>` | device |
| Core | `engine.<TAG>` | `engine.<TAG>/<rom>` | device |
| Hotkeys + Button Mapping | `hotkey.<TAG>`, `btnmap.<TAG>` | `ghotkey.<TAG>/<rom>` + `gbtnmap.<TAG>/<rom>` (written together) | library |
| Palette | `palettedef.<TAG>` (Auto) | `palette.<TAG>.<rom>` | library |
| Turbo | `turbomap.<TAG>` | `turbomap.<TAG>/<rom>` | library |
| Disc | - | `disc.<TAG>.<m3u>` (0-based) | library |

- No own row = shelf default. The own row is a full copy taken on first change;
  later default changes don't reach it.
- In a game, changes are written only while **Save Per-Game Overrides**
  (`saveoverrides`, default on) is on. Saved rows always apply.
- Reset Overrides deletes one kind of per-game row for a whole shelf.

### Palettes, cores, discs

- **Palette** (GB, and original-GB games on the Color shelf): labels are one
  list for every core; each launch sends Gambatte's
  `gambatte_gb_colorization`/`gambatte_gb_internal_palette` or mGBA's
  `mgba_gb_colors_preset`/`mgba_gb_colors` after the core options. Auto = what
  a GBC does: boot-ROM colours, else Dark Green. Save states don't carry it.
- **Core per game:** GB, GBC, GBA and DS shelves (and the Flip's SNES and Neo
  Geo Pocket) have several: Gambatte (GB/GBC) or armGBA (GBA) by default,
  PokeDaisy, mGBA. `.srm` is shared by every core; states are per core (load
  on the old core, save in-game, switch back).
- **Multi-disc:** a folder with the discs and an `.m3u` (one name per line)
  shows as one game. A **Disc** row appears under Palette when the core reports
  more than one disc: open tray, swap, close after a second of play. The
  choice is sent at the next launch so an auto-resume finds its disc. One
  memory card per game, named after the `.m3u`.

### Nintendo DS options

DSperate (dsperate-lr v3.3.0-lr.1,
[README](https://github.com/thwonp/dsperate-lr#core-options)). No menu yet
(plorpos-l07.6): set `coreopt.NDS.<option>` rows, which apply to every DS game.
Default first.

| option | values | notes |
|---|---|---|
| `dsperate_frameskip` | **auto**, slotplus, disabled | skips 3D only when a frame will be late; never audio. Brick and H700 only |
| `dsperate_slot2` | **empty**, rumble pak | rumble costs CPU in slot-polling games (Golden Sun: Dark Dawn, Brick: 0 vs 456 skips). Needs a motor |
| `dsperate_gba_game` | **none**, GBA name (no `.gba`) | dual-slot games, from `Roms/Game Boy Advance`; refused names stay `none`. Save `<name>.sav` |
| `dsperate_guitar_grip` | **auto**, off | Guitar Hero frets on L, L2, R2, R |
| `dsperate_analog` | **off**, CPAD patch name | sticks + ZL/ZR to a built-in patch (names in the log). On the GKD the stick also moves the pen |

Slot 2 is set before the first frame (fresh launch). A resume state with a
different slot 2 loads with slot 2 reset (logged).

### Files the launcher still writes

- `.userdata/<platform>/cheevos-active.set`: diatom reads it by path from `RUN`
  (ADR-0026); a db would mean diatom linking sqlite.
- `systems.cfg`: build input, above.

# Turbo

### How it runs

- The map is per game (`turbomap.<TAG>/<rom>`, format `x:a,a:a` =
  button:turbo), with a shelf default; see the table above. Older
  `turbo.<TAG>/<rom>` rows are converted once; `persistturbo` is no longer read.
- Diatom, not the core, pulses, so it is the same on every system, and keeps
  no state. The Turbo Assign hotkey and its `TURBO` reports are retired
  (plorpos-5tm.26; diatom ADR-0045): turbo is set only on the Turbo
  Assignments screen under Hotkeys, live in a game through
  `plat_live_turbo`/`plat_live_turbo_set`.
- The launcher holds the map and sends the whole map on each change:
  `b:b~3` per turbo button, plus `l2:none,r2:none` on GBA. It also sends it
  just after `RUN`, which resets the map to identity.
- `turbo_period` is half a cycle in frames (3 = ~10 Hz at 60 Hz); see diatom
  `src/env.c`, ADR-0028.

### Why X and Y are not turbo any more

Before plorpos-tkh, X/Y were a fixed turbo A/B on two-button systems
(`turbo.<tag>` = `x:a~3,y:b~3`), and mGBA's Turbo L/R sat on GBA's L2/R2. Both
took buttons players want for hotkeys. The launcher now drops `turbo.<tag>`
rows, and GBA's L2/R2 map to nothing.

# Menus

Every list screen is drawn by `menu_draw` (`src/main.c`). It abstracts
*drawing* a menu, not *being* one; the rules below came from five copies of the
same defect (2026-09-04).

### Row kinds

A `menu_row` is `{ label, value, live }`. Don't add a kind without changing
`menu_draw` and `menu_measure`.

| kind | built as | meaning |
|---|---|---|
| Item | `{ "Wi-Fi", "on", true }` | does something |
| Placeholder | `{ "No networks found", NULL, false }` | a real row that does nothing yet; highlights one step quieter |
| Rule | `MENU_RULE` | divider, same bar as under the heading; the list ends here |
| Note | `MENU_NOTE("Y: rescan")` | centred text about the list, not in it |

Text that is not a would-be item is a note, not a placeholder.

### Rules a screen must follow

- **Use `menu_run`.** It steps over dead rows with `menu_step_sel`, a bounded
  walk: a list can have no live rows at all. Never write `(sel + 1) % n`.
- **Note any key other than A/B** (Y rescanned Wi-Fi for months, unlabelled).
- **Confirm destructive actions with `confirm_panel`**, which starts on Cancel
  and handles power and idle itself.
- **Name the consequence, don't refuse:** forgetting the connected Wi-Fi is
  allowed, with a warning.
- **Never paint menu chrome** (panels, rules, plates, text fitting) in a screen;
  that is `menu_draw`'s.
- **`menu_measure` must agree with `menu_draw`.** It is a separate copy of the
  width pass for sizing a panel without drawing it.

### The runner

- `menu_run` owns polling, quit, power, idle, back, the cursor and the frame. A
  screen supplies `build` and `on_key` ([ADR-0001](decisions/0001-screens-declare-menus.md)).
- `build` must run with no renderer and no device, so `make check` can ask what
  a menu contains (`src/wifi_menu.c` + `tools/menu-check.c`, `src/sys_menu.c`).
- `menu_style` is the renderer half: fixed width, accent, `follow_tint`,
  `backdrop` (NULL = dimmed shelf) and `on_power` (NULL = power off). The
  in-game menu overrides both.
- `menu_run` returns how it ended. The in-game menu needs it: B and MENU mean
  Continue (send `RESUME`), and the runner flushes input on exit.
- **MENU closes the whole menu; B goes back one screen.** MENU raises
  `g_menu_closing` and every screen under it leaves. Own-loop screens call
  `menu_leaving(a)`. The flag is lowered only outside menus (shelf loop,
  `game_menu`, Muse+), never when a menu opens.
- Not on the runner: `cheevos_screen`, and the Muse pickers
  `muse_long_pick`, `muse_tset_run`, `muse_tabs_screen`.

### Where a setting lives

| menu | changes | build |
|---|---|---|
| Shelf MENU | Sort By, `<Shelf> Defaults`, Reset Overrides, Scrape Shelf, Rescan Folder | `sys_menu_build`, `sys_menu_defaults_build`, `sys_menu_reset_build` |
| X menu > Game Overrides | one game's own rows; Shelf Default deletes the row | `go_rows` |
| In-game menu | the running game; writes its own row only under `plat_save_overrides()`; never a shelf default | `game_menu` |

Reset Overrides counts per kind when it opens and after each reset, not per
frame.

### Cheevos is not a menu

`cheevos_screen` borrows `menu_draw` but not the runner, on purpose:

- `live` means earned there; the runner would skip every unearned one.
- `CHV_MAX` is 256 (largest set 203); the runner holds `MENU_RUN_ROWS` (32).
- A closes it; L1/R1 page by eight.

A second screen wanting emphasis without action gets a `dim` flag on
`menu_row`, not a runner switch.

# Ports

PortMaster is unofficial on every port that has it (not the Mini Flip, not the
Nano).

### Where things live

| | path |
|---|---|
| PortMaster and its runtimes | `.userdata/shared/portmaster/` (`XDG_DATA_HOME`) |
| ports | `Roms/Ports/` |
| runner | `plorpOS/<device>/portmaster/pm-run.sh` (RG DS: the old ROCKNIX GKD runner) |
| tools PortMaster needs (python, bash) | `pmtools.squashfs` beside the runner, read-only, loop-mounted only while a port runs (Brick, H700, GKD, Zero 28/40) |
| 32-bit userland | H700: SDL2 in `plorpOS/lib32/`; Zero 28/40: `lib32.squashfs` (armhf + software GL), mounted per run |

Every port goes through `pm-run.sh SCRIPT [ARGS]`; Delete Game is
`pm-run.sh --uninstall ZIP` (PortMaster's own uninstall, per `port.json`).

### Running a port

- The port is a process group of its own: frozen, thawed and killed as one
  (its game is the script's grandchild). Ending it TERMs the group but the
  script, so the script runs its own cleanup.
- **MENU over a port** opens Continue / Reset / Quit over the frozen game;
  Quit ends the port and returns to the shelf.
- Fullscreen at the panel's size; sound through the speaker or Bluetooth.
- Some ports need the game's own files (Downwell, To the Moon, Stardew Valley)
  where the port's page says. A first run may download a runtime (Java for
  Mindustry, ~47 MB); LOVE ships with PortMaster.
- A killed port leaves runtime mounts; the next run unmounts only plorpOS's
  and PortMaster's, never the firmware's.

### Per device

| device | notes |
|---|---|
| Brick | firmware SDL; `/ports` alias on tmpfs (stock FAT rejects the lowercase path) |
| H700 (BaseOS) | no bash/python3: pmtools; 32-bit via box86 + `plorpOS/lib32/` SDL2 |
| GKD (BasedOS) | has bash and SDL2, no python3; read-only root. Each run in its own mount namespace (diatom's `retroarch` stand-in over `/usr/bin`; mounts vanish with the last process). `portscale.so` scales small fixed-size SDL games |
| RG DS (ROCKNIX) | sway: windows moved to the main screen fullscreen; Ren'Py keeps its shape (bars). Tested: native 64/32-bit, LOVE, Mono, .NET, GameMaker (GMLoader 64/32), Java, Godot 3/4, Westonpack (Wayland, X11), box86, Python, Pyxel, Ren'Py, Qt6. Braid needs Panfrost (RG DS is Mali); Dust Racing 2D wants a Qt6 runtime name PortMaster doesn't install |
| Rotate | sway + Xwayland on the first X11 port; plorpOS's own Mesa (no LLVM, GLES 1) |
| Zero 28 / 40 (oakMOSS) | the Brick's runner on oakMOSS SDL. First port adds a pad udev rule, `/run` (-> `/tmp/run`) and 32-bit lib links (`/usr/lib32` and neighbours). 32-bit = software GL (no 32-bit GPU driver); GameMaker slow (MEGA MAN Sunrise ~13 fps) |

# Device notes

### GKD (BasedOS)

- BasedOS = the fork's userland on the GKD's own kernel, drivers and firmware;
  no compositor, starts faster than ROCKNIX did.
- The zip is the card root: `System/launch_frontend.sh`, `plorpOS/`, empty
  folders, `INSTALL.md`.
- No system curl: `plorpOS/curl` (Zero 40's static build) does all HTTP.
  Tailscale runs userspace; plorpOS HTTP goes via tailscaled's proxy
  `127.0.0.1:1055` (`src/tailscale.h`).
- Launcher gives up after 5 starts that exit in under 5 s; the first runs OTA
  rollback if an update was just installed; then BasedOS's own screen.
- Boot logos: U-Boot's replaced by `plorpOS/gkdb/bootlogo.bmp.gz` (stock kept
  as `.stock`); BasedOS's keys logo is `bootlogo-keys.bmp.gz`.

### RG DS

- Image: trimmed ROCKNIX (emulators and ES removed). First boot (fs-resize)
  makes exFAT `PLORPOS` partition p3 from free space, copies
  `plorpos-payload.tar`, restarts. Mounted at `/storage/games-external`.
- adb: Debian adbd + `adbroot.so` on a FunctionFS gadget, replacing ROCKNIX's
  USB network/MTP gadgets; no authorisation.
- Main screen is DSI-2 (top) by default.
- Lid: hall sensor taken from logind, read as a level every 100 ms; closed for
  1 s or more = POWER tap. A unit started shut doesn't sleep.

### Rotate

- Data partition p4, exFAT `PLORPOS`, made by `plorpos-firstboot.sh` whenever
  it has no plorpOS (also after a wipe).
- `sd/rotate/session.sh` replaces only the exact old dtb (md5 `f71e3340...`),
  keeps `.bak-f71e3340`, restarts once via system-updated.

### Mini Flip

- Stock `/customer/main` runs `/mnt/SDCARD/.tmp_update/updater` instead of
  MainUI.
- Gambatte replaced mGBA for GB/GBC in v1.9: about half the cost per frame.
- Scanline Filter is drawn by the Flip's 2D engine.
