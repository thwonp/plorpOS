#!/bin/sh
# Push a build to the Anbernic RG DS over ssh (ROCKNIX). The RG DS's counterpart
# of mk/gkd-deploy.sh, with the same layout under <card>/plorpOS.
#
# The release image mounts an exFAT partition at /storage/games-external
# (plorpos-rgds.6); on a stock ROCKNIX card used for development it is a plain
# directory on /storage, which this creates.
#
# Usage: mk/rgds-deploy.sh [elf|res|vendor|diatom|boot]
#        RGDS=root@<ip> SSH_OPTS="-i <key>" to reach the device
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WHAT=${1:-elf}
RGDS=${RGDS:-root@192.168.0.74}
SSH_OPTS=${SSH_OPTS:--i $HOME/.ssh/rgds}
CARD=/storage/games-external
P=$CARD/plorpOS

S="ssh -n $SSH_OPTS -o ConnectTimeout=5 $RGDS"
$S true 2> /dev/null || { echo "no RG DS over ssh at $RGDS"; exit 1; }
$S "mkdir -p $P/cards $P/cores $P/res/web $P/shaders $CARD/Roms"

put() { scp $SSH_OPTS -q -r "$@"; }
# One binary, to $P/<name>, around whatever is running it.
swap() { put "$1" "$RGDS:$P/$2.new" && $S "mv -f $P/$2.new $P/$2"; }

case $WHAT in elf)
	[ -f "$ROOT/build/rgds/tortos.elf" ] || { echo "run make PLATFORM=rgds first"; exit 1; }
	swap "$ROOT/build/rgds/tortos.elf" tortos.elf
	cat "$ROOT/config/systems.cfg" "$ROOT/config/systems-rotate.cfg" | ssh $SSH_OPTS "$RGDS" "cat > $P/systems.cfg"
	swap "$ROOT/build/rgds/muse" muse
	swap "$ROOT/build/rgds/musectl" musectl
	echo "  + launcher, muse, musectl"
esac
case $WHAT in res)
	put "$ROOT/res/cards/"* "$RGDS:$P/cards/"
	put "$ROOT/res/fonts/menu.ttf" "$ROOT/res/fonts/head.ttf" "$ROOT/res/fonts/josefin.ttf" "$ROOT/res/fonts/lato.ttf" "$ROOT/res/ssl/cacert.pem" "$ROOT/res/singles.png" "$RGDS:$P/"
	put "$ROOT/res/web/"* "$RGDS:$P/res/web/"
	put "$ROOT/res/fbneo-titles.tsv" "$RGDS:$P/res/"
	put "$ROOT/res/fonts/menu.ttf" "$RGDS:$P/res/web/menu.ttf"
	put "$ROOT/res/shaders/"* "$RGDS:$P/shaders/"
	echo "  + assets"
esac
case $WHAT in diatom)
	D=${DIATOM_ELF:-$ROOT/../diatom-rgds/build/rgds/diatom}
	[ -f "$D" ] || { echo "no diatom at $D (set DIATOM_ELF)"; exit 1; }
	swap "$D" diatom
	echo "  + diatom"
esac
case $WHAT in boot)
	put "$ROOT/sd/rgds/launch.sh" "$RGDS:$P/"
	$S "mkdir -p /storage/.config/system.d"
	put "$ROOT/sd/rgds/system.d/"* "$RGDS:/storage/.config/system.d/"
	$S "systemctl daemon-reload"
	echo "  + boot (from the next boot)"
esac
case $WHAT in vendor)
	# The aarch64 cores every ROCKNIX port takes, as mk/gkd-deploy.sh. VENDOR:
	# a worktree has no vendor/ of its own (mk/fetch-vendor.sh).
	V=${VENDOR:-$ROOT/vendor}
	put "$V/cores/"* "$V/cores-gkd/"* "$RGDS:$P/cores/"
	put "$V/cores-brick/gambatte_libretro.so" "$V/cores-brick/armgba_libretro.so" \
	    "$V/cores-brick/pokedaisy_gba_libretro.so" "$V/cores-brick/pokedaisy_gbc_libretro.so" \
	    "$V/cores-brick/pokedaisy_ds_libretro.so" "$RGDS:$P/cores/"
	echo "  + cores"
esac
$S sync
echo "deployed '$WHAT' to $RGDS"
