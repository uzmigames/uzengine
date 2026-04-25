# Proposal: phase16_vfx-particle-sort-pipeline-refactor

## Why

While starting `phase16_vfx-mesh-particle-depth-sort` we discovered the existing
particle sort pipeline is fundamentally broken in three independent ways. None
of these can be fixed by adding a `sortMode` field to `MeshRenderModule` alone —
correct depth-sorted rendering for both sprites and meshes requires a
coordinated refactor across `particle_sort_cs.hlsl`, `gpu_particle_sort.cpp`,
the inline VFX dispatch path, and the mesh instance CS.

1. **The sort pass is disabled engine-wide.** `frame_particles.cpp:224` wraps
   the entire sort dispatch in `if (false)`. Comment: *"particle buffers are
   not tracked by render graph. Will re-enable when ping-pong + proper
   resource imports are restored."* Sprites today claim to depth-sort but
   the pass never runs — `particle_billboard_vs.hlsl` also does not actually
   index through `sortedIndicesSrvIndex` (the field exists in the CB but the
   VS uses raw `instanceID` directly). The field is dead code waiting for
   the dispatch to come back.

2. **The sort CS assumes alive particles occupy slots `[0..aliveCount-1]`.**
   `particle_sort_cs.hlsl` `LocalSortPhase` writes `SORT_KEY_DEAD` for any
   `globalIdx >= particleCount`. But the spawn shader allocates from a
   free-list (`particle_spawn_cs.hlsl:222-233`):
   `InterlockedAdd(freeListHead[0], -1, prevHead); particleIdx = freeList[prevHead - 1];`
   Alive particles end up scattered across `[0..capacity-1]`. The current
   sort therefore assigns garbage keys to alive particles whose slot index
   exceeds aliveCount, and assigns real keys to dead particles whose slot
   index is below aliveCount. This was working only by accident on dense-
   spawn cases, and fails entirely on long-lived emitters.

3. **`VFXMeshInstanceConstants` has a 16-byte mismatch between CPU and HLSL.**
   The HLSL block declares `colorsSrvIndex` and `instanceColorsUavIndex`
   (`particle_mesh_instance_cs.hlsl:108,116`). The CPU struct in
   `frame_particles.cpp` does not. The shader reads zeroed bytes for those
   slots — `instanceColors` writes go through descriptor heap index 0, which
   is whatever happens to be there. Mesh particle vertex colors are
   currently undefined.

The data-model side of the original task (sortMode enum, JSON serialization,
parameter def, mesh instance CS reading `sortedIndices` when valid) was
delivered safely under a `constexpr bool sortDispatched = false` gate in
`frame_particles.cpp` so authors can author `sort_mode: "ViewDepth"` in `.uzfx`
files without breaking rendering. This task flips that gate to `true` after
implementing the rest of the pipeline correctly.

## What Changes

### Sort core (`particle_sort_cs.hlsl` + `gpu_particle_sort.{h,cpp}`)
- Extend `VFXSortConstants` with `agesSrvIndex`, `lifespansSrvIndex`,
  `customSortKeySrvIndex`, `sortMode` (8 + 4 + 4 = +16 bytes → 96 total).
- `LocalSortPhase`: dispatch over `paddedCount = NextPow2(capacity)` (not
  aliveCount). Generate keys for ALL slots `[0..capacity-1]`. Read age and
  lifespan via the new SRVs; `if (age >= lifespan)` → key = `SORT_KEY_DEAD`.
  Slots `[capacity..paddedCount-1]` continue to get `SORT_KEY_DEAD` via the
  existing `globalIdx >= particleCount` branch.
- `GenerateSortKey` gains a `sortMode` switch: `ViewDepth` (current behaviour),
  `CustomAttribute` (reads float from `customSortKeySrvIndex` instead of
  computing depth).
- `gpu_particle_sort.cpp`: dispatch over `paddedCount`, plumb the new SRV
  indices, default `sortMode = ViewDepth` for backward-compat.

### Inline dispatch (`frame_particles.cpp`)
- Move the disabled RG sort pass to inline dispatch (same pattern as
  `DispatchVFXSimulation` in `frame_vfx_inline.cpp`). VFX particle buffers
  are not RG-tracked anyway.
- Schedule sort BEFORE `VFX_MeshInstanceCS` so the indices buffer is populated
  when the mesh CS reads it.
- For sprite emitters with the existing "alpha blended" blend mode, dispatch
  sort too — fixes sprite depth-sort regression.
- Flip `constexpr bool sortDispatched = false;` → `true` in the mesh
  instance CB fill so the foundation laid by
  `phase16_vfx-mesh-particle-depth-sort` activates.

### Sprite VS wiring (`particle_billboard_vs.hlsl`)
- Read `sortedIndicesSrvIndex` and translate `instanceID` → `particleID` via
  the indices buffer. Falls back to identity when the SRV is invalid (back-
  compat for non-sorted emitters).

### Mesh CS slot allocation (`particle_mesh_instance_cs.hlsl`)
- When `sortMode != None` and `sortedIndicesSrvIndex != 0xFFFFFFFF`: write
  the transform to slot `sortedSlot` (deterministic, no atomic). Skip the
  `if (age >= lifespan) return` check (sort already filtered dead).
- Thread 0 writes `aliveCount` directly to indirect args instance count
  (no `InterlockedMax` needed in the sorted path).
- Foundation already in place: the CS already reads `sortedIndices[sortedSlot]`
  to resolve `particleIdx` when sortMode is enabled.

### Mesh CB mismatch (`frame_particles.cpp` + `particle_mesh_instance_cs.hlsl`)
- Add `colorsSrvIndex` and `instanceColorsUavIndex` to the CPU
  `VFXMeshInstanceConstants` struct so it matches the 80-byte HLSL block.
  Wire `colors` via `gpuBuf.WriteSet().colors`. Wire
  `meshInstanceColors` via the existing `MeshRenderModule::instanceColorBuffer_`.
- Static asserts on both sides to prevent future drift.

### Custom attribute sort (`MeshSortMode::CustomAttribute`)
- `MeshRenderModule.customSortAttribute` is already serialized. The CB needs
  to plumb the SRV index of that user-attribute channel to the sort CS.
- `ParticleEmitter::FindUserAttribute(name)` resolver returns the SRV.

### Demo + perf (`demos/vfx_viewport`)
- New sample asset `effects/tests/mesh_sort_shards.uzfx`: 200 transparent
  mesh particles, `sort_mode: "ViewDepth"`, mesh = a translucent crystal
  shard, additive blend. Visual gold standard: meshes blend correctly
  back-to-front from any camera angle.
- Perf test in `tests/engine/vfx`: dispatch sort on 16 384 mesh particles,
  assert GPU timestamp budget < 0.3 ms on RTX 4090.

### Tests
- `test_particle_sort_alive_detection.cpp`: 1024-particle buffer with 50%
  alive at random scattered slots, run the sort, verify the first
  aliveCount entries of `sortedIndices` cover all alive slots and no dead
  slots.
- `test_mesh_sort_deterministic_slots.cpp`: feed known sorted indices to
  the mesh CS, verify instance buffer is identical to the input order.
- `test_mesh_cb_size.cpp`: `static_assert(sizeof(VFXMeshInstanceConstants) ==
  80);` once the mismatch is fixed.

## Impact

- Affected specs: `runtime/engine/vfx/sort-pipeline` (new spec written by
  this task).
- Affected code:
  - `runtime/shaders/runtime/vfx/particle_sort_cs.hlsl`
  - `runtime/shaders/runtime/vfx/particle_billboard_vs.hlsl`
  - `runtime/shaders/runtime/vfx/particle_mesh_instance_cs.hlsl`
  - `runtime/engine/vfx/core/gpu_particle_sort.{h,cpp}`
  - `runtime/engine/renderer/frame/frame_particles.cpp`
  - `runtime/engine/renderer/frame/frame_vfx_inline.cpp` (sort dispatch
    moved here)
  - `runtime/engine/vfx/components/particle_emitter.{h,cpp}`
    (FindUserAttribute resolver)
  - New: `runtime/tests/engine/vfx/test_particle_sort_alive_detection.cpp`
  - New: `runtime/tests/engine/vfx/test_mesh_sort_deterministic_slots.cpp`
  - New: `runtime/tests/engine/vfx/test_mesh_cb_size.cpp`
  - New: `sample/content/vfx/tests/mesh_sort_shards.uzfx`
- Breaking change: NO — default `sortMode = None` preserves current
  behaviour for emitters that don't opt in. Sprite emitters with
  `blendMode = AlphaBlend` automatically gain working depth sort
  (currently they have *no* sort despite the CB claiming so).
- User benefit:
  - Transparent mesh particles (glass shards, energy cores, jelly debris)
    blend correctly back-to-front.
  - Sprite alpha-blended emitters finally depth-sort correctly across the
    whole capacity, not just the first `aliveCount` slots.
  - Mesh particle vertex colors render predictably (CB mismatch fixed).
  - `MeshSortMode::CustomAttribute` enables material-id binning and other
    custom draw-order strategies.

## Dependencies

- Builds on the data-model groundwork from
  `phase16_vfx-mesh-particle-depth-sort` (already archived). That task
  added `MeshSortMode`, `customSortAttribute`, JSON parsing, and the
  CB foundation (`sortMode` + `sortedIndicesSrvIndex` fields) gated by
  `constexpr bool sortDispatched = false`.

## Risks

- Extending `VFXSortConstants` from 80 → 96 bytes is a CB layout change. Any
  consumer that reads the CB without the new fields will see padded bytes
  as zero — safe by construction but warrants a static_assert on size.
- Inline dispatch path bypasses the render graph for VFX. This matches the
  existing inline-sim pattern but means sort buffers are NOT tracked by RG
  for cross-pass barriers. Manual `EmitSortUAVBarriers` (already in
  `gpu_particle_sort.cpp`) is the contract.
- The 0.3 ms perf target is aggressive on integrated GPUs. If unmet on
  baseline hardware, the task adds an `r.VFX.Sort.MaxParticles` CVar that
  falls back to None when the budget would be exceeded.
