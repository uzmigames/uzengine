# Phase 4b — 4.18.0 VFX Neighbor Grid 3D (Spatial Hashing)

GPU spatial hash for particle-to-particle interaction. A two-pass compute
pipeline: Pass 1 scatters each particle into a grid cell; Pass 2 for each
particle iterates its 3x3x3 neighbor cells and accumulates forces (Boids
alignment/cohesion/separation, N-body gravity, magnetic repulsion). Enables
flocking, swarming, and repulsion effects.

---

## 1. Grid Data Structures

- [ ] 1.1 Create `runtime/engine/vfx/core/neighbor_grid_3d.h/cpp`. Define:
      ```cpp
      struct NeighborGrid3DConfig {
          Vec3     gridMin     = Vec3(-200.f, -200.f,   0.f); // cm AABB min
          Vec3     gridExtent  = Vec3( 400.f,  400.f, 400.f); // cm AABB size
          uint32_t cellsX      = 32;
          uint32_t cellsY      = 32;
          uint32_t cellsZ      = 32;
          uint32_t maxPerCell  = 8;  // max particles recorded per cell
      };
      ```
      Total grid cells = 32^3 = 32 768. Total cell-list entries =
      `cellsX * cellsY * cellsZ * maxPerCell` = 262 144 uint32 indices
      (1 MB at uint32).
- [ ] 1.2 Allocate GPU buffers:
      - `g_cellCounts`: `RWStructuredBuffer<uint>`, count = `cellsX*cellsY*cellsZ`,
        stride = 4. Cleared to 0 each frame via `ClearUAV` or a clear CS pass.
      - `g_cellParticles`: `RWStructuredBuffer<uint>`, count = `cells*maxPerCell`,
        stride = 4. Stores particle indices per cell slot.
- [ ] 1.3 Expose `NeighborGrid3D::Resize(NeighborGrid3DConfig, IRHIDevice*)` to
      reallocate buffers when config changes.

## 2. Pass 1 — Grid Scatter CS

- [ ] 2.1 Create `runtime/shaders/runtime/vfx/particle_grid_scatter_cs.hlsl`.
      `[numthreads(64,1,1)]`. For each alive particle:
      ```hlsl
      cbuffer GridCB : register(b4) {
          float3 gridMin;    float _p0;
          float3 gridExtent; float _p1;
          uint cellsX; uint cellsY; uint cellsZ; uint maxPerCell;
      };
      RWStructuredBuffer<uint> g_cellCounts   : register(u0);
      RWStructuredBuffer<uint> g_cellParticles: register(u1);
      StructuredBuffer<float3> g_positions    : register(t0); // read SoA
      ```
      Compute cell index:
      ```hlsl
      float3 uvw = (pos - gridMin) / gridExtent;
      if (any(uvw < 0) || any(uvw >= 1)) return; // outside grid
      uint cx = (uint)(uvw.x * cellsX);
      uint cy = (uint)(uvw.y * cellsY);
      uint cz = (uint)(uvw.z * cellsZ);
      uint cellIdx = cz * cellsY * cellsX + cy * cellsX + cx;
      uint slot;
      InterlockedAdd(g_cellCounts[cellIdx], 1, slot);
      if (slot < maxPerCell)
          g_cellParticles[cellIdx * maxPerCell + slot] = particleIdx;
      ```
- [ ] 2.2 Dispatch count: `ceil(aliveCount / 64)` groups. Alive count read from
      the alive-count buffer indirectly.

## 3. Pass 2 — Neighbor Force CS

- [ ] 3.1 Create `runtime/shaders/runtime/vfx/particle_neighbor_force_cs.hlsl`.
      `[numthreads(64,1,1)]`. For each alive particle:
      - Compute own cell coordinates `(cx, cy, cz)`.
      - Iterate over the 27 neighbor cells `(cx±1, cy±1, cz±1)` (skip out-of-bounds).
      - For each neighbor particle in each cell, accumulate forces:
        ```hlsl
        cbuffer NeighborForceCB : register(b5) {
            float separationRadius;   // cm, default 10.0
            float separationStrength; // default 200.0 (cm/s^2)
            float alignmentRadius;    // cm, default 30.0
            float alignmentStrength;  // default 50.0
            float cohesionRadius;     // cm, default 50.0
            float cohesionStrength;   // default 20.0
            uint  forceModeMask;      // bit 0=sep, bit 1=align, bit 2=cohesion
            float _pad;
        };
        ```
      - Separation: `if (dist < separationRadius) force -= dir * (separationRadius-dist) * separationStrength`
      - Alignment: accumulate neighbor velocities, lerp own velocity toward average
      - Cohesion: steer toward center of mass of neighbors within `cohesionRadius`
- [ ] 3.2 Write accumulated velocity delta to `RWStructuredBuffer<float3> g_velDelta : register(u2)`.
      In the standard update CS, after all other forces, add `velDelta[idx] * dt` to velocity.

## 4. CPU Orchestration

- [ ] 4.1 Create `runtime/engine/vfx/modules/neighbor_interaction_module.h/cpp`:
      ```cpp
      struct NeighborInteractionModule : IVFXModule {
          NeighborGrid3DConfig gridConfig;
          float separationRadius   = 10.f;
          float separationStrength = 200.f;
          float alignmentRadius    = 30.f;
          float alignmentStrength  = 50.f;
          float cohesionRadius     = 50.f;
          float cohesionStrength   = 20.f;
          uint32_t forceModeMask   = 0b111; // all three enabled
      };
      ```
- [ ] 4.2 Each frame: dispatch clear CS on `g_cellCounts` (1 group, cellCount/64 threads),
      then dispatch scatter CS, then dispatch neighbor force CS.
- [ ] 4.3 After scatter completes (UAV barrier), dispatch force CS.
      After force CS (UAV barrier), the main update CS can read `g_velDelta`.

## 5. Render Graph Integration

- [ ] 5.1 In `vfx_system.cpp`, add three new passes per emitter when
      `NeighborInteractionModule` is attached:
      - `GridClearPass`  → clears `g_cellCounts` to 0
      - `GridScatterPass` → populates `g_cellParticles`
      - `NeighborForcePass` → writes `g_velDelta`
      All three must execute before `ParticleUpdatePass`.
- [ ] 5.2 Declare UAV barriers in the render graph between each of the three passes.

## 6. Demo Integration

- [ ] 6.1 Add preset "Boids Flock" in `vfx_viewport_demo.cpp`:
      2000 particles, `forceModeMask=0b111`, all default radii/strengths,
      initial velocities randomized in a hemisphere.
- [ ] 6.2 Add preset "Magnetic Repulsion":
      `forceModeMask=0b001` (separation only), `separationRadius=15.0`,
      `separationStrength=500.0`, very dense spawn (5000 particles in 10cm cube).
- [ ] 6.3 ImGui controls: per-force radius and strength sliders, enable/disable
      checkboxes for each force mode.

## 7. Build and Verification

- [ ] 7.1 `cd runtime && zig build --clean && zig build` — no errors.
- [ ] 7.2 `cd demos/vfx_viewport && zig build --clean && zig build`.
- [ ] 7.3 Capture "Boids Flock" at frame 30: verify visible cluster formation
      (particles should aggregate into a loose swarm, not remain uniform).
- [ ] 7.4 Capture "Magnetic Repulsion" at frame 5: verify particles explode
      outward from initial dense cluster.
- [ ] 7.5 Profile dispatch overhead with `UZ_PROFILE_SCOPE("NeighborGridPasses")`
      — 2000 particles should stay under 0.5ms GPU time for all three passes.

## 8. Documentation

- [ ] 8.1 Add to `particle_neighbor_force_cs.hlsl`:
      ```hlsl
      /// @ref Reynolds 1987 "Flocks, Herds, and Schools: A Distributed
      ///      Behavioral Model" — separation, alignment, cohesion rules
      /// @ref Green 2010 "Particle Simulation using CUDA" GDC —
      ///      GPU spatial hashing pattern for N-body simulation
      ```
- [ ] 8.2 Document `maxPerCell` overflow behavior: particles beyond slot
      `maxPerCell-1` are silently dropped from neighbor queries (not from simulation).

## 9. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 9.1 Update or create documentation covering the implementation
- [ ] 9.2 Write tests covering the new behavior
- [ ] 9.3 Run tests and confirm they pass
