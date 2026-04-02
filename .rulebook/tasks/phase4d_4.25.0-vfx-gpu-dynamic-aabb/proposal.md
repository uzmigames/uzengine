# Proposal: phase4d_4.25.0-vfx-gpu-dynamic-aabb

## Why
The current frustum culling (3.89.0) uses a static bounding sphere estimated from
`spawnRadius + maxSpeed * maxLifetime`. This over-estimates bounds for most effects
and under-estimates for effects with external forces (attractors, wind, vortex).
Niagara computes dynamic bounds from actual alive particle positions each frame via
a GPU reduction pass. Accurate bounds enable: tighter frustum culling, correct
shadow caster bounds, and future occlusion culling integration.

## What Changes
- New compute shader `particle_bounds_cs.hlsl`: parallel min/max reduction over alive
  particle positions using groupshared memory + atomic InterlockedMin/Max
- Per-emitter `BoundsBuffer` (6 floats: AABB min + max) with CPU readback (1-frame delay)
- `EmitterBounds` struct on CPU side, updated from readback each frame
- Frustum culling (3.89.0) updated to use dynamic AABB instead of static sphere
- Fallback to static sphere estimate when no alive particles or readback not ready

## Impact
- Affected specs: particle-system.md (add dynamic bounds section)
- Affected code: gpu_particle_buffer.h, particle_bounds_cs.hlsl (new), frame_particles.cpp, vfx_frustum_cull.cpp
- Breaking change: NO (enhances existing frustum culling)
- User benefit: Tighter culling = fewer wasted draw calls, correct shadow bounds
