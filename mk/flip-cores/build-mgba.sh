#!/bin/sh
# mgba (Game Boy, Game Boy Color) for the Miyoo Mini Flip from libretro/mgba (pin: see
# cores.lock). Its HEAD builds the libretro core through CMake (BUILD_LIBRETRO), with
# every optional dependency off; flags from the CC wrapper (-Ofast, Cortex-A7 NEON).
. "$(dirname "$0")/common.sh"
fetch mgba libretro/mgba 7a12d6d4b9acb14c0ae62c9166b6a2f3d08007f6 5cbf639e527fb586bf33e14d59037eab35f78f45154e0b79c784fff474c3bc37
OFF="BUILD_QT BUILD_SDL BUILD_SHARED BUILD_STATIC BUILD_GL BUILD_GLES2 BUILD_GLES3 BUILD_PERF BUILD_TEST BUILD_SUITE BUILD_CINEMA BUILD_EXAMPLE BUILD_UPDATER BUILD_PYTHON BUILD_DOCGEN BUILD_ROM_TEST BUILD_HEADLESS USE_DISCORD_RPC USE_EDITLINE USE_ELF USE_EPOXY USE_FFMPEG USE_GDB_STUB USE_LIBZIP USE_LUA USE_LZMA USE_MINIZIP USE_PNG USE_SQLITE3 USE_ZLIB USE_JSON_C ENABLE_SCRIPTING ENABLE_DEBUGGERS ENABLE_GDB_STUB"
D=""; for o in $OFF; do D="$D -D$o=OFF"; done
dk src/mgba "rm -rf b && mkdir b && cd b && cmake .. -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=arm -DCMAKE_C_COMPILER=/w/bin/arm-linux-gnueabihf-gcc -DCMAKE_CXX_COMPILER=/w/bin/arm-linux-gnueabihf-g++ -DCMAKE_BUILD_TYPE=Release -DBUILD_LIBRETRO=ON -DSKIP_LIBRARY=ON $D && make -j4 mgba_libretro" > "$W/logs/mgba.log" 2>&1 ||
	{ tail -20 "$W/logs/mgba.log"; echo "RESULT mgba FAILED"; exit 1; }
check_so src/mgba/b/mgba_libretro.so mgba_libretro.so
