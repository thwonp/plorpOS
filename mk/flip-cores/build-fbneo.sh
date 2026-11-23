#!/bin/sh
# FBNeo for the Miyoo Mini Flip from finalburnneo/FBNeo @ a836e52 (pin: see
# cores.lock) with patches/fbneo-lazy-cpu-maps.patch: two CPU cores' static
# tables (40 MB of BSS in every build) are allocated when a game uses them, or
# a 128 MB device cannot load the core beside the launcher (plorpos-f1p.95).
#
#   build-fbneo.sh            every driver          -> fbneo_libretro.so
#   build-fbneo.sh cps12      CPS-1 and CPS-2 only  -> fbneo_cps12_libretro.so
#   build-fbneo.sh neogeo     Neo Geo only          -> fbneo_neogeo_libretro.so
#
# The family cores load in a fraction of the time (plorpos-f1p.95.13): the
# full core is 60 MB to map. -O2: the same speed as -O3 here, and smaller.
SUB=$1
CORE=fbneo
. "$(dirname "$0")/common.sh"
XFLAGS="$ARCH -O2"
fetch fbneo finalburnneo/FBNeo a836e52198aa5ce1f54be7b050e44872a1223797 a04fdb75c2f46998a3df82a20b66ca2b7488f7f484699dbf4b0851b5f28542d9
(cd "$R" && git apply --directory=build/flip-cores/src/fbneo mk/flip-cores/patches/fbneo-lazy-cpu-maps.patch)
D=fbneo/src/burner/libretro
A="platform=rpi2 $XCW"
if [ -n "$SUB" ]; then
	A="$A SUBSET=$SUB"
	# The driver list is generated per subset. The last step of generate-files
	# builds a host tool for a header the tarball already has; no host g++ in
	# the image, so that step's failure is expected.
	dk "src/$D" "make $A generate-files" > "$W/logs/fbneo-$SUB-generate.log" 2>&1 || true
	grep -q . "$W/src/$D/../../dep/generated/driverlist.h"
fi
mk "fbneo${SUB:+-$SUB}" "$D" $A
check_so "src/$D/fbneo${SUB:+_$SUB}_libretro.so" "fbneo${SUB:+_$SUB}_libretro.so"
