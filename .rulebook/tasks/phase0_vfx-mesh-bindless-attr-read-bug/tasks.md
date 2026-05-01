## 1. Diagnose

- [x] 1.1 Added `UZ_VFX_DIAG_ATTR_READBACK` macro gating a per-frame readback of `gpuBuf.WriteSet().positions / lifespans / ages` immediately after the end-of-tick `UAV -> ShaderResource` transition in `frame_vfx_inline.cpp`. Scans the FULL buffer (capacity items, not just first 4 — spawn pops free-list LIFO so first-spawned slots are 199, 198, 197… NOT 0, 1, 2). Outcome: **spawn / update writes are SANE**. At frame 1 the readback reports `gpuAliveSeen=4` particles at `pos=(-10.33, -15.48, 104.79) life=5.000 age=0.121`. Position magnitude grows over frames (104 → 109 → 113 → 117 cm). Values match expected spawn behavior (emitter at origin + velocity (0,0,200) cm/s + gravity (0,0,-80) cm/s²). **Rules out path 4 (spawn writes garbage).**

- [x] 1.2 Tested explicit `UAV -> UAV` flush barrier on each attribute buffer immediately before the `UAV -> ShaderResource` transition. Build clean, no regression. **Result: engulf-bug stops appearing in the screenshot, but no visible particles either.** Combined with the UZ_VFX_DIAG_ALIVECOUNT readback, observed: `mesh={instCount=0 idxPerInst=12 indirectInst=0}` every frame — the mesh-instance CS dispatches but writes ZERO instances to `instanceCount` and `indirectArgs.InstanceCount`. The CS's bindless SRV reads of lifespans return 0, so the dead-particle check (`lifespan <= 0`) fires for ALL threads and they all early-return. **Rules out path 2 (UAV write hazard) — the explicit UAV barrier does not solve the problem.**

- [x] 1.2.1 Tested cmd-list split-submit (`End() / Execute() / WaitIdle() / Begin()`) inserted by the readback diagnostic itself between sim and RG. **Same result**: `instCount=0 indirectInst=0` even with WaitIdle. The mesh CS bindless reads still see zero. CPU-side `CopyBuffer` readback (which uses the same SR-state buffer) sees the spawn-written values correctly. **The bindless SRV path through `ResourceDescriptorHeap[]` is what's broken — separate from the buffer state.**

- [ ] 1.3 (BLOCKED — needs RenderDoc / PIX) Capture a RenderDoc frame of the demo running `mesh_sort_shards.uzvfx --headless --frames 80`. In RenderDoc Pipeline State viewer, inspect:
   - Resource state of `VFX_PositionsB` / `VFX_LifespansB` immediately before the `VFX_MeshInstanceCS` dispatch (should be `ShaderResource` per our barrier; if not, the auto-tracker is overriding our manual transition).
   - The descriptor heap entries at `positionsSrvIndex` (37161) and `lifespansSrvIndex` (37167) — verify they actually point to the per-particle attribute buffers and not to some other allocation that got the same index.
   - The shader's actual fetch result via the in-shader debugger (set a breakpoint in `particle_mesh_instance_cs.hlsl:386` `if (lifespan <= 0.0f || age >= lifespan) return;` and inspect `lifespan` value).

- [ ] 1.4 (DEFERRED — not needed; path 4 already ruled out by 1.1) Logging `emitter->worldPosition` was the discriminator for path 4 (spawn writes wrong values). The 1.1 readback definitively proves spawn writes correct positions, so the worldPosition source is correct.

- [ ] 1.5 **Triangulation** (preliminary, pending RenderDoc):
   - Spawn / update CS writes are CORRECT (verified by CopyBuffer readback). Path 4 ruled out.
   - State transition `UAV -> ShaderResource` is emitted (verified by diag-barrier log; barrier_count=7 ws.positions.idx=117).
   - Explicit UAV-flush barrier does NOT help. Path 2 ruled out.
   - CPU-side WaitIdle (split-submit) does NOT help either. Rules out CPU/GPU sync hypothesis.
   - **Most likely remaining hypothesis: the descriptor heap entry for the per-particle attribute buffer SRV was created when the buffer was in a different physical allocation, OR there is a D3D12MA aliasing issue where the SRV view points to memory that got recycled.** Needs RenderDoc descriptor-heap inspection to confirm.
   - Alternative hypothesis: the CS instruction sequence has a memory barrier issue at the HLSL level — e.g., `if (sortedSlot == 0) instanceCount[0] = 0;` followed by other threads' `InterlockedAdd(instanceCount[0], 1, slot)` may have an ordering hazard within the dispatch. But `InterlockedAdd` itself is atomic, so this should not produce `instCount=0` consistently.

## 2. Fix

- [ ] 2.1 Implement the surgical fix matching the diagnosed path. Path 1 = import attribute buffers into RG (gated on `firstCycle == false` to avoid the predecessor's TDR regression). Path 2 = explicit `UAV -> UAV` barrier before the state transition. Path 3 = move SR transition to after the per-emitter loop. Path 4 = fix `scene_vfx_sync.cpp` worldPosition / attach resolution.

- [ ] 2.2 Re-build runtime + demo (`zig build -Ddll=true -Doptimize=ReleaseFast` in `runtime/`, then `runtime/demos/vfx_viewport_app/`).

- [ ] 2.3 Re-run `--gpu-validation --headless --frames 30` and confirm zero `D3D12 ERROR` lines (regression guard against the RG-import-causes-TDR scenario the predecessor fix tripped on).

## 3. Verify visually

- [ ] 3.1 Capture `--vfx-asset vfx/tests/mesh_sort_shards.uzvfx --headless --screenshot e:/UzmiEngine/uzengine/.sandbox/visibility-fixed-v2.png --screenshot-frame 90 --frames 120`. The asset must remain the COMMITTED state (`scale: [3.0, 3.0, 3.0]`, `velocity: [0, 0, 200]`, `sort_mode: ViewDepth`) — do NOT tune the asset to mask the bug.

- [ ] 3.2 Read the PNG with the Read tool. Pass criterion: discrete tetrahedron mesh particles must be IDENTIFIABLE in the sky+ground scene. Pass = visible particle silhouettes against an OBVIOUS sky+ground composition (matches `.sandbox/spline-now.png` baseline composition + adds visible mesh particles). Fail = uniform color filling the viewport, OR camera-engulfed-by-geometry, OR fully black.

- [ ] 3.3 Capture three additional camera-frame variants (`--screenshot-frame 30`, `60`, `120`) to confirm particles persist across the spawn arc. Discrete particles must be visible in ALL three frames; if any frame shows engulfed-camera the fix is incomplete.

- [ ] 3.4 Verify the `mesh_sort_shards.uzfx` editor-schema sister-asset still loads + renders (use the demo's preset cycling or the editor harness — TBD whether a separate command line). Mesh particle path must work for both the runtime production loader (`.uzvfx`) and the editor / serializer schema (`.uzfx`).

## 4. Tail (mandatory — enforced by rulebook v5.3.0)

- [ ] 4.1 Update or create documentation covering the implementation — overhaul `docs/engine/25-vfx/88-mesh-particle-render-path.md` section 7. Replace the "Visibility verified" claim from the predecessor with a corrected entry. Add an explicit "Failure modes investigated" subsection enumerating the four hypothesized root causes and which one matched. Embed the new `visibility-fixed-v2.png` screenshot AND keep the misleading `post-fix-final.png` for reference (annotated as "previous fix's appearance — actually broken").

- [ ] 4.2 Write tests covering the new behavior — extend `runtime/tests/engine/renderer/frame_particles_mesh_guard_test.cpp` (or a new sibling) with a regression test specific to whichever path was fixed. Examples: (path 1) verify the mesh-instance-CS RG pass declares `pb.Read` on attribute buffers when imported; (path 2) verify a `UAV -> UAV` barrier is recorded before the state transition; (path 4) verify the asset emitter's worldPosition is `(0,0,0)` after `ParticleEmitter::Initialize`.

- [ ] 4.3 Run tests and confirm they pass — `runtime/build/bin/test_runtime.exe --gtest_filter="*MeshGuard*:*MeshAssetLookup*:*ParticleMeshPSO*:*Visibility*"` 100% pass; the `phase16_vfx-sort-pipeline-hardware-validation` 1.2 visual A/B re-run produces a screenshot with discrete identifiable mesh particles. Capture the post-fix screenshot and link it from the test description / docs.
