# Building plorpOS

Everything cross-compiles in Docker, so the host needs Docker (rootless podman
works), Python 3 with Pillow and ffmpeg (only for regenerating art), and
`sshpass` for the Brick's `make deploy`. No device is needed to build, except
`mk/fetch-sysroot.sh` (Brick), `mk/fetch-gkd-sysroot.sh` (ROCKNIX GKD) and
`mk/fetch-rgds-sysroot.sh` (RG DS), which pull the device's own libraries.

Every target takes `PLATFORM=` (default `brick`) and builds into
`build/<platform>/` (the Brick into `build/`). One version number for every
port: `VERSION` in the Makefile.

## Once

```sh
make toolchain                  # tortos-toolchain image (mk/toolchain.Dockerfile)
make vendor                     # fetched cores, hash-pinned, + PortMaster.zip -> vendor/
```

## Cores every aarch64 card ships

`make payload` refuses a card whose `systems.cfg` names a core `vendor/` does
not have, and names the script to run. Each script pins its own hashes:

| Script | Core | Into |
|---|---|---|
| `mk/build-mgba-bridge.sh` | mGBA (buildbot core crashes on MBC2 carts; delete the script when libretro's fork syncs) | `vendor/cores` |
| `mk/build-pcsx-rearmed.sh` | PlayStation | `vendor/cores` |
| `mk/build-fbneo.sh` | Arcade, Neo Geo | `vendor/cores` |
| `mk/build-fake08.sh` | PICO-8 (fake-08) | `vendor/cores` |
| `mk/build-gambatte-core.sh` | Game Boy, Color | `vendor/cores-brick` |
| `mk/fetch-armgba-core.sh` | Game Boy Advance | `vendor/cores-brick` |
| `mk/build-pokedaisy-gba-core.sh`, `-gbc-core.sh`, `-ds-core.sh` | PokeDaisy | `vendor/cores-brick` |

The buildbot versions of pcsx_rearmed, fbneo and fake08 need a newer glibc
than the Brick's 2.33; that is why they are built.

## Per device

Every payload also needs a diatom built for the same platform: `DIATOM_ELF`,
default `../diatom/build/<platform>/diatom` (Flip: `../diatom-miyoo/...`).

| Device | Sysroot | Extra prerequisites | Build and package |
|---|---|---|---|
| TrimUI Brick / Brick Pro | `mk/fetch-sysroot.sh` (adb) | `mk/build-dsperate-brick.sh`, `mk/build-pmtools-brick.sh`, `mk/build-unzip.sh` | `make && make payload` -> `out/sd/`, `out/plorpOS-v<VER>-brick.zip` |
| GKD 350H Ultra (BasedOS) | `mk/fetch-gkdb-sysroot.sh`: the fork's Buildroot output (`BR_OUT`) and the H700's sysroot for Muse+'s FFmpeg; no device | `mk/build-dsperate.sh`, `mk/build-melondsds.sh` (-> `vendor/cores-gkd`); `mk/build-curl-zero40.sh`; the H700's `mk/build-pmtools-h700.sh` and `make PLATFORM=h700`; diatom's GKD build | `make PLATFORM=gkdb && make PLATFORM=gkdb payload` -> `out/gkdb/`, zip |
| GKD 350H Ultra (ROCKNIX, not released in 2.0) | `mk/fetch-gkd-sysroot.sh` (ssh) | `mk/build-dsperate.sh`, `mk/build-melondsds.sh` (-> `vendor/cores-gkd`) | `make PLATFORM=gkd && make PLATFORM=gkd payload` -> `out/gkd/` (development only, no zip) |
| Anbernic RG DS (ROCKNIX) | `mk/fetch-rgds-sysroot.sh` (ssh) | `mk/build-dsperate.sh`, `mk/build-melondsds.sh` (-> `vendor/cores-gkd`), `mk/fetch-rgds-adbd.sh` | `make PLATFORM=rgds && make PLATFORM=rgds payload` -> `out/rgds/card`, zip; then `mk/rgds-image.sh <ROCKNIX RK3566 image>` (card image) |
| Anbernic H700 (BaseOS) | `mk/fetch-h700-sysroot.sh` (~10 min) | `mk/build-dsperate-brick.sh`, `mk/build-pmtools-h700.sh`, `mk/build-sdl2-armhf.sh` | `make PLATFORM=h700 && make PLATFORM=h700 payload` -> `out/h700/`, zip |
| RG Rotate | `mk/fetch-rotate-sysroot.sh` | `mk/build-dsperate.sh`, `mk/build-melondsds.sh` (-> `vendor/cores-rotate`), `mk/build-unzip.sh`; image: `mk/build-ports-mesa.sh` | `make PLATFORM=rotate`, then `mk/rotate-deploy.sh package` (update zip) and `mk/rotate-image.sh` (card image) |
| Miyoo Mini Flip | `mk/fetch-miyoo-sysroot.sh` | `docker build -t miyoo-toolchain -f mk/miyoo-toolchain.Dockerfile mk`; cores: `sh mk/flip-cores/build-all.sh` | `make PLATFORM=miyoo`, then `mk/miyoo-deploy.sh package` and `verify` |
| MagicX Zero 40 (oakMOSS) | the Brick's, `mk/fetch-sysroot.sh`; and the H700's (`mk/fetch-h700-sysroot.sh`) for Muse+'s FFmpeg | `mk/build-dsperate-brick.sh`, `mk/build-dropbear-zero40.sh`, `mk/build-curl-zero40.sh`, `mk/build-lib32-zero40.sh`, `mk/build-pmtools-h700.sh zero40`; diatom `make PORT=zero40` | `make PLATFORM=zero40 && make PLATFORM=zero40 payload` -> `out/zero40/`, zip |
| MagicX Zero 28 (oakMOSS) | as the Zero 40 | as the Zero 40 (the oakMOSS tools in `build/zero40/` serve both); diatom `make PORT=zero28` | `make PLATFORM=zero28 && make PLATFORM=zero28 payload` -> `out/zero28/`, zip |
| RG Nano | `mk/fetch-nano-sdk.sh` | none | `make nano-zip` (update zip), `make nano-kernel && make nano-image` (card image) |

Notes:

- **H700:** BaseOS has no SDL2 or FFmpeg, so the sysroot builds both and the
  payload ships them in `plorpOS/lib/`. Its FFmpeg is `--disable-everything`
  plus a short filter list: a filter Muse+ starts using must be added there, or
  every track fails to open on the H700 alone.
- **RG DS:** `mk/rgds-image.sh` needs no device or root; `W=` sets its work
  directory (about 6 GB, not a tmpfs).
- **Zero 40 / Zero 28:** this repo builds the zips. The card images are built
  from the oakMOSS fork with the zip inside (`docs/plorpos-image.md` on
  [thwonp/oakMOSS](https://github.com/thwonp/oakMOSS), branch `plorpos-image`).
- **Rotate:** `mk/rotate-image.sh` builds from the saved base in `work/`;
  `READ_DEVICE=1` refreshes it from a Rotate over ssh.
- **Flip:** cores are built from pinned source, see
  [mk/flip-cores/README.md](../mk/flip-cores/README.md). `package` writes
  `plorpOS-v<VER>-miyoominiflip.zip` to `OUT` and also needs `FLIP_CURL`
  (`mk/flip-cores/build-curl.sh`) and `FLIP_INPUTS` (the pinned Wi-Fi driver);
  the defaults point outside the repo.
- **Nano:** a different stack (nanoshelf + PicoArch on FunKey-OS); see
  [install-nano.md](install-nano.md#sources-and-licences).
- **Box art:** a release needs the ScreenScraper developer pair in
  `.screenscraper.env` (gitignored). Without it `make payload` refuses;
  `ALLOW_NO_SS=1` packages anyway.
- `make install-card CARD=<mount>` copies the Brick payload to a mounted card,
  adding files only.

## Releases

The update check on the devices (Network Services > Check for update) finds
releases by these rules, so every release follows them:

- Tag `plorpos-v<VER>`, `<VER>` being the Makefile's `VERSION` the release was
  built with (`2.0`, `2.0.1`). A release candidate is marked **pre-release**
  on GitHub; the devices skip pre-releases and drafts.
- One zip per port, named `plorpOS-v<VER>-<port>.zip`, exactly as the
  packagers above write it: `brick`, `gkd350based` (from `PLATFORM=gkdb`), `h700`, `rgds`, `zero40`, `zero28`,
  `rotate-update` and `miyoominiflip`. No `gkd` zip in 2.0: the ROCKNIX GKD build is
  development only (`make PLATFORM=gkd payload` writes none), and 1.9.x GKD
  cards have no update check. A device takes the newest release that
  has its own port's zip, so a release for some ports only is fine.
- A `SHA256SUMS` beside them, covering every asset:
  `cd out && sha256sum plorpOS-v<VER>-*.zip > SHA256SUMS`.
- A port that needs a new card image for this release (a new kernel, say)
  gets no zip in it, so no device updates to it over Wi-Fi.
- Other downloads keep names outside that pattern: the card images
  `plorpOS-v<VER>-<port>.img.xz` (`rotate`, `rgds`, `zero40`, `zero28`) and
  `plorpOS-miyoominiflip-fast-boot.zip`.
- The fast-boot zip: `mk/flip-fw/build-lean.sh <stock miyoo285_fw.img> <lean.img>`,
  then `mk/flip-fw/package-fast-boot.sh <lean.img>` (writes it to `out/`).

## Checks

```sh
make check          # every check, offline, needs no device
make check-menus    # any one alone
```

The list is `CHECKS` in the Makefile. Each exists because something once broke
without anyone noticing.

| Area | Checks |
|---|---|
| Menus and screens | `check-menus` (what each menu contains in a given state, no renderer), `check-controls`, `check-hkbind`, `check-btnmap`, `check-shaderlist`, `check-gamelist`, `check-sort`, `check-sysorder`, `check-titles`, `check-paused` |
| Settings and device state | `check-db`, `check-stats`, `check-clock`, `check-idle`, `check-battery`, `check-audioout`, `check-gbpal` |
| Network and file server | `check-hare`, `check-httpd`, `check-xfer`, `check-bt`, `check-ota` |
| Achievements | `check-cheevos`, `check-rahash`, `check-raset` (C and Python agree; `raset` needs `RA_USER`/`RA_PASS`, skips without) |
| Box art | `check-artscrape`, `check-artrun`, `check-ss` |
| Muse+ | `check-muselib`, `check-musequeue`, `check-museart`, `check-playlist`, `check-podfeed`, `check-jellyfin` |
| Other | `check-logpack`, `check-backlog`, `check-emptywrap` |

`check-paused` skips without host SDL2/SDL_ttf headers; `check-titles` needs
sqlite3.

## Host build

`make native` builds the launcher for the host (`build-native/tortos`). It
renders what the handheld renders and can write a single frame:

```sh
TORTOS_ROOT=… TORTOS_ROMS=… TORTOS_FONT=res/fonts/menu.ttf \
  build-native/tortos --shot /tmp/shelf.png --screen games
```

- `--menu [row]` draws the MENU over that shelf.
- `--slots <n> [aspect]` draws the save/load carousel over synthetic frames.
- `--jump <n>` applies n letter-jumps first (negative for up).

Each shot prints the screen and focused item on stderr.

## On device

| Device | Script | Notes |
|---|---|---|
| Brick | `make adb`, `adb-elf`, `adb-res`, `adb-vendor`, `adb-restart`, `adb-restart-all`, `adb-run`, `adb-log` (`mk/adb-deploy.sh`); or `make deploy` / `restart` / `logs` over ssh (`BRICK=<ip>`) | Brick only. Never kill `launch.sh`: the boot hook's failsafe powers the device off when the launch loop exits |
| GKD (ROCKNIX) | `mk/gkd-deploy.sh elf\|res\|vendor\|diatom\|boot`; `make PLATFORM=gkd deploy` / `logs` | ssh, `GKD=root@<ip>` |
| RG DS | `mk/rgds-deploy.sh elf\|res\|vendor\|diatom\|boot` | ssh, `RGDS=root@<ip>`, `SSH_OPTS` |
| H700 | `mk/h700-deploy.sh elf\|diatom\|muse\|all\|ds` | adb; cable attached at power-on |
| Rotate | `mk/rotate-deploy.sh elf\|diatom\|all\|boot\|stage` | ssh, `ROTATE_HOST`, `ROTATE_KEY` |
| Flip | `mk/miyoo-deploy.sh all\|elf\|diatom\|cores\|hook` | ssh over Wi-Fi only (USB is charge-only), `MIYOO=root@<ip>` |
| Zero 40, Zero 28 | `adb push` to `/mnt/SDCARD/plorpOS/` (`.new`, then `mv`), then kill `tortos.elf` (launch.sh restarts it) | adb over USB (oakMOSS's gadget, root); ssh once Wi-Fi is on |

## Layout

```
src/            the launcher; src/platform_<device>.c per port, src/muse/ the audio player
tools/          checks, card/boot generators, musectl, btplayer, setbright, PICO-8 shims
config/         systems.cfg (every port) and systems-rotate.cfg (DS, Ports rows)
sd/             per-device boot and session scripts: tortos/ (Brick launch.sh), brick/,
                trimui/, gkd/, gkdb/, rgds/, h700/, rotate/, miyoo/, oakmoss/ (Zero 28/40)
mk/             cross build, core builds, payload, deploy and image scripts;
                flip-cores/, flip-fw/, patches/, pgo/
res/            boot pictures, system cards, fonts, shaders, CA bundle, Over The
                Hare's page, Nano boot logo
third_party/    vendored libchdr and rcheevos
docs/           guide, install pages, how it works, building, decisions/
fetched/built (gitignored):
sysroot*/       per-device SDL2/FFmpeg to link against; sdk-nano/ for the Nano
vendor/         cores (cores/, cores-brick/, cores-gkd/, cores-rotate/), portmaster/, adbd-rgds/
build*/, out/   build outputs and payloads
```
