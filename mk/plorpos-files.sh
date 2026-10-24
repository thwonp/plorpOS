# The plorpOS folder's device-independent files, for every device's card:
# sourced by mk/payload.sh (Brick, GKD, H700) and mk/rotate-deploy.sh (the
# Rotate). One list, so a card can no longer miss what another one ships - the
# Rotate's own copy of it went without the arcade titles, the licences and two
# ROM folders (plorpos-3yv.89). Needs ROOT; the caller writes P/systems.cfg first.
#
# SPDX-License-Identifier: MIT

# plorpos_files P COREDIRS [EXTRA_CORES]
#   P            the plorpOS folder being built, its systems.cfg already in it
#   COREDIRS     folders to take cores from, the first that has one wins
#   EXTRA_CORES  cores no shelf names but the launcher can switch to (ALT_CORES)
plorpos_files() {
	_p=$1 _dirs=$2 _extra=${3:-}
	mkdir -p "$_p/cards" "$_p/cores" "$_p/res/web" "$_p/shaders"
	# diatom's GLSL passes and the in-game menu's list of them, one list on
	# every device (plorpos-gkd.72, plorpos-reo.4)
	cp "$ROOT/res/shaders/"* "$_p/shaders/"
	cp -R "$ROOT/res/cards/." "$_p/cards/"     # the classic/ and fancy/ sets, as adb-deploy.sh pushes them
	rm -rf "$_p/cards/src"                    # art sources for tools/genportscard.py, not the card's
	cp "$ROOT/res/fonts/head.ttf" "$_p/"       # the heading face (ui.c, plorpos-8m1.2)
	cp "$ROOT/res/fonts/josefin.ttf" "$ROOT/res/fonts/lato.ttf" "$_p/"   # UI Settings > Font (plorpos-8m1.3)
	cp "$ROOT/res/fonts/menu.ttf" "$_p/"       # the UI face, and the in-game menu's
	cp "$ROOT/res/singles.png" "$_p/"          # Muse's cover for Singles, the loose songs
	# Over The Hare's page. The launcher serves these off the card at P_WEB, so a
	# payload without them is a card whose transfer screen starts a server and then
	# answers its own page with a 404. adb-deploy.sh has always pushed them and
	# this did not, which is a difference that only shows on a built card - the
	# reason to build one before calling it a release. The font goes in twice
	# rather than being kept in the repo twice: the launcher reads $P/menu.ttf and
	# a browser asks for /web/menu.ttf.
	cp "$ROOT/res/web/"* "$_p/res/web/"
	cp "$ROOT/res/fonts/menu.ttf" "$_p/res/web/menu.ttf"
	# The device has curl and OpenSSL but nothing to trust - see res/ssl/README.md.
	# Without this, every HTTPS request fails verification and achievements never
	# arrive, with an error that reads like the network being down.
	cp "$ROOT/res/ssl/cacert.pem" "$_p/"
	cp "$ROOT/LICENSE" "$ROOT/NOTICE" "$_p/"  # MIT, and which files are not (NOTICE)
	cp -r "$ROOT/LICENSES" "$_p/"            # PolyForm Noncommercial, for the NextUI-derived parts
	cp "$ROOT/THIRD-PARTY-LICENSES.md" "$_p/"  # notices for the redistributed software
	# What a core needs on the card besides itself, only when it ships.
	if grep -q '^sys|[^|]*|[^|]*|fbneo|' "$_p/systems.cfg"; then
		cp "$ROOT/res/fbneo-titles.tsv" "$_p/res/"   # Arcade and Neo Geo titles (src/titles.c)
		cp "$ROOT/LICENSE-FBNeo.txt" "$_p/"     # FBNeo requires its full text, verbatim
	fi
	if grep -q '^sys|[^|]*|[^|]*|fake08|' "$_p/systems.cfg"; then
		cp "$ROOT/LICENSE-fake08.md" "$_p/"     # fake08's MIT notice and its components' terms
	fi

	# The cores are the ones the card's systems.cfg names - no more, no fewer.
	#
	# This used to be a glob over vendor/cores, which ships whatever that folder
	# happens to hold and says nothing about what is missing. That is how a card was
	# nearly built with four of nine systems dead - systems.cfg gained SNES and the
	# Sega machines, vendor/ still held the original three, and nothing failed. A
	# shelf whose cards open onto nothing is a worse failure than a build that
	# refuses. "portmaster" is no core: the Ports shelf runs scripts (main.c is_ports).
	_missing=
	for _core in $(awk -F'|' '$1=="sys"{gsub(/^[ \t]+|[ \t]+$/,"",$4); print $4}' \
	               "$_p/systems.cfg" | sort -u) $_extra; do
		[ "$_core" = portmaster ] && continue
		_found=
		for _d in $_dirs; do
			[ -f "$_d/${_core}_libretro.so" ] || continue
			cp "$_d/${_core}_libretro.so" "$_p/cores/"
			_found=1
			break
		done
		[ -n "$_found" ] || _missing="$_missing $_core"
	done
	if [ -n "$_missing" ]; then
		echo "payload: systems.cfg needs cores that vendor/ does not have:$_missing" >&2
		echo "payload: run mk/fetch-vendor.sh" >&2
		exit 1
	fi
}

# plorpos_rom_folders P: the ROM folder of every system in P/systems.cfg, one a
# line, each to be made with the .media folder box art goes in. awk, not sed:
# folder names contain spaces.
plorpos_rom_folders() {
	awk -F'|' '$1=="sys"{gsub(/^[ \t]+|[ \t]+$/,"",$3); print $3}' "$1/systems.cfg"
}
