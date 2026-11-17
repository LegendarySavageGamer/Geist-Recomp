# Geist's virtual memory and how the recompiled build handles it

Geist (GITE01 rev 0) programs the PowerPC MMU itself. It pages a virtual window at
**0x7E000000–0x7FFFFFFF** on demand, backed by ARAM. This is n-Space engine code, not the
Dolphin SDK. Three things in it needed work before the native build could get past boot.

## 1. MMU bring-up: `fn_80340CA4` → `fn_80340CF8` → `fn_80340D10`

These functions load SDR1 (the page-table base) and turn on MSR[IR|DR] using `rfi`. Each
`rfi` targets its own fall-through address, so it means "set MSR, continue". gcn-recomp's
`hle_rfi` treats an `rfi` as an indirect jump instead, and the build stopped with
`call_indirect: no function at 80340D38`.
**Fix:** `recomp/patches.txt` entries at 0x80340D0C and 0x80340D34.

## 2. DSI handler `fn_80340EBC`

The handler first checks that DAR is inside the window and that DSISR reports "no PTE".
It then parks r3–r6 and CR in the SPRGs, records the fault in the thread's OSContext
(SRR0, SRR1, CR, LR, CTR, XER), and sets OS_CONTEXT_STATE_EXC. Finally it uses `rfi` to
jump forward to 0x80340FB0 inside the same function.
**Fix:** a patch at 0x80340F90 turns that `rfi` into `goto L_80340FB0`.

## 3. Resuming the faulting thread: `fn_80341000`

`fn_80341000` switches to a temporary context and calls the pager callback, which may
sleep while ARAM/DVD DMA runs. It then calls `OSLoadContext` on the thread's context to
retry the faulting instruction.

gcn-recomp's DSI path assumed every handler ends in an `rfi`. When `OSLoadContext` ran
instead, the context had no host-thread binding. The runtime then started a fresh thread
at SRR0 (a return address in the middle of a function), which crashed.

**Fix (shared runtime, game-agnostic):**
`patches/gcn-recomp-0001-dsi-resume-via-OSLoadContext.patch`. `deliver_dsi` now binds
the current context to a resume point, the same way `irq_deliver` already does for
interrupts (`exc_bind_current`). It writes the context first so the binding stamp
matches what the handler writes. When the handler ends in `rfi`, the binding is dropped
again (`exc_unbind`). When the handler ends in `OSLoadContext`, execution returns
through the setjmp with the CPU as it was at the fault, and the access is retried.
The patch follows gcn-recomp's rules: no game names or addresses.
`tools/check_generic.py` is clean.

## Current stop point

The game presents 3 frames, then the pager thread (started at `fn_8000C334`) stays in
`fn_80286EAC`. That function scans for a page to evict, copies it out with ARAM DMA
(`fn_802F6C24`), and polls `fn_802F6BE8` for completion. Next steps: log ARAM DMA (`--log=hw`
in a build with logging enabled), confirm the DMA-busy register clears, and check that the
page-table entries it edits produce the `tlbie` the runtime expects.

## Crash at the first level load (fixed in v1.0.1): tlbie invalidates a set, not a page

**Symptom (user's PC, 2026-11-12):** Health & Safety, then the Nintendo, Dolby and n-Space
logos play, a loading bar appears, then the game closes. game/geist_log.txt:
`FAULT: guest memory access at (addr & 0x3FFFFFFF) = 01900000`, in fn_800368CC called from
the DVD-completion callback fn_8017CBE0 -> fn_8002FC30 while mainmenu.GSF loads.

**How it was found:** reproduced headless with scripted input
(`GCN_INPUT="200:A:10,400:A:10,600:START:10,900:A:10,1200:START:10"`). A `-DGCN_WATCH`
build with `GCN_WATCH_ADDR=7E3D8ED0` showed the descriptor at 0x7E3D8ED0 going 4 ->
0x902BCE72 during the DI command write (a 0x1F4C0-byte read of mainmenu.GSF into 006D7020).
Walking the page table at that moment showed 0x7E3D8000 **unmapped**, with every frame
above 00729000: Geist had shrunk its paging pool and evicted the page correctly. The
runtime's software TLB still held the old translation, so the game kept using a frame
it had already given to the disc loader.

**Cause:** Geist invalidates with `fn_8034079C`: `rlwinm r0,r3,0,14,19; tlbsync; tlbie r0`.
That passes only EA bits 14-19, the 750's TLB congruence class (64 sets x 2 ways). On the
hardware this drops every translation in that set. `fn_80340C4C` flushes the whole TLB by
issuing tlbie for the 64 set addresses 0x0000-0x3F000. gcn-recomp's hle_tlbie treated the
operand as a page address and cleared one slot of its 4096-entry direct-mapped cache, so
almost none of Geist's invalidations reached the stale entries.

**Fix:** patches/gcn-recomp-0001 now makes hle_tlbie drop every software-TLB entry in the
operand's set (the slots congruent mod 64), which matches the hardware.

**Result:** with scripted input, the game ran 240 s without a fault: logos, main menu, New
Game, then loading and running 01A_X (the first campaign level). Headless speed was about
69 fps on 2 cores (115% of 60).

patches/gcn-recomp-0005 also adds an opt-in disc-timing mode (`di_set_timing`,
`GCN_DVD_TIMING=<KB/s>,<seek us>`): reads are delivered at completion, at a drive's pace.
It was tried during the investigation, isn't needed for this crash, and is off by default.
