#!/bin/sh
# Shipped as TortOS/launch.sh on the GKD only (plorpos-nzi). plorpOS moved to
# plorpOS/, but a GKD updated by copying the zip over its card keeps the
# system.d drop-ins it was installed with - the guide says to leave them - and
# those start TortOS/launch.sh. This passes the boot on. plorpOS/launch.sh's
# migration keeps this file when it deletes the rest of TortOS/.
exec /bin/sh /storage/games-external/plorpOS/launch.sh
