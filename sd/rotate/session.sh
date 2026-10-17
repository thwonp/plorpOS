#!/bin/sh
# The Rotate's session (plorpos-3yv): plorpos.service runs this under its own
# D-Bus. PipeWire owns the codec, so Muse and a game share it the GKD's way
# and the sc2730's habit of resetting HPL/HPR Gain at every PCM open happens
# once, not at every track. sway then runs launch.sh (sway.conf).
DIR=$(cd "$(dirname "$0")/.." && pwd)
export XDG_RUNTIME_DIR=/run/plorpos
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
# /run outlives a restart of the service: an old sway's IPC socket would be
# the first swaymsg finds.
rm -f "$XDG_RUNTIME_DIR"/sway-ipc.*.sock
pipewire &
wireplumber &
pipewire-pulse &
exec sway -c "$DIR/rotate/sway.conf"
