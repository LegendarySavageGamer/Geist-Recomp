# Releasing GeistNative publicly, without shipping the game

## Rule: never share `Geist.exe` or anything in `build/`

A static recompilation turns Geist's own program code (`main.dol`, Nintendo/n-Space
copyright) into C and then into machine code. The `Geist.exe` built so far therefore
**contains the game's code**, even though it contains none of the art, levels or audio. It
is fine for your own use. Publishing it would be distributing the game. The same applies
to `build/gen/*.c`, the dtk `asm/` output, and any `.dol`/`.iso`.

This is the same rule every recomp project follows (gcn-recomp's own README: "No game
code or data"). Players supply the game. You supply only your own work.

## What can be public

| Public (your work) | Never public (the game) |
|---|---|
| `server/` relay, Dockerfile, scripts | `Geist (USA).iso`, `sys/`, `files/` |
| `recomp/*.txt` steering tables, `patches/` | `main.dol` / `default.dol` |
| `analysis/config.yml`, `symbols.txt`, `splits.txt` (addresses and names) | `build/gen/*.c` (recompiled game code) |
| `src/`, `game/controls.txt`, `tools/`, `tests/`, docs | `build/dtk/asm/` (disassembly) |
| the gcn-recomp runtime (MIT), SDL2 (zlib) | any `Geist.exe` built from the disc |

`symbols.txt` holds only function addresses, sizes and names, no code bytes. Decompilation
projects publish the same kind of file. It is listed as public on that basis.

## How players get a working Geist.exe: build it on their own PC

The public download is a **GeistNative Setup** containing only the "public" column:

1. **Pick your disc.** The player chooses *their own* Geist (USA) image. The disc picker
   built into the runtime now does this: it checks disc ID `GITE01` and remembers the path.
2. **Verify.** Setup extracts `main.dol` from the player's image and checks its SHA-1
   (`6612f52c…f288`, rev 0). Any other dump or revision is refused with a clear message.
3. **Recompile locally.** The bundled recompiler turns *their* DOL into C using the
   public tables, and a bundled compiler (llvm-mingw clang) builds it. It is linked
   against the prebuilt runtime library, which contains no game code, to produce
   `Geist.exe` on their machine. Expect a few minutes, with a progress bar.
4. **Play.** `Geist.exe` reads the art, levels and sound from their disc image at run time,
   as it does now.

The download never contains the ISO or the game code, and you never have to hand anyone
an ISO. Every player provides their own copy, and the program refuses anything that isn't
Geist (USA) rev 0.

### Status

| Piece | State |
|---|---|
| Disc picker (asks for the player's own image, checks GITE01, remembers it) | **Done**, in `Geist.exe`, patch 0004 |
| DOL SHA-1 check | Done at build time (`extract_dol.py`); needs to move into Setup |
| Recompiler without Python installed | **To do**: freeze `recomp.py`, or ship an embeddable Python |
| Bundled compiler + prebuilt runtime `.lib` | **To do**: trim llvm-mingw to what the build needs |
| Setup UI with progress and diagnostics | **To do** |

Not legal advice: this follows the approach other recompilation projects use. Check
anything you're unsure about with someone qualified before publishing.
