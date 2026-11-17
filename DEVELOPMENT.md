# GeistNative — native PC port of Geist (GameCube, n-Space / Nintendo, 2005)

Developer: ThyHeroOfTime. This project is separate from Sacred Forest, Heroes of Twilight,
LA Online, and Metroid Dread Co-op: it has its own folder, its own Docker compose project
(`geist`), its own container (`geist-online-relay`), and its own port.

**No game data is included or distributed.** You need your own Geist disc image. The
build reads the DOL from it, and the game reads its assets from it at run time. Generated
C, disassembly, and the ISO are never committed. Original game © n-Space / Nintendo.

## Approach (chosen from evidence, see `docs/dev/`)

This is a **hybrid static recompilation**, not emulation. There is no Dolphin and no
interpreter loop.

1. **decomp-toolkit (dtk)** analyses `main.dol` and writes the function layout to
   `analysis/symbols.txt`.
2. **gcn-recomp** (MIT, Ethan Nicholas) translates every PowerPC function into C and
   links it with a native runtime: CPU/OS threads, GX → OpenGL 3.3, DSP/audio, DVD,
   memory card, pad, and SDL2.
3. Geist-specific steering lives only in `recomp/` (names, HLE list, patches, idle loop).
4. Subsystems that need real PC behaviour (multiplayer, options) will be decompiled into
   readable C++ and hooked in place of the original functions.

```
GeistNative/
  CMakeLists.txt          game project (gcn_add_game)
  analysis/               dtk config.yml, symbols.txt, splits.txt
  recomp/                 names.txt, hle.txt, special_calls.txt, idle.txt, patches.txt
  patches/                our changes to the shared gcn-recomp layer (game-agnostic)
  tools/rebuild_iso.py    rebuild a (sparse) disc image from a Dolphin extraction
  server/                 geist-online-relay (Go): source, tests, Dockerfile, compose
  client/geist_net.ini    client network config (relay hostname/port)
  docs/dev/               iso_analysis.md, mmu.md
```

## Build (Windows x64)

Prerequisites: CMake ≥ 3.20, Ninja, Python 3, Visual Studio 2022 Build Tools (or clang),
and Git. Run `build.ps1` from a "x64 Native Tools" prompt. It clones gcn-recomp at the
pinned commit, applies `patches/*.patch`, configures with your ISO, and builds `geist.exe`.

## Status report (session 1, 2026-11-11)

PASS / FAIL / NOT TESTED as defined in the brief. Every result below was measured in the
Linux dev container using the headless build. Nothing has been run on Windows yet.

| # | Item | Result |
|---|---|---|
| 1 | ISO revision analysed | **PASS**: GITE01 rev 0, NTSC-U (`docs/dev/iso_analysis.md`) |
| 2 | Executable/asset structure | **PASS**: single DOL, 8,706 functions, no game RELs; MusyX audio; per-level OLC/SDF_GC/GSF |
| 3 | Port method | Hybrid static recompilation (gcn-recomp) + selective decompilation |
| 4 | Toolchain evaluated | dtk 1.8.4 + gcn-recomp (MIT): all 8,706 functions translate, 1 expected warning (debugger vector) |
| 5 | Native build works | Boots: CRT init, OS init, threads, interrupts, MMU + page faults, and 3 presented frames |
| 6 | Unsupported GameCube services | ARAM-paged VM eviction stalls after frame 3; MusyX DSP ucode HLE not yet verified (runtime HLEs only AX/JAudio) |
| 7 | Files created | see tree above; plus a gcn-recomp runtime patch (DSI resume via OSLoadContext) |
| 8 | Native executable compiles | **PASS**: Linux headless `geist_bench`. Windows `geist.exe`: **NOT TESTED** |
| 9 | Graphics displayed | **FAIL** (so far): 3 frames / 6 draws reach the GX front end headless; no window yet |
| 10 | Original audio | **NOT TESTED** |
| 11 | Input | **NOT TESTED** (gcn-recomp maps keyboard/gamepad to the GC pad) |
| 12 | Single player begins | **FAIL**: stalls in the VM pager before the menu |
| 13 | MP maps load | **NOT TESTED** |
| 14 | PC Options | **NOT TESTED** (not started) |
| 15 | Networking features | **PASS** at relay level, see N-tests below. Game-side client: not started |
| 16 | Docker relay started | **NOT TESTED** (no Docker daemon in the dev container); the Go binary runs and serves |
| 17 | Incomplete | Everything from Milestone 4 onward except the relay |
| 18 | Next milestone | Get past the pager stall → reach the title screen in a window on Windows |

### Relay tests (`cd server && go test -v`), all PASS, stable over repeated runs

N01 relay starts · N02 client connects · N03 public room · N04 private room ·
N05 code admits, wrong code refused · N06 public list shows live rooms · N07 counts update ·
N08–N10 host name/map/mode shown · N12–N14 2/3/4 humans, 5th refused (`room_full`) ·
client→client traffic blocked, host fan-out works · N23 disconnect detected (timeout) ·
N24 slot → BOT_TAKEOVER · reconnect reservation hides the slot during grace ·
N25 slot re-advertised after grace (`3 humans, 1 bot, 1 joinable`) · N26 newcomer joins
the running match into the bot slot (HUMAN_REJOINING) · N27 only the host can confirm the
handoff → HUMAN_ACTIVE · N28 no leftover bot, 4/4 humans · N29 reconnect within grace
reclaims the slot; a token can't be reused; a late returner can't double-claim ·
N30 private rooms never listed · host leaving closes the room for everyone ·
no bot filler at match start · replayed/spoofed/malformed packets dropped · usernames validated.

N11 (ping) uses the client-measured round trip to the relay, labelled as relay latency;
the server never invents a value. N15–N22 need the game client: **NOT TESTED**.

### Decisions taken this session (tell me to change any)

- **Capacity:** you chose 16. The relay protocol and server support up to 16 (`GEIST_MAX_PLAYERS`).
  The default room size is 4, which is what the original engine supports (4 humans, 8
  combatants with bots). Rooms larger than that need engine changes, and the UI won't offer
  them until those changes are tested.
- **Relay hostname:** `n-space.duckdns.org`, UDP **47720**, in `client/geist_net.ini`.
  I could not check your PC for port conflicts (the Linux shell on your PC would not start).
  Check with `docker ps` / `netstat -ano | findstr 47720` before forwarding the port.
- **Host disconnect:** the session ends cleanly for everyone (`CLOSED host_left`). Host
  migration is deferred until the host-authoritative game state can be snapshotted.
- **Transport:** v1 is plain UDP with connection IDs, source binding, per-control-message
  sequence numbers (replay guard), and rate limits. It is **not encrypted**. An encrypted
  transport (DTLS or Noise) is on the list before public release.
