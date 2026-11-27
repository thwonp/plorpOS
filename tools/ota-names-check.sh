#!/bin/sh
# Every released port's update check finds its own zip and nothing else
# (plorpos-fbay). For each port: OTA_PORT as src/ota.h gives it under that
# port's -D flags from mk/cross.mk must be the name its packager writes, and
# src/ota.c's pick() rule ("plorpOS-v" <version without '-'> "-" OTA_PORT
# ".zip") must match exactly one asset of a full release: its own.
cd "$(dirname "$0")/.." || exit 1
CC=${HOSTCC:-cc}
VER=2.0
FAIL=0
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT

# PLATFORM  zip name its packager writes  packager
PORTS='brick brick mk/payload.sh
h700 h700 mk/payload.sh
gkdb gkd350based mk/payload.sh
rgds rgds mk/payload.sh
zero40 zero40 mk/payload.sh
zero28 zero28 mk/payload.sh
rotate rotate-update mk/rotate-deploy.sh
miyoo miyoominiflip mk/miyoo-deploy.sh'

# A whole release: each port's zip plus the downloads that are not updates.
echo "$PORTS" | while read -r _ z _; do echo "plorpOS-v$VER-$z.zip"; done > "$T/assets"
for x in rotate rgds zero40 zero28; do echo "plorpOS-v$VER-$x.img.xz"; done >> "$T/assets"
printf '%s\n' plorpOS-v$VER-zero28.omupd plorpOS-miyoominiflip-fast-boot.zip \
	plorpOS-v$VER-gkd.zip SHA256SUMS >> "$T/assets"

bad() { echo "FAIL $*"; FAIL=1; ok=0; }

echo "$PORTS" > "$T/ports"
while read -r plat zip packager; do
	ok=1
	defs=$(make -s --no-print-directory PLATFORM="$plat" -f mk/cross.mk -f - print-defs 2>/dev/null <<-'EOF'
	print-defs: ; @echo $(filter -D%,$(CFLAGS))
	EOF
	)
	[ -n "$defs" ] || { bad "$plat: no -D flags from mk/cross.mk"; continue; }
	port=$(printf '#include "src/ota.h"\nOTA_PORT\n' | eval "$CC -E -P $defs -x c -" 2>/dev/null |
		tail -n 1 | tr -d '" ')
	[ "$port" = "$zip" ] || bad "$plat: src/ota.h says '$port', the release zip is '$zip'"
	{ grep -qE "PORT=$zip\$|-$zip\.zip" "$packager" ||
	  { [ "$zip" = "$plat" ] && grep -q 'PORT=\$PLATFORM$' "$packager"; }; } || bad "$plat: $packager does not write -$zip.zip"
	m=$(grep -E "^plorpOS-v[^-]+-$port\.zip\$" "$T/assets")
	[ "$m" = "plorpOS-v$VER-$zip.zip" ] || bad "$plat: its check takes [$(echo $m)] from a release"
	[ $ok = 1 ] && echo "ok   $plat -> plorpOS-v$VER-$port.zip"
done < "$T/ports"
exit $FAIL
