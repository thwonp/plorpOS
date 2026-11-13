#!/bin/sh
# Fetch armGBA, the Game Boy Advance shelf's core on the Brick, H700, GKD and Rotate (plorpos-9i7): the
# upstream release build, MagnominusGit/armGBA v1.6 (de50d047, 2026-11-13: Fast-Forward Skip, Video Threads).
# Its aarch64 core needs glibc 2.29, under the Brick's 2.33, so no local build. mGBA stays a selectable Core.
# Both hashes are pinned: the zip as the release's SHA256SUMS lists it, and the core inside it.
# Installed to vendor/cores-brick, which every aarch64 payload ships from. GPL-2.0.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
URL=https://github.com/MagnominusGit/armGBA/releases/download/v1.6/armGBA-1.6-aarch64.zip
ZIP_SHA=8cf2067309d16f195222057129454ae29ffe90d3fe3c1f376d1deac7fd188909
SO_SHA=ffcc94ddd6c6a4c4a0637696fcfc412009309ccd0584d69fea0c6f0685ad1421
OUT=$ROOT/vendor/cores-brick/armgba_libretro.so

if [ -f "$OUT" ] && [ "$(sha256sum "$OUT" | cut -d' ' -f1)" = "$SO_SHA" ]; then echo "  ok      armgba"; exit 0; fi
DL=$(mktemp -d); trap 'rm -rf "$DL"' EXIT
curl -sSfL -o "$DL/a.zip" "$URL"
[ "$(sha256sum "$DL/a.zip" | cut -d' ' -f1)" = "$ZIP_SHA" ] || { echo "!! armGBA zip sha256 mismatch" >&2; exit 1; }
unzip -q -o "$DL/a.zip" armgba_libretro.so -d "$DL"
[ "$(sha256sum "$DL/armgba_libretro.so" | cut -d' ' -f1)" = "$SO_SHA" ] || { echo "!! armgba_libretro.so sha256 mismatch" >&2; exit 1; }
mkdir -p "$ROOT/vendor/cores-brick"
cp "$DL/armgba_libretro.so" "$OUT"
echo "  fetched armgba v1.6"
