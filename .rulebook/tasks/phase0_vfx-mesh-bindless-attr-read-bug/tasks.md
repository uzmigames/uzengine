## 1. Diagnose

- [x] 1.1 Added `UZ_VFX_DIAG_ATTR_READBACK` macro gating a per-frame readback of `gpuBuf.WriteSet().positions / lifespans / ages` immediately after the end-of-tick `UAV -> ShaderResource` transition in `frame_vfx_inline.cpp`. Scans the FULL buffer (capacity items, not just first 4 — spawn pops free-list LIFO so first-spawned slots are 199, 198, 197… NOT 0, 1, 2). Outcome: **spawn / update writes are SANE**. At frame 1 the readback reports `gpuAliveSeen=4` particles at `pos=(-10.33, -15.48, 104.79) life=5.000 age=0.121`. Position magnitude grows over frames (104 → 109 → 113 → 117 cm). Values match expected spawn behavior (emitter at origin + velocity (0,0,200) cm/s + gravity (0,0,-80) cm/s²). **Rules out path 4 (spawn writes garbage).**

- [x] 1.2 Tested explicit `UAV -> UAV` flush barrier on each attribute buffer immediately before the `UAV -> ShaderResource` transition. Build clean, no regression. **Result: engulf-bug stops appearing in the screenshot, but no visible particles either.** Combined with the UZ_VFX_DIAG_ALIVECOUNT readback, observed: `mesh={instCount=0 idxPerInst=12 indirectInst=0}` every frame — the mesh-instance CS dispatches but writes ZERO instances to `instanceCount` and `indirectArgs.InstanceCount`. The CS's bindless SRV reads of lifespans return 0, so the dead-particle check (`lifespan <= 0`) fires for ALL threads and they all early-return. **Rules out path 2 (UAV write hazard) — the explicit UAV barrier does not solve the problem.**

- [x] 1.2.1 Tested cmd-list split-submit (`End() / Execute() / WaitIdle() / Begin()`) inserted by the readback diagnostic itself between sim and RG. **Same result**: `instCount=0 indirectInst=0` even with WaitIdle. The mesh CS bindless reads still see zero. CPU-side `CopyBuffer` readback (which uses the same SR-state buffer) sees the spawn-written values correctly. **The bindless SRV path through `ResourceDescriptorHeap[]` is what's broken — separate from the buffer state.**

- [ ] 1.3 (BLOCKED — RenderDoc / PIX unavailable in this automated headless environment) Capture a GPU frame of the unchanged `mesh_sort_shards.uzvfx` with `sort_mode: ViewDepth`. The later bisection isolates the remaining failure to `sortedIndices`, not general attribute reads. Inspect:
   - Whether the sort dispatch actually executes for this emitter, its output indices, the `sortedIndicesSrvIndex` descriptor heap entry, and the sort buffer state immediately before `VFX_MeshInstanceCS`.
   - The actual `sortedIndices[sortedSlot]` fetch in the shader debugger: recorded reads all return zero, mapping threads to unspawned particle slot zero and producing `instCount=0`.
   - Resource state of `VFX_PositionsB` / `VFX_LifespansB` immediately before the `VFX_MeshInstanceCS` dispatch (should be `ShaderResource` per our barrier; if not, the auto-tracker is overriding our manual transition).
   - The descriptor heap entries at `positionsSrvIndex` (37161) and `lifespansSrvIndex` (37167) — verify they actually point to the per-particle attribute buffers and not to some other allocation that got the same index.
   - The shader's actual fetch result via the in-shader debugger (set a breakpoint in `particle_mesh_instance_cs.hlsl:386` `if (lifespan <= 0.0f || age >= lifespan) return;` and inspect `lifespan` value).

- [ ] 1.4 (NOT PERFORMED — redundant after 1.1; retained unchecked) Logging `emitter->worldPosition` was the discriminator for path 4 (spawn writes wrong values). Full-buffer readback already proves sane spawn writes. This additional log is not evidence needed to resolve the remaining sorted-index read; that diagnosis is blocked on the unavailable RenderDoc / PIX capture in 1.3.

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

Historical evidence only: with `sort_mode: None`, the prior bisection recorded visible tetrahedra with depth and lighting (`.sandbox/final-unsorted-frame60.png`, 126 KB) and 4-7 indirect instances. This does not satisfy any committed-asset visual item below. With committed `ViewDepth`, it recorded `instCount=0` and a baseline-only screenshot (179 801 bytes).

- [ ] 3.1 (BLOCKED — sortedIndices read diagnosis needs unavailable RenderDoc / PIX) Capture `--vfx-asset vfx/tests/mesh_sort_shards.uzvfx --headless --screenshot e:/UzmiEngine/uzengine/.sandbox/visibility-fixed-v2.png --screenshot-frame 90 --frames 120`. Keep the COMMITTED asset (`scale: [3.0, 3.0, 3.0]`, `velocity: [0, 0, 200]`, `sort_mode: ViewDepth`); do not tune it to mask the bug. No screenshot run was attempted during this automated closeout: GPU rendering hangs headless here.

- [ ] 3.2 (BLOCKED — ViewDepth renders nothing until the capture-backed fix) Read the new PNG. Pass only if discrete tetrahedra are identifiable against obvious sky and ground, matching the `.sandbox/spline-now.png` composition with added particles. Uniform color, engulfing geometry, fully black output, and a baseline-only scene fail.

- [ ] 3.3 (BLOCKED — same sorted-path failure and unavailable GPU capture) Capture frames 30, 60, and 120. Discrete particles must remain visible in all three; no frame may engulf the camera.

- [ ] 3.4 (BLOCKED — same sorted-path failure and unavailable GPU capture) Verify `mesh_sort_shards.uzfx` loads and renders through the editor-schema loader. Loading alone cannot satisfy the required rendering evidence.

## 4. Tail (mandatory — enforced by rulebook v5.3.0)

- [ ] 4.1 (DOCUMENTATION UPDATED; screenshot requirement BLOCKED) Section 7 of `docs/engine/25-vfx/88-mesh-particle-render-path.md` now corrects the predecessor's visibility claim, explains the landed count race fix and unresolved sortedIndices read, and enumerates the four hypotheses without inventing a confirmed GPU root cause. The previous `post-fix-final.png` reference is retained and embedded as "previous fix's appearance - actually broken". No `visibility-fixed-v2.png` exists from this closeout; embedding genuine fixed visibility evidence remains blocked on the RenderDoc / PIX diagnosis and sorted-path fix.

- [x] 4.2 Added `MeshGuard_SortedInstanceCount.PostDeadCheckSortedPathHasNoThreadZeroGate` in `runtime/tests/engine/renderer/frame_particles_mesh_guard_test.cpp`. The source-level guard strips HLSL comments, locates the dead check, parses the sorted branch, and rejects `if (sortedSlot == 0)` there while allowing pre-dead-check initialization. It guards the incremental race fix, not the unresolved sorted-index GPU read.

- [x] 4.3 CPU regression verification completed 2026-10-14. The bounded `cd runtime && zig build test -Doptimize=ReleaseFast` with `GTEST_FILTER=-RuntimeViewportTest.*` compiled/installed the tests and ran 7681 tests from 754 suites in 31495 ms: 7661 passed, with the same three pre-existing user VFX-editor WIP failures (`VfxEditorSlate.WorkbenchHasTwoColumns`, `TitleBarReflectsEmitterName`, `StatusBarBindingsReflectTelemetry`). Therefore this full build/test command exited 1 (47/49 build steps succeeded), not a clean full-suite pass. No failing test was changed or suppressed. The subsequent full-path `E:/UzmiEngine/uzengine/runtime/build/bin/test_runtime.exe --gtest_filter="*MeshGuard*:*MeshAssetLookup*:*ParticleMeshPSO*:*Visibility*"` passed **37/37 tests from 9 suites, exit 0**, including `MeshGuard_SortedInstanceCount.PostDeadCheckSortedPathHasNoThreadZeroGate` (3 ms). Build/full-suite deadline: 30 minutes; filtered-run deadline: 2 minutes. The original hardware visual A/B and screenshot obligation remains explicitly blocked under 3.1-3.4 and 4.1, not satisfied by these CPU tests. Clangd diagnostics were unavailable (clangd not installed); compilation and the scoped `git diff --check` completed without errors.

**Status: partial closeout only; task remains BLOCKED, not archived.** Recommended follow-up: `phase0_vfx-mesh-sortedindices-bindless-read` (recommendation only, not created).

Follow-up acceptance criteria:

1. Capture RenderDoc / PIX evidence of sort dispatch/gating, output indices, the sorted-index SRV descriptor and pre-mesh-dispatch resource state, and actual shader fetches. Show alive slots resolve to live particle indices rather than all reading unspawned slot zero.
2. Fix the evidenced cause; show nonzero matching mesh/indirect instance counts with the unchanged committed `ViewDepth` asset.
3. Read screenshots at frames 30, 60, 90, and 120 showing discrete identifiable tetrahedra against sky and ground, with no engulfment or black output. Verify the `.uzfx` sister asset loads and renders.
4. Pass the renderer regression filters and rerun `phase16_vfx-sort-pipeline-hardware-validation` item 1.2 visual A/B. Link the genuine fixed screenshots in section 7 before closing the remaining visual/documentation items.
