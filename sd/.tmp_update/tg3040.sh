#!/bin/sh
# TortOS tg3040 boot: hand off to the launch loop on the SD card.

export LD_LIBRARY_PATH=/usr/trimui/lib:$LD_LIBRARY_PATH
export PATH=/usr/trimui/bin:$PATH

# plorpOS/, or TortOS/ on a card the rename has not reached yet (plorpos-nzi):
# the first launch from plorpOS/ moves what is left in TortOS/ and deletes it.
LAUNCH=/mnt/SDCARD/plorpOS/launch.sh
[ -f "$LAUNCH" ] || LAUNCH=/mnt/SDCARD/TortOS/launch.sh
if [ -f "$LAUNCH" ]; then
	sh "$LAUNCH"
fi
