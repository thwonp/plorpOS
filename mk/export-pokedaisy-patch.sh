#!/bin/sh
# Export the PokeDaisy clone's working changes to mk/patches/pokedaisy-plorpos.patch - the one way to do it:
# new files are marked (git add -N) first, or the diff leaves them out and mk/build-pokedaisy.sh's clean
# checkout deletes them.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=${POKEDAISY_SRC:-$HOME/.cache/plorpos-pokedaisy}
PATHS="linux 3ds gamedata scripts/gen_gamedata.py scripts/port_records.py"
cd "$SRC"
git ls-files --others --exclude-standard -- $PATHS | xargs -r git add -N --
git diff -- $PATHS > "$ROOT/mk/patches/pokedaisy-plorpos.patch"
echo "export: $(grep -c '^diff --git' "$ROOT/mk/patches/pokedaisy-plorpos.patch") files -> mk/patches/pokedaisy-plorpos.patch"
