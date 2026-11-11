# Miyoo Mini Flip cores and tools, built from pinned source

Every core the Flip ships, and its ssh server, is built here from pinned
upstream source with the Flip's glibc toolchain (`localhost/miyoo-toolchain`,
Debian buster armhf, glibc 2.28) - none is a third-party prebuilt
(plorpos-f1p.62.3). `cores.lock` lists each one's upstream, commit, tarball
sha256, flags, licence and the md5 of the stripped result; `mk/flip-verify.sh`
checks a release zip against it.

    sh mk/flip-cores/build-all.sh      # every core + dropbear, ~10 minutes

`build-<core>.sh` (helpers in `common.sh`) fetch GitHub archive tarballs of the
pinned commits once into
`~/projects/misc/coding-team/plorpos-miyoo-mini-flip/dist-inputs/src/` (override
with `DL`), check their sha256, unpack under `build/flip-cores/src/`, apply our
patches (`patches/`, `../patches/`) and write stripped results to
`build/flip-cores/out/` (`out/bin/dropbearmulti` for ssh). `mk/miyoo-deploy.sh
package` and `cores` take them from there. Each script fails if the `.so` has
undefined unversioned symbols or needs glibc newer than 2.28. Builds are
reproducible (same md5 on a clean rerun).

Every core is upstream HEAD (re-pin with a newer commit, never an older one), and
each was gated against the community prebuilt it replaced: per-frame cost with
`lrbench` on the Flip and the launcher sweep (plorpos-f1p.62.3). Flags: by default
a CC wrapper appends `-marm -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard
-Ofast -mtune=cortex-a7` after each Makefile's own flags. Where HEAD was slower,
the script says what closes the gap: LTO plus profile-guided optimisation
(fceumm, snes9x2010, fake08), as the RG Nano port's picoarch does.

PGO: `PGO=gen sh mk/flip-cores/build-<core>.sh` makes an instrumented core,
`mk/flip-cores/pgo-train.sh <core> FRAMES ROM...` runs it on the Flip with
`lrbench` (launcher stopped, 120 s cap per ROM) and pulls the `.gcda` files into
`pgo/<core>/` (the exact command is `pgo/<core>/TRAIN`). Builds use them whenever
that folder exists; retrain after changing the core's pin, patches or flags.
picodrive's Cyclone.s generator runs on the host (`g++`).

Notes:
- `mame2000`: the RG Nano port's changes (`patches/mame2000-nano.patch`). The
  community prebuilt cannot open ROMs when the content folder path is over 26
  characters.
- `pcsx_rearmed`: `../patches/pcsx-rearmed-flip-tc.patch` (dynarec translation
  cache 16 MB -> 4 MB) and `HAVE_PHYSICAL_CDROM=0`, or dlopen fails on
  `dir_list_new`.
- `dropbear`: `patches/dropbear-keydir.patch` (with `-D`, the key folder ends
  the permission walk: the Flip's `/` is group-writable on a read-only rootfs).
- `snes9x2010`: `patches/snes9x2010-flip-buffers.patch` (-DPLORPOS_FLIP): HEAD sized
  every screen buffer for 1024-px 4x Mode 7 and HD packs; the Flip keeps the NTSC
  width, runs 4x Mode 7 as 2x and loads no HD packs.
- `fake08`: plorpOS's save state / rewind fixes (`../patches/fake08-states.patch`,
  `fake08-bound-lua.patch`, never merged upstream) and `fake08-load.patch`.
- `fceumm`: `HAVE_HDPACK=0`, as on the RG Nano.
- `race` (both Neo Geo Pockets) ships the DrZ80 (ARM asm Z80) build.
- Patches are applied with `git apply --directory=...` from the repo root: a
  plain `git apply` inside this repo's `build/` silently skips every file.
- `build-curl.sh` (static curl + Mbed TLS, `TortOS/curl`) is separate.
