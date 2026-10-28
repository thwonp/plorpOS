#!/bin/sh
# Python and Bash for PortMaster on stock TG3040 firmware (plorpos-reo.17).
# The firmware supplies SDL and its GPU libraries; do not replace those.
# SPDX-License-Identifier: MIT
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$ROOT/build
DL=$OUT/pmtools-brick-dl
mkdir -p "$DL"
fetch() {
    url=$1 sha=$2 name=$3
    # Share the already verified H700 downloads when available.
    if [ ! -f "$DL/$name" ] && [ -f "$OUT/h700/pmtools-dl/$name" ]; then
        cp "$OUT/h700/pmtools-dl/$name" "$DL/$name"
    fi
    if [ "$(sha256sum "$DL/$name" 2>/dev/null | cut -d' ' -f1)" != "$sha" ]; then
        curl -fsSL -o "$DL/$name" "$url"
    fi
    [ "$(sha256sum "$DL/$name" | cut -d' ' -f1)" = "$sha" ] || exit 1
}
fetch 'https://github.com/astral-sh/python-build-standalone/releases/download/20261026/cpython-3.12.16%2B20261026-aarch64-unknown-linux-gnu-install_only_stripped.tar.gz' \
    bb4b6392670c5d3f93876996fcaa19858a4a413a0c7720144c18b0d6f1f0db2d python.tar.gz
fetch 'https://github.com/robxu9/bash-static/releases/download/5.2.015-1.2.3-2/bash-linux-aarch64' \
    8877ad33344af461ed801066322fd9a7808cd73e4e81087da228e32e8fad54ca bash
rm -rf "$DL/tree"
mkdir -p "$DL/tree/bin" "$DL/tree/lib"
tar xzf "$DL/python.tar.gz" -C "$DL/tree"
cp "$DL/bash" "$DL/tree/bin/bash"
chmod +x "$DL/tree/bin/bash"
# pysdl2 and SoLoud search these unversioned names without ldconfig.
for name in SDL2 SDL2_image SDL2_ttf SDL2_mixer; do
    ln -s "/usr/trimui/lib/lib$name-2.0.so.0" "$DL/tree/lib/lib$name.so"
done
mksquashfs "$DL/tree" "$OUT/pmtools.squashfs" -comp xz -all-root -noappend -quiet
echo "$OUT/pmtools.squashfs"
