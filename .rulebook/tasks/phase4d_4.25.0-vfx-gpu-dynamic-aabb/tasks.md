# Phase 4d — 4.25.0 VFX GPU Dynamic AABB Bounds

Compute tight axis-aligned bounding boxes from actual alive particle positions
each frame via a GPU parallel reduction. Replaces the static sphere estimate
in frustum culling with accurate dynamic bounds. Niagara reference:
`FNiagaraBoundsCalculator` + `NiagaraEmitterInstanceBatcher::TickBatch()`.

---

## 1. Bounds Buffer

- [ ] 1.1 Add `RhiBufferHandle boundsBuffer` to `GPUParticleBuffer` — 32 bytes
      (float3 aabbMin + padding, float3 aabbMax + padding). Created with
      UAV + CPU_READ usage for readback.
- [ ] 1.2 Add `RhiBufferHandle boundsReadback` — staging buffer for GPU→CPU copy.
      Created with READBACK heap type.
- [ ] 1.3 Add `EmitterBounds` struct to `particle_emitter.h`:
      `Vec3 aabbMin, aabbMax; bool valid = false; uint32_t frameAge = 0;`.
- [ ] 1.4 Initialize boundsBuffer with extreme values (min=FLT_MAX, max=-FLT_MAX)
      at emitter creation and at the start of each frame before the reduction.

## 2. Bounds Compute Shader

- [ ] 2.1 Create `runtime/shaders/runtime/vfx/particle_bounds_cs.hlsl`:
      `[numthreads(256,1,1)]` — each thread loads one particle position,
      uses groupshared float3 arrays for local min/max reduction (log2 steps),
      group leader writes to UAV via `InterlockedMin/InterlockedMax` on
      asuint(float) values (IEEE 754 float-safe for positive values).
- [ ] 2.2 Handle negative coordinates: use `FloatToSortableUint()` (from
      particle_sort_cs.hlsl) for correct atomic min/max on negative floats.
- [ ] 2.3 Skip dead particles (age >= lifespan) in the reduction.
- [ ] 2.4 Add `BoundsConstants` cbuffer: `uint aliveCount; uint positionsSrvIndex;
      uint boundsUavIndex;`.

## 3. Dispatch Integration

- [ ] 3.1 Add bounds compute dispatch to `frame_particles.cpp` after the update CS
      and before frustum culling. Dispatch `ceil(aliveCount / 256)` groups.
- [ ] 3.2 Add UAV barrier between bounds write and the subsequent CopyResource
      to the readback buffer.
- [ ] 3.3 CopyResource from boundsBuffer → boundsReadback. Map the readback buffer
      and read aabbMin/aabbMax into `EmitterBounds` (1-frame latency is acceptable).
- [ ] 3.4 If aliveCount == 0, skip bounds dispatch and mark bounds invalid.

## 4. Frustum Culling Update

- [ ] 4.1 Update `vfx_frustum_cull.cpp` to use `EmitterBounds.aabbMin/aabbMax` when
      `valid == true`, falling back to the static sphere estimate when `valid == false`
      or `frameAge < 2` (readback not ready yet).
- [ ] 4.2 AABB-vs-frustum test: check all 6 frustum planes against the AABB using
      the standard "positive vertex" algorithm (Gribb & Hartmann 2001).
- [ ] 4.3 Add padding to the AABB (e.g., 10% expansion) to account for 1-frame
      readback delay and particle movement between frames.

## 5. Pipeline Library

- [ ] 5.1 Add `GetParticleBoundsCS()` to `PipelineLibrary` with lazy PSO creation.
- [ ] 5.2 Register `particle_bounds_cs.hlsl` in ShaderManager.

## 6. Debug Visualization

- [ ] 6.1 In debug mode, draw the dynamic AABB as a wireframe box in the stats overlay
      (use existing debug line renderer if available, or log min/max values).
- [ ] 6.2 Add `--vfx-show-bounds` CLI flag to enable AABB visualization.

## 7. Tests

- [ ] 7.1 Unit test: EmitterBounds struct default values (valid=false, FLT_MAX/-FLT_MAX).
- [ ] 7.2 Unit test: AABB-vs-frustum plane test with known inside/outside/intersecting cases.
- [ ] 7.3 Unit test: FloatToSortableUint correctness for negative values.
- [ ] 7.4 Integration test: after spawning particles, boundsBuffer readback contains
      reasonable min/max (within spawn radius of emitter position).

## 8. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 8.1 Update or create documentation covering the implementation
- [ ] 8.2 Write tests covering the new behavior
- [ ] 8.3 Run tests and confirm they pass
