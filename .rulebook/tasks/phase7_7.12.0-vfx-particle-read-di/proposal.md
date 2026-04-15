# Proposal: phase7_7.12.0-vfx-particle-read-di

## Why
Enables one emitter to read another emitter's particle attributes (position, velocity, color). Required for dependent effects - trails pulled toward a master head particle, shockwaves centered on the N brightest particles, etc. Without this, such effects require CPU readback round-trips. Depends on 7.4.0 persistent IDs for stable cross-emitter references.

## What Changes
Expose ParticleReadDI with ReadParticlePosition(emitterHandle, particleID) returning float3 and batch variants. Implementation shares SoA SRVs across emitters via bindless indices; the reader emitter declares a readEmitters list that gets bound at dispatch time.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/data_interfaces/
- Breaking change: NO
- User benefit: Cross-emitter dependencies without CPU readback (master-follower patterns).

## Source
Reference: UE5 UNiagaraDataInterfaceParticleRead. Depends on: 7.4.0 (persistent IDs).
