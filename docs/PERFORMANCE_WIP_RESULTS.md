# Geist Recompiled v1.1.1 performance work — STOPPED HANDOFF

Prepared 2026-11-20 for ThyHeroOfTime and Claude Opus 5.5.

## Read this first
The owner requested that development stop and that everything completed so far be packaged. This is a WORK IN PROGRESS snapshot, not an accepted performance release. Do not publish it as a verified stable-60-FPS fix. Preserve the current launcher, controls, HUD, saves, GCI importer and report feature. The owner previously deferred further widescreen HUD edge alignment. Do not restart or replace the launcher.

## What is included
- Full current project source, including the patched gcn-recomp runtime, launcher, steering tables, SDK build tools, tests, networking service and protected bug-report service.
- Complete Windows client/toolchain/SDK with the rebuilt launcher and updated runtime archive. The player must build the game from their own verified GITE01 rev-0 disc. Game-derived C, the game executable, disc, memory cards, saves and private webhook credentials are excluded.
- Performance CSVs, runtime/build/test logs, small visual evidence, machine summary, and an exact textual patch against the original source used for baseline measurements.
- The original directive, for context. Treat it as the owner's requested goals; this handoff describes actual completion and limitations.

## Machine and execution model
Windows x64; Intel Core i7-8700K; NVIDIA GTX 1650; OpenGL 4.6, NVIDIA 621.14. SDL uses WASAPI, 48000-Hz signed-16-bit stereo, 512-frame callbacks (~10.67 ms). Observed game DMA rate is 32000 Hz.
The launcher statically translates PowerPC functions into native C, then compiles/links 60 generated units with a native support runtime. There is no Dolphin process or PPC interpreter dispatch loop. Hardware/OS/memory/graphics/audio services remain emulated or HLE: this is a hybrid static recompilation. GX is translated to OpenGL; SDL provides input and audio. Guest host threads hand off execution, optional GX frontend/transform workers build batches, and the main thread renders/presents them.

## Confirmed problems and implemented changes
1. Previously the device callback ran the mixer itself: DMA/DTK mutexes, per-32-byte streamed-disc reads, decoder/vector work, optional capture writes. The new Windows callback only consumes a bounded SPSC PCM queue and records diagnostics. A high-priority producer mixes 256-frame chunks. The physical queue capacity is 32768 frames; normal target is only ~21 ms or optional ~40 ms, with a minimum two driver callbacks. There is no continuous device reset.
2. DTK now has bounded asynchronous compressed read-ahead (three 32-KiB blocks) on a separate disc-I/O worker. Current/next-track prefetch avoids evicting current data. Portable non-SDL backends retain synchronous cached reads. Miss recovery advances music rather than permanently accumulating drift after a started stream; initial delayed playback preserves the first samples. Recovery fades in. This logic needs more listening and seek/loop testing.
3. Actual DMA rate is queried; 32-to-48 and 48-to-48 conversion are supported. A million-frame SPSC stress test, stereo-order test, measured 1000-Hz resampling checks and delayed-read tests pass. Atomic recording-tap publication removes a race. WAV header updates are reduced to once per second plus final close.
4. Desktop shader program binaries were compile-time disabled by the GL3.3 loader despite the GPU supporting GL4.6. Optional GL4.1/ARB entry points are now dynamically resolved; GL3.3/source fallback remains. Cache records use existing driver fingerprint/source hashes. New source records are written by a worker; program-binary extraction/full rewrite is deferred until clean exit, away from active rendering. First-ever shader compilation can still stall. Two-run binary reuse, corruption/driver mismatch and cache-Off fallback have NOT been fully verified.
5. Automatic input logging (including frequent clock-jump flushes) is off by default; --input-log and --input-log=<dir> explicitly enable diagnostics. Routine expected VM faults log only with GCN_VM_TRACE. Essential exception handling remains.
6. Optional performance overlay/CSV measures real game-frame swap completion intervals, average, 1% low, spikes, CPU render-thread work and delayed nonblocking GPU batch timer results. It is not a scanout or full guest-CPU profiler. 1% low is 1000 / mean of the worst ceil(N/100) intervals; the overlay uses a rolling 720-sample window. GPU is explicitly latest completed batch, not whole-frame GPU time. Newest source accumulates render CPU across batches per presented frame; older baseline/candidate-matched CSV columns were last-batch CPU and must not be compared as full-frame CPU. 0/60 frame cap is an absolute-deadline presentation limiter, independent of guest time.
7. Launcher and F1 share settings.ini: monitor, display mode/resolution/aspect/internal scale/filter/VSync/FPS, cap, graph, persistent cache, volumes, output selection and 20/40-ms buffer. Apply/Save/Defaults connect to real settings. Launcher edits take effect at next game startup; F1 applies live. Music and voices use a shared bus; independent voice volume is NOT implemented and is labelled shared rather than faked. Output-device removal/default fallback is implemented; physical unplug testing remains outstanding.

## Important failed acceptance results
The smaller 40-ms DMA cushion (previously 100 ms) caused more game-source underruns in the matched replay: 10 versus 1. The output device itself had zero underruns, but zero device underruns does NOT mean uninterrupted game audio: the producer can feed silence while the DMA source refills. DO NOT claim all crackling/desync is fixed. Source ring recovery still needs improvement; simply adding a large delay is not an accepted final solution.
Frame pacing is NOT fixed: matched replay averaged 53.66 FPS versus 54.30 baseline; spikes over 33.334 ms increased from 224 to 291. The worst frame improved in this run, but this does not establish an overall performance improvement. See raw logs.
No default clock/simulation-rate change was made. The virtual back-edge clock can consume a synthetic per-field CPU budget. Existing GCN_CLOCK=host was tested separately; it did not establish a reliable improvement. Host time tracked wall time, but Chapter 9 still ran around 20–35 FPS in heavy scenes. Keep this experimental; do not enable by default without gameplay/timer/physics validation. Do not change VI/audio rate or time scale to inflate FPS.

## Measurements
Same 9000-present replay fixture, same starting shader-cache file, 1920x1080 windowed, 16:9, internal 2x, VSync Off:
| Metric | Baseline | Candidate matched |
|---|---:|---:|
| Presented FPS | 54.2968 | 53.6579 |
| Average interval ms | 18.4173 | 18.6366 |
| 1% low FPS | 21.1931 | 21.0321 |
| Worst interval ms | 202.7965 | 165.4092 |
| Intervals >33.334 ms | 224 | 291 |
| DMA-source underruns | 1 | 10 |
| Device-output underruns | 0 | 0 |
| Max device callback ms | 0.4315 | 0.0662 |
| Disc reads inside device callback | 62373 | 0 |
An earlier first baseline had a 97.766-ms callback, but that cold/occasional observation is NOT the representative matched warm baseline. The callback refactor addresses that blocking path; do not selectively compare cold worst against warm best.
Candidate-matched was built before the final next-track prefetch and CPU-label refinements. Those refinements are in the final compiled SDK; unit tests cover the read-ahead correction, but there is no new matched 9000-frame trace of the final binary.
Host-menu run: 14000 frames, 54.83 FPS, 6 DMA underruns, 0 output underruns. Not comparable gameplay evidence.
Host-play exploratory run: 12854 frames, ~342 seconds; includes menus/loading/cutscene, F1 interactions and mission-failed screen, so its 37.89-FPS aggregate is NOT a controlled gameplay benchmark. 8 DMA underruns, 0 output underruns, 0 callback disc reads. No crash. It is not an extended synchronization sign-off.

## PASS / FAIL / NOT TESTED
PASS: Windows runtime/launcher compile and native executable link; build log included.
PASS: Audio ring million-frame concurrent ordering, bounded fade, stereo order, 32/48-kHz sine conversion, cached reads, 80-ms read-ahead preservation and 100-ms simulated disc delays without device callback disc/file I/O or output underruns.
PASS: Shared controls/options Apply/Save/Cancel/Defaults, persistence, conflicts, CRLF/custom entries, atomic save/external-change protection.
PASS: Real SDL virtual-controller buttons/axes; keyboard and mouse button remaps; native relative mouse accumulation, consume-once, 0.01x/20x sensitivity, yaw/pitch/script lock, controller coexistence.
PASS: Existing finished save opens; Chapter 9 Volks selected visually, loads cutscene, skips to playable HUD using Start. Existing HUD shown. F1 and launcher graphics/audio screens render. Test card was a COPY; original user saves were not modified.
FAIL: Consistent 60-FPS/frame-pacing target and complete source-audio stability.
NOT TESTED: Final-build prolonged A/V listening/sync, complete Chapter 1 progression/PDA scene, successful sustained combat/weapon effects, movement speed/physics/timers, possession/dispossession, level transitions, all aspect/display modes on final binary, physical controllers/device unplug, final shader-cache warm reuse/invalidation, clean-room full source build, Quiver download/install flow and actual Discord report re-delivery.
Attempted movement/fire/dispossess in Chapter 9 came after its helicopter mission had already failed; DO NOT count these as successful gameplay tests. The exploratory chapter eventually reached normal Mission Failed, not a crash.

## File map
runtime under gcn-recomp/runtime:
- audio_sdl.cpp: output callback, mixer thread, latency/device selection and shutdown.
- audio_output.h: bounded stereo SPSC queue, underrun fades.
- audio.cpp: source-rate resampling, DTK mixing/read-ahead/recovery, taps and WAV capture.
- dtk_reader.h: bounded async compressed reads and generation-safe publication.
- performance.h: counters/CSV/statistics/pacing.
- main.cpp: timers, diagnostics flags, device events, clean audio/cache shutdown.
- gx/render_gl.cpp/.h: program binaries, cache worker, deferred final persistence.
- pc_menu.cpp/.h: functional settings, early preferences and performance graph.
- mmu.cpp: opt-in routine VM fault logging; record.cpp: atomic tap declaration.
launcher/src: pc_options.h settings model; main.cpp settings layout/default/apply/save and screenshot test modes.
tests: audio_pipeline_test.cpp new; controls_editor_test.cpp expanded. Existing input/native mouse tests unchanged.
tools/make_sdk.sh: SDK revision changed for rebuild invalidation.

## Build and resume instructions
Use this archive's gcn-recomp directory as the authoritative patched runtime. Do NOT reset it or overwrite it with an upstream clone; do not rely on old patches to recreate the new changes. Existing top-level build.ps1 preserves an included runtime.
Current machine full source: C:/Users/Midni/Documents/Codex/Geist-performance-2026-11-20/source
Current private test client: same task directory/candidate-client (has generated game code; PRIVATE, excluded).
Installed original client: G:/GeistRecomp/GeistRecompiled. It has NOT been overwritten by this task.
Owner's original supplied ZIP: G:/GeistRecomp/GeistRecompiled.zip. It is older than these outputs.
Original/source baseline: task directory/baseline-source. Exact changes also in PERFORMANCE_CHANGES.patch.
Toolchain: installed client/toolchain/bin; SDL static library: client/sdk/lib/libSDL2.a. On this machine SDL headers were C:/devkitPro/portlibs/switch/include/SDL2; these are not bundled in the binary toolchain. Obtain compatible SDL2 headers for a different machine.
Python: C:/Users/Midni/AppData/Local/Programs/Python/Python312/python.exe
User-owned test ISO: E:/Nintendo GameCube/ISO/Geist (USA).iso (not included).
Fresh build: configure CMake/Ninja with the Windows x64 compiler/toolchain and compatible SDL2 headers/library, build the runtime/game from your own disc, run tools/make_sdk.sh, then launcher/build_launcher.sh. Existing documented CMake builds were not rerun clean-room in this task.
For reproducing this snapshot on this machine, evidence/build_profile.py compiles seven changed runtime units, replaces their members in a copied known-good SDK and runs the launcher --build. It references the previous private object cache; adapt paths on another machine. The full source is present; it is not limited to those seven units. evidence/build_launcher.py rebuilds the launcher; evidence/run_tests.py builds/runs four targeted tests.
SDK revision is 1.1.1-performance-wip.20261120, while UI/product version stays 1.1.1. This forces users with an older built game to rebuild from their disc instead of silently keeping old runtime code. The final game executable was linked/tested before this metadata-only bump. Public client intentionally has no game/work folders.
Replay traces use virtual back-edge timing: do not run the v4 replay under host clock and call it the same workload. The source snapshot intentionally contains no replay memory card. On this machine the private fixture is baseline-run/saves/inputs/20261120-195021. run_matched.ps1 uses it. frame-route.txt converts its channel-0 PAD changes into frame-based presses for non-replay experiments; host-play used manual test-backend input, not the full matched replay.
Test hooks: GCN_PERF_CSV, GCN_PERF_FRAMES, GCN_CLOCKLOG, GCN_TEST_INPUT_FILE, GCN_INPUT_FILE, GCN_SCREENSHOT_FILE, GCN_PCMENU_SHOT. Input events accepted: key <name> down/up; mouse dx dy; button left/right/x1 down/up; padbutton/padaxis; event key F1; event click x y. GCN_INPUT_FILE accepts frame:buttons:duration / now:... scripts. Hidden game windows require WM_CLOSE to quit; CloseMainWindow() may return false because they are hidden. Last test was closed by posting WM_CLOSE to windows belonging only to its PID, with a clean performance summary/cache close.

## Existing reporting and security context
Public relay endpoint remains client/reporting.ini; Discord webhook stays server-side. Do not put webhook URLs, service.env, real reports or private credentials in client/source archives. Reporting service source/Docker files are included, existing deployment was not changed here. Do not send test Discord reports without explicit authorization. Prior session implemented screenshot/MP4 forwarding; this task did not rerun live delivery. Existing networking/reporting source was preserved.

## Recommended next work
1. Reproduce final candidate's source underruns and isolate guest delivery versus render/GX waits. Restore/retune source buffering only with latency and listening evidence. Add proper source underflow fade, if needed, rather than relying on a silent filled output queue.
2. Profile guest execution, GX queue/draw-done waits and host-thread handoffs (GCN_STALLS, GCN_SWITCHLOG), plus CPU utilization. Render-thread/GPU batch timings alone do not identify all bottlenecks. Do not infer low total GPU use from one batch.
3. Verify binary cache writes on clean exit, actual binary hits on second startup, cache-Off behavior and invalidation. Current prewarm logs still showed source compiles; successful capability discovery alone is not evidence of binary reuse.
4. Repeat a controlled gameplay trace at each relevant clock setting with simulation/timer checks. Leave guest clock default intact until proven compatible. Keep first-ever shader stalls and loading separate from steady gameplay.
5. Test Chapter 1, sustained combat/possession, physical input, display-mode matrix, long A/V sync and report flow, then clean-room build/Quiver update. Only after acceptance should this be labelled a finished release.
