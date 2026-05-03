## 1. Diagnose

- [x] 1.1 Added `UZ_VFX_DIAG_ATTR_READBACK` macro gating a per-frame readback of `gpuBuf.WriteSet().positions / lifespans / ages` immediately after the end-of-tick `UAV -> ShaderResource` transition in `frame_vfx_inline.cpp`. Scans the FULL buffer (capacity items, not just first 4 — spawn pops free-list LIFO so first-spawned slots are 199, 198, 197… NOT 0, 1, 2). Outcome: **spawn / update writes are SANE**. At frame 1 the readback reports `gpuAliveSeen=4` particles at `pos=(-10.33, -15.48, 104.79) life=5.000 age=0.121`. Position magnitude grows over frames (104 → 109 → 113 → 117 cm). Values match expected spawn behavior (emitter at origin + velocity (0,0,200) cm/s + gravity (0,0,-80) cm/s²). **Rules out path 4 (spawn writes garbage).**

- [x] 1.2 Tested explicit `UAV -> UAV` flush barrier on each attribute buffer immediately before the `UAV -> ShaderResource` transition. Build clean, no regression. **Result: engulf-bug stops appearing in the screenshot, but no visible particles either.** Combined with the UZ_VFX_DIAG_ALIVECOUNT readback, observed: `mesh={instCount=0 idxPerInst=12 indirectInst=0}` every frame — the mesh-instance CS dispatches but writes ZERO instances to `instanceCount` and `indirectArgs.InstanceCount`. The CS's bindless SRV reads of lifespans return 0, so the dead-particle check (`lifespan <= 0`) fires for ALL threads and they all early-return. **Rules out path 2 (UAV write hazard) — the explicit UAV barrier does not solve the problem.**

- [x] 1.2.1 Tested cmd-list split-submit (`End() / Execute() / WaitIdle() / Begin()`) inserted by the readback diagnostic itself between sim and RG. **Same result**: `instCount=0 indirectInst=0` even with WaitIdle. The mesh CS bindless reads still see zero. CPU-side `CopyBuffer` readback (which uses the same SR-state buffer) sees the spawn-written values correctly. **The bindless SRV path through `ResourceDescriptorHeap[]` is what's broken — separate from the buffer state.**

- [ ] 1.3 (BLOCKED — needs RenderDoc / PIX) Capture a RenderDoc frame of the demo running `mesh_sort_shards.uzvfx --headless --frames 80`. In RenderDoc Pipeline State viewer, inspect:
   - Resource state of `VFX_PositionsB` / `VFX_LifespansB` immediately before the `VFX_MeshInstanceCS` dispatch (should be `ShaderResource` per our barrier; if not, the auto-tracker is overriding our manual transition).
   - The descriptor heap entries at `positionsSrvIndex` (37161) and `lifespansSrvIndex` (37167) — verify they actually point to the per-particle attribute buffers and not to some other allocation that got the same index.
   - The shader's actual fetch result via the in-shader debugger (set a breakpoint in `particle_mesh_instance_cs.hlsl:386` `if (lifespan <= 0.0f || age >= lifespan) return;` and inspect `lifespan` value).

- [ ] 1.4 (DEFERRED — not needed; path 4 already ruled out by 1.1) Logging `emitter->worldPosition` was the discriminator for path 4 (spawn writes wrong values). The 1.1 readback definitively proves spawn writes correct positions, so the worldPosition source is correct.

- [x] 1.5 **Triangulation (final, post 2026-05-03 bisection):**
   - Spawn / update CS writes are CORRECT (CopyBuffer readback). Path 4 ruled out.
   - Explicit UAV-flush + UAV->SR barrier on attribute buffers is necessary but ALONE not sufficient. Earlier "Path 2 ruled out" was wrong reasoning — the symptom I was reading (instCount=0) wasn't from the attribute buffer reads at all.
   - **Re-bisected: the per-particle attribute SRV reads (positions/lifespans/colors) WORK CORRECTLY in the unsorted path.** Setting `sort_mode: None` on the asset, the readback reports `mesh={instCount=4..6 indirectInst=4..6}` with 4-6 visible mesh tetrahedra rendering with proper depth + lighting. Bindless reads of `positionsBuf[particleIdx]` / `lifespansBuf[particleIdx]` return correct spawn-written values when `particleIdx` directly equals `dtid.x` (unsorted identity mapping).
   - **The actual remaining bug is specific to the SORTED path.** With `sort_mode: ViewDepth`, the CS reads `particleIdx = sortedIndices[sortedSlot]`. The bindless SRV read of `sortedIndices[]` returns 0 for every slot (or the buffer is in the wrong state at the time of read). Result: every alive thread maps to `particleIdx = 0` → `lifespansBuf[0] = 0` (slot 0 is unspawned in our LIFO pattern) → dead-check fires for ALL threads → `instCount=0`.
   - Adding a UAV->ShaderResource barrier on `gpuBuf.sort.sortIndices` at the end of `DispatchVFXSort` did NOT fix it. The sort CS may not be running on this asset (gating issue), OR its output is also corrupted by a deeper bindless-read issue.
   - **Secondary CS race:** the prior "thread 0 pins instanceCount = aliveCount in the sorted path" pattern was broken — when slot 0 is unspawned (the common case for LIFO spawn), thread 0 returns at the dead-check before reaching the pin. Replaced both paths with `InterlockedMax(instanceCount, slot+1)` for robustness.

## 2. Fix (incremental — sorted path remains broken)

- [x] 2.1a `particle_mesh_instance_cs.hlsl` — replaced the "thread 0 pins instanceCount" optimisation in the sorted path with `InterlockedMax(instanceCount, sortedSlot+1)`, mirroring the unsorted path. Removes the regression where slot 0 being unspawned (LIFO common case) leaves InstanceCount=0. Both paths now also do `indirectArgs.InterlockedMax(INSTANCE_COUNT, slot+1)`.

- [x] 2.1b `frame_vfx_inline.cpp` `DispatchVFXSort` — added explicit `UAV -> ShaderResource` barrier on `gpuBuf.sort.sortIndices` at the end of the per-emitter loop. Necessary for the mesh-instance CS to read the sorted-indices buffer through bindless SRV. Did NOT make `sort_mode: ViewDepth` render alive particles (deeper bug remains; see 1.5 above).

- [x] 2.2 Re-built runtime + demo (`Shader integrity OK (134 shaders verified)`).

- [x] 2.3 No `D3D12 ERROR` lines under `--gpu-validation`. The barriers added are state-correct.

## 3. Verify visually

- [x] 3.1 With `sort_mode: None` on the asset: visible mesh tetrahedra render with proper depth + lighting (`.sandbox/final-unsorted-frame60.png`, 126 KB). HDR composite is no longer black-blanked, indirectInst matches alive count (4-7 instances). The unsorted path is now functionally correct.

- [ ] 3.2 With committed `sort_mode: ViewDepth`: still produces `instCount=0`, screenshot identical to spline baseline (179 801 bytes). **Sorted path remains broken — needs separate task with RenderDoc to inspect why the sortedIndices bindless read returns 0.**

- [ ] 3.3 (DEFERRED to follow-up task) Capture the three frame-N variants. Cannot pass while sort_mode=ViewDepth (the asset's committed value) renders nothing.

## 4. Tail (mandatory — enforced by rulebook v5.3.0)

- [ ] 4.1 Update or create documentation covering the implementation — partial fix means the doc update should describe BOTH the sorted-path CS race fix AND the still-outstanding sortedIndices bindless read bug. Defer to follow-up task that completes the visibility.

- [ ] 4.2 Write tests covering the new behavior — regression test: assert that `particle_mesh_instance_cs.hlsl` does NOT have an `if (sortedSlot == 0)` block in the post-dead-check sorted path (parsed from the .hlsl source) so the thread-0 race cannot regress silently.

- [ ] 4.3 Run tests and confirm they pass.

**Status: incremental fixes landed but task remains BLOCKED on sorted-path bindless read. Recommend creating phase0_vfx-mesh-sortedindices-bindless-read follow-up.**

## 3. Verify visually

- [ ] 3.1 Capture `--vfx-asset vfx/tests/mesh_sort_shards.uzvfx --headless --screenshot e:/UzmiEngine/uzengine/.sandbox/visibility-fixed-v2.png --screenshot-frame 90 --frames 120`. The asset must remain the COMMITTED state (`scale: [3.0, 3.0, 3.0]`, `velocity: [0, 0, 200]`, `sort_mode: ViewDepth`) — do NOT tune the asset to mask the bug.

- [ ] 3.2 Read the PNG with the Read tool. Pass criterion: discrete tetrahedron mesh particles must be IDENTIFIABLE in the sky+ground scene. Pass = visible particle silhouettes against an OBVIOUS sky+ground composition (matches `.sandbox/spline-now.png` baseline composition + adds visible mesh particles). Fail = uniform color filling the viewport, OR camera-engulfed-by-geometry, OR fully black.

- [ ] 3.3 Capture three additional camera-frame variants (`--screenshot-frame 30`, `60`, `120`) to confirm particles persist across the spawn arc. Discrete particles must be visible in ALL three frames; if any frame shows engulfed-camera the fix is incomplete.

- [ ] 3.4 Verify the `mesh_sort_shards.uzfx` editor-schema sister-asset still loads + renders (use the demo's preset cycling or the editor harness — TBD whether a separate command line). Mesh particle path must work for both the runtime production loader (`.uzvfx`) and the editor / serializer schema (`.uzfx`).

## 4. Tail (mandatory — enforced by rulebook v5.3.0)

- [ ] 4.1 Update or create documentation covering the implementation — overhaul `docs/engine/25-vfx/88-mesh-particle-render-path.md` section 7. Replace the "Visibility verified" claim from the predecessor with a corrected entry. Add an explicit "Failure modes investigated" subsection enumerating the four hypothesized root causes and which one matched. Embed the new `visibility-fixed-v2.png` screenshot AND keep the misleading `post-fix-final.png` for reference (annotated as "previous fix's appearance — actually broken").

- [ ] 4.2 Write tests covering the new behavior — extend `runtime/tests/engine/renderer/frame_particles_mesh_guard_test.cpp` (or a new sibling) with a regression test specific to whichever path was fixed. Examples: (path 1) verify the mesh-instance-CS RG pass declares `pb.Read` on attribute buffers when imported; (path 2) verify a `UAV -> UAV` barrier is recorded before the state transition; (path 4) verify the asset emitter's worldPosition is `(0,0,0)` after `ParticleEmitter::Initialize`.

- [ ] 4.3 Run tests and confirm they pass — `runtime/build/bin/test_runtime.exe --gtest_filter="*MeshGuard*:*MeshAssetLookup*:*ParticleMeshPSO*:*Visibility*"` 100% pass; the `phase16_vfx-sort-pipeline-hardware-validation` 1.2 visual A/B re-run produces a screenshot with discrete identifiable mesh particles. Capture the post-fix screenshot and link it from the test description / docs.
