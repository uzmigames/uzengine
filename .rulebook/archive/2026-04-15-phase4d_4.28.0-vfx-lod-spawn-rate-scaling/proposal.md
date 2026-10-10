# Proposal: phase4d_4.28.0-vfx-lod-spawn-rate-scaling

## Why
The existing VFX LOD system (3.65.0) selects discrete LOD levels based on distance, but
doesn't scale the emission rate. A campfire 200m away still spawns the same 500 particles/s
as one 5m away — wasting GPU budget on particles the player can barely see. Niagara's
scalability system reduces spawn rate, max particles, and simulation tick rate based on
significance (distance + screen size). Adding spawn rate scaling to the existing LOD
infrastructure is a high-impact optimization with minimal implementation cost.

## What Changes
- Add `lodSpawnRateScale` float array to `VFXLodSettings` (one scale per LOD level)
- LOD level 0 (closest) = 1.0 (full rate), level 1 = 0.5, level 2 = 0.25, level 3 = 0.1
- `frame_particles.cpp` multiplies `emissionRate × lodSpawnRateScale[currentLod]` before
  feeding the spawn accumulator
- Add `lodMaxParticlesScale` to also reduce the per-emitter particle cap at distance
- Smooth transition between LOD levels (lerp over 0.5s) to avoid pop

## Impact
- Affected specs: particle-system.md (extend LOD section)
- Affected code: vfx_lod.h, particle_emitter.h, frame_particles.cpp
- Breaking change: NO (new optional fields with defaults that preserve current behavior)
- User benefit: 2-4x fewer particles for distant effects, better GPU budget utilization
