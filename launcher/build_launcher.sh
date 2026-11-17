#!/bin/sh
# Builds GeistLauncher.exe (Windows x64) with llvm-mingw, SDL2 static, the icon and version info.
#   launcher/build_launcher.sh <llvm-mingw bin dir> <SDL2 mingw x86_64 dir> <out exe>
set -eu
T=$1; S=$2; OUT=$3; cd "$(dirname "$0")"
"$T/x86_64-w64-mingw32-windres" src/launcher.rc -O coff -o /tmp/launcher_res.o
"$T/x86_64-w64-mingw32-clang++" -std=c++17 -O2 -Ithird_party -I"$S/include/SDL2" -DSDL_MAIN_HANDLED \
  src/main.cpp src/ui.cpp src/builder.cpp src/proc.cpp src/gci_import.cpp /tmp/launcher_res.o "$S/lib/libSDL2.a" \
  -static -mwindows -lsetupapi -limm32 -lversion -lwinmm -lole32 -loleaut32 -luuid -lcomdlg32 -lgdi32 -luser32 -lshell32 -ladvapi32 \
  -o "$OUT"
