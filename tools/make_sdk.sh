#!/bin/sh
# Assembles the public Geist Recompiled SDK: everything the launcher needs to build Geist.exe
# on a player's PC from their own disc, and NOTHING derived from the game's code or data.
#
#   tools/make_sdk.sh <windows build dir> <out sdk dir> <llvm-mingw cross bin dir>
#
# The runtime objects come from the Windows build (build/win). main.cpp is compiled again
# without the developer's disc path baked in. The generated game code (game_recomp) is not
# used: players' launchers generate and compile it themselves.
set -eu
B=$1; OUT=$2; T=$3
ROOT=$(cd "$(dirname "$0")/.." && pwd)
RT=$ROOT/gcn-recomp/runtime
rm -rf "$OUT"; mkdir -p "$OUT/recomp" "$OUT/tables" "$OUT/include" "$OUT/lib" "$OUT/licenses"

# Recompiler (gcn-recomp, MIT, with our patches applied) and Geist's steering tables.
cp "$ROOT/gcn-recomp/recomp/recomp.py" "$ROOT/gcn-recomp/recomp/ppc.py" "$ROOT/gcn-recomp/recomp/dol.py" "$OUT/recomp/"
cp "$ROOT/recomp/"*.txt "$OUT/tables/"
cp "$ROOT/analysis/symbols.txt" "$OUT/symbols.txt"
cp "$RT/recomp.h" "$RT/compat.h" "$OUT/include/"
cp "$ROOT/game/controls.txt" "$OUT/controls.txt"
cp "$ROOT/gcn-recomp/LICENSE" "$OUT/licenses/gcn-recomp-MIT.txt"
cp "$ROOT/gcn-recomp/runtime/third_party/Rajdhani-OFL.txt" "$OUT/licenses/Rajdhani-OFL.txt"  # the settings menu font

# Runtime: every non-game object of the Windows build, main.cpp rebuilt without a disc path.
OBJS=$(find "$B/CMakeFiles/gcn_core.dir" "$B/CMakeFiles/geist.dir" -name '*.obj' ! -name 'main.cpp.obj' ! -path '*/geist.dir/src/*')
# The game project's own sources (src/) register hooks from static initialisers that
# nothing references, so they are linked as objects, never pulled from the archive.
GAME_OBJS=""
for o in $(find "$B/CMakeFiles/geist.dir/src" -name '*.obj' | sort); do
  base=$(basename "$o" .cpp.obj)
  cp "$o" "$OUT/lib/$base.o"
  GAME_OBJS="$GAME_OBJS lib/$base.o"
done
CMD=$(cd "$B" && ninja -t commands geist.exe | grep ' -c .*runtime/main.cpp' | head -1)
CMD=$(echo "$CMD" | sed -e 's#-DGCN_DEFAULT_ISO=\\"[^"]*\\"#-DGCN_DEFAULT_ISO=\\"\\"#' \
                        -e 's#-DGCN_APP_LABEL=\\"[^"]*\\"#-DGCN_APP_LABEL=\\"Geist\\ Recompiled\\"#' \
                        -e "s# -o [^ ]*main.cpp.obj# -o $OUT/lib/main.o#" -e 's# -MD -MT [^ ]* -MF [^ ]*##')
(cd "$B" && eval "$CMD")
"$T/llvm-ar" rcs "$OUT/lib/libgeist_runtime.a" $OBJS "$OUT/lib/main.o"
rm "$OUT/lib/main.o"
cp "$(grep -o '[^ ]*libSDL2\.a' "$B/build.ninja" | head -1)" "$OUT/lib/libSDL2.a"
# Icon and version info for the player's Geist.exe (taskbar, title bar, Explorer).
(cd "$ROOT/sdk_res" && "$T/x86_64-w64-mingw32-windres" geist_game.rc -O coff -o "$OUT/lib/geist_icon.o")

FLAGS=$(grep -m1 -A3 'gen/recomp_000.c.obj:' "$B/build.ninja" | sed -n 's/^  FLAGS = //p' | sed 's/-O3 -DNDEBUG //')
LIBS=$(cd "$B" && ninja -t commands geist.exe | tail -1 | grep -o ' -l[A-Za-z0-9_]*' | awk '!seen[$0]++' | tr -d '\n')
SHA=$(grep -o 'DOL_SHA1 [0-9a-f]*' "$ROOT/CMakeLists.txt" | cut -d' ' -f2)
cat > "$OUT/manifest.ini" <<EOF
# Geist Recompiled SDK manifest. Read by GeistLauncher.exe.
sdk_version=1.0.7
title=Geist
disc_id=GITE01
disc_name=Geist (USA), revision 0
dol_sha1=$SHA
exe_name=Geist.exe
cc=x86_64-w64-mingw32-clang
cxx=x86_64-w64-mingw32-clang++
cflags=$FLAGS
link_inputs=lib/geist_icon.o$GAME_OBJS lib/libgeist_runtime.a lib/libSDL2.a
ldflags=$LIBS -mwindows
runtime_dlls=x86_64-w64-mingw32/bin/libc++.dll x86_64-w64-mingw32/bin/libunwind.dll
game_files=controls.txt
EOF

# Proof of cleanliness: no game code or strings in what we ship.
for needle in FearAIBot CARD_GUIDE1 POSSESS "Collection Host Unlock"; do
  if grep -rqa "$needle" "$OUT"; then echo "LEAK: '$needle' found in SDK" >&2; exit 1; fi
done
echo "SDK ok: $(du -sh "$OUT" | cut -f1), no game content found."
