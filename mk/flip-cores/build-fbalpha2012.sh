#!/bin/sh
# fbalpha2012 for the Miyoo Mini Flip from libretro/fbalpha2012 @ e0c7990dd24f4c60324843b095566d0ac0d1a015 (pin: see cores.lock).
. "$(dirname "$0")/common.sh"
fetch fbalpha2012 libretro/fbalpha2012 0ce31536bef3162fe7e69ff5f555334ec4913cef 6825b86c65887fc92ba02c07059d8225113bcca7764dab84ce21da18954c33af
mk fbalpha2012 fbalpha2012/svn-current/trunk -f makefile.libretro $XCW platform=unix-armv7-hardfloat-neon
check_so src/fbalpha2012/svn-current/trunk/fbalpha2012_libretro.so fbalpha2012_libretro.so
