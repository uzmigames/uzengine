# Proposal: phase0_vfx-mesh-bindless-attr-read-bug

## Why

`phase0_vfx-mesh-particle-visibility-debug` (archived 2026-04-30, commit
`b3eae87`) was archived prematurely. The fix made the mesh-particle
render path EXECUTE end-to-end (HDR composite stops being all-zero,
`--diag` reports `hdr OK avg=4.30`), but the underlying bindless SRV
read of per-particle attribute buffers in `particle_mesh_instance_cs.hlsl`
STILL returns garbage data. The earlier fix only "looked correct" because
the garbage positions placed enormous tetrahedra spanning the camera
frustum — the `--diag avg=4.30` reading was the camera-engulfing
particle surfaces, not visible mesh particles in a proper scene.

### Concrete evidence captured 2026-04-30

| Test in CS                                         | Output                                                                          |
|-----------------------------------------------------|---------------------------------------------------------------------------------|
| `pos = positionsBuf[idx]` (real) + `scale=1.0`     | giant tetrahedra engulfing the viewport (262 KB / 348 KB depending on scale)    |
| `pos = float3(0,0,0)` hardcoded + `scale=1.0`      | clean sky+ground + small visible cyan/blue cluster of tetrahedra near origin  ✓ |
| `pos = float3(0,0,0)` hardcoded + `scale=0.001`    | clean sky+ground + sub-pixel particles (179 801 bytes — matches spline baseline)|
| `color = encode(pos)` mapping `[-1000,1000] → [0,1]`| **pure white** (saturated) → `pos > 1000` on every axis (garbage)               |
| `pos` hardcoded + `color` hardcoded                 | clean expected scene                                                            |

The encoded-position screenshot proves position values returned by
`positionsBuf[particleIdx].xyz` are well outside the expected range.
The asset places the emitter at world `(0,0,0)` with spawn velocity
`(0,0,200) cm/s` and lifetime 5 s → expected positions span
`{x in [-30..30], y in [-30..30], z in [0..150] cm}` — far below the
1000 cm saturation threshold. Yet every alive particle's encoded color
reads as `(1,1,1)`.

### What the fix DID accomplish (commit `b3eae87`)

- `MeshRenderModule::CreateGPUBuffers` now actually gets called from
  `ParticleEmitter::Initialize` — `instanceColorBuffer_` is no longer
  invalid and the bindless `instanceColorsUavIndex` no longer falls
  back to descriptor 0.
- The per-instance transform + color buffers are imported into the
  render graph and declared as `pb.Read(..., ShaderResource)` by the
  mesh render pass.
- The mesh-instance CS cbuffer moved from `register(b0)` to
  `register(b1)` (the bindless RS pins slot 0 to RootConstants).
- The dead-particle test rejects both `lifespan <= 0` (unspawned) and
  `age >= lifespan` (decayed).
- The mesh PS reads `PerFrameCB` from `b1` instead of a colliding
  local `VFXMeshLightConstants` block.

### What the fix DID NOT actually fix

**The bindless SRV read of `positionsBuf` (and presumably the other
attribute SRVs — `velocitiesBuf`, `agesBuf`, `lifespansBuf`,
`colorsBuf`) returns garbage data when accessed from inside the
render-graph mesh-instance-CS pass.** The end-of-tick
`UAV -> ShaderResource` barrier added in `frame_vfx_inline.cpp`
(verified to fire via diag log: `barrier_count=7 ws.positions.idx=117`)
is not enough on its own.

### Most likely root causes (ranked by suspicion)

1. **Render graph re-emits a state transition that overrides the
   inline barrier.** `RegisterParticlePasses` imports
   `meshInstanceTransforms` / `meshInstanceCount` /
   `meshIndirectDrawArgs` / `meshInstanceColors` into the RG with a
   `firstCycle ? UAV : ShaderResource` initial state. If the RG
   internally resolves the attribute buffer states from a different
   source (e.g. an internal tracker that thinks they're still in UAV)
   it would emit a redundant `SR -> UAV` transition between the inline
   sim and the mesh CS pass, blowing my barrier away. The attribute
   SRV indices in the mesh-CS CB are valid (37161, 37165, 37167, 37171
   — verified via diag log) but the GPU reads from descriptors that
   point to buffers in the wrong state.
2. **UAV write hazard between spawn/update and the SR transition.**
   The inline sim issues spawn -> UAV barrier -> update -> UAV barrier
   -> indirect args -> indirect-arg-state transition -> end-of-tick
   UAV -> SR. There is no explicit UAV barrier on the attribute
   buffers between the last UAV write and the SR transition. DX12
   requires a UAV-flush barrier before a state-transition only when
   the writes have not yet reached visibility, but the spec is
   ambiguous about whether the SR transition itself acts as an
   implicit flush. NVIDIA drivers historically required an explicit
   UAV barrier first.
3. **Per-emitter dispatch ordering.** The end-of-tick barrier sits
   inside the per-emitter for loop. If a downstream emitter's spawn
   transitions ANOTHER emitter's WriteSet back to UAV (via shared
   bindless RS state), the second emitter's barrier would invalidate
   the first emitter's SR transition. Unlikely with a single emitter
   asset but worth ruling out.
4. **Spawn writes garbage initial position.** Spawn CB carries
   `emitterPos` from `emitter->worldPosition`. `scene_vfx_sync.cpp:253`
   sets `rt->worldPosition = sc->GetPosition()`. The asset emitter is
   at `(0,0,0)` per the demo. But if the `attach.resolved` path fires
   (line 250) and gives a different position, spawned particles would
   be born at that position. NEED to verify by adding a one-shot
   `UZ_LOG` in the spawn CB build site for the asset emitter.

### Prerequisite for closure

Visual A/B with the unmodified asset (`scale: [3.0, 3.0, 3.0]`,
`velocity: [0, 0, 200]`) MUST produce a screenshot showing
**discrete, identifiable mesh particles** in a properly composited
sky+ground scene — not "tetrahedron faces engulfing the camera".
The `tuned-shards.png` and `final-clean.png` from the prior task
look like uniform colored geometry filling the viewport because the
particle positions are runaway garbage, not because the scene is
working correctly.

## What Changes

### Diagnose (instrumentation must be removable / gated)

1. **Add `UZ_VFX_DIAG_ATTR_READBACK` macro** that, when defined, runs a
   per-frame readback of `gpuBuf.WriteSet().positions[0..3]` after the
   end-of-tick SR transition. Logs the float4 values at `Info` level.
   If the read-back values are sane (close to `(0,0,0)`) but the
   in-shader bindless reads return garbage, root cause is render-graph
   state corruption (path 1). If the read-back itself returns garbage,
   root cause is the spawn / update writes (path 4).
2. **Force-emit a UAV barrier on each attribute buffer before the SR
   transition** in `frame_vfx_inline.cpp:1043`. Test under PIX or
   RenderDoc (RenderDoc trace capture preferred — the GPU breadcrumbs
   path on this hardware doesn't show inter-pass state). If the bug
   disappears, root cause was path 2; if it persists, path 1.
3. **Capture a RenderDoc frame** of the demo running
   `mesh_sort_shards.uzvfx`. Inspect the resource state of
   `VFX_MeshInstanceTransforms` and the SoA attribute buffers in the
   Pipeline State viewer right before the `VFX_MeshInstanceCS`
   dispatch. The state visible in PIX is the ground truth — if it
   says `UAV` instead of `ShaderResource`, the render-graph or
   barrier logic is the culprit.
4. **One-shot `UZ_LOG` in the spawn CB build site** dumping
   `emitter->worldPosition` for the asset emitter. Confirms whether
   spawn is being told `(0,0,0)` or some non-zero position by the
   `attach.resolved` path in `scene_vfx_sync.cpp`.

### Fix

5. **Whichever path the diagnosis lands on**, fix surgically:
   - Path 1 (RG state corruption): import the attribute buffers into
     the RG with explicit `Read(ShaderResource)` declarations on the
     mesh-instance-CS pass so the auto-barrier scheduler matches the
     actual state. Risk: `phase0_re-enable-mesh-particle-render-path-rc3`
     specifically REMOVED these imports because they caused TDR on
     first frame. The fix would need to gate the import on
     `firstCycle == false` and rely on the inline sim to manage state
     during the boot frame.
   - Path 2 (UAV write hazard): emit `UAV -> UAV` barrier on each
     attribute buffer before the `UAV -> SR` transition.
   - Path 3 (per-emitter ordering): move the end-of-tick SR transition
     to AFTER the per-emitter loop so all emitters share the same
     transition pass.
   - Path 4 (spawn writes garbage): fix the
     `scene_vfx_sync.cpp:250` attach-resolution path or the
     `worldPosition` init order so asset-driven emitters report
     `(0,0,0)` correctly.

6. **Re-run the visual A/B** with the unmodified
   `mesh_sort_shards.uzvfx` (`scale: [3.0, 3.0, 3.0]`, original
   spawn velocity). Discrete particles MUST be visible in the
   sky+ground scene — not engulfing the camera. Compare against
   `.sandbox/spline-now.png` (180 010 bytes) for the baseline scene
   composition.

7. **Update `docs/engine/25-vfx/88-mesh-particle-render-path.md`
   section 7 "Visibility verified"** with the corrected screenshot.
   Annotate the previous `post-fix-final.png` entry as
   "appeared-correct, was actually camera-engulfing-garbage".

## Impact

- Affected specs: `vfx-particle-mesh-render`,
  `vfx-particle-mesh-visibility` (both already exist, both still
  unsatisfied even though the parent task was archived).
- Affected code: most likely
  `runtime/engine/renderer/frame/frame_vfx_inline.cpp` (barrier
  ordering) AND/OR `runtime/engine/renderer/frame/frame_particles.cpp`
  (RG attribute-buffer imports). Possibly
  `runtime/engine/renderer/scene/scene_vfx_sync.cpp` (worldPosition
  resolution) if path 4 lands.
- Breaking change: NO. CPU surface unchanged.
- User benefit: actually-visible mesh particles for the first time —
  unblocks the deferred `phase16_vfx-sort-pipeline-hardware-validation`
  visual A/B step (item 1.2) and any future feature work that consumes
  visible mesh-particle output (mesh trails, mesh GI, etc.).

## Dependencies

- Builds on `phase0_vfx-mesh-particle-visibility-debug` (archived
  2026-04-30, commit `b3eae87` — leaves the structural pipeline intact
  but the bindless read bug standing).
- Blocks: `phase16_vfx-sort-pipeline-hardware-validation` items 1.2,
  1.3, 1.4 (still blocked — the prior task closure did NOT unblock
  them in practice, only in spec).
- Surfaced by: this task's own predecessor 2026-04-30, after the user
  asked for the viewport to be opened interactively and noticed the
  visual was wrong.
