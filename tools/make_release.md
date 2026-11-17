# Making a Geist Recompiled release

1. Windows runtime build: `cmake -S . -B build/win -DCMAKE_TOOLCHAIN_FILE=<mingw toolchain> ...` then `ninja -C build/win geist.exe`
2. SDK (no game code): `tools/make_sdk.sh build/win dist/GeistRecompiled/sdk <llvm-mingw cross bin>` (fails if game strings leak)
3. Launcher: `launcher/build_launcher.sh <llvm-mingw cross bin> <SDL2 mingw x86_64> dist/GeistRecompiled/GeistLauncher.exe`
4. Toolchain: Windows llvm-mingw 20250924 ucrt x86_64, trimmed (no ARM/i686 sysroots, lldb, clangd, clang-tidy, Linux runtimes, Python tests)
5. assets/ (fonts), licenses/, README.txt
6. Before zipping, make sure `work/` and `game/` are NOT in dist (they are built from a disc), and run the leak scan.
