# Flip cores built from the RG Nano port's sources

Two cores on the Miyoo Mini Flip are rebuilt from the sources the RG Nano port
pins (`picoarch/`: `mame2000` 181b42e, `pcsx_rearmed` c8816799) with the
Flip's glibc toolchain (`localhost/miyoo-toolchain`), because the Nano's own
builds are musl and cannot load here (plorpos-f1p.26, .27).

1. Copy the two source trees next to the scripts: `cp -r picoarch/mame2000
   picoarch/pcsx_rearmed .` (from a checkout with the Nano patches applied).
2. PlayStation only: `git -C pcsx_rearmed apply ../../patches/pcsx-rearmed-flip-tc.patch`
   (dynarec translation cache 16 MB -> 4 MB).
3. `./build-mame2000.sh`, `./build-pcsx.sh` (strip the result with
   `arm-linux-gnueabihf-strip --strip-unneeded`), then put the `.so` in the
   card's `TortOS/cores/`. `miyoo-deploy.sh package` zips what is there.

Why: Allium's `mame2000` cannot open ROMs when the content folder path is over
26 characters; `pcsx_rearmed` needs `HAVE_PHYSICAL_CDROM=0` or dlopen fails on
`dir_list_new` (the script sets it).
