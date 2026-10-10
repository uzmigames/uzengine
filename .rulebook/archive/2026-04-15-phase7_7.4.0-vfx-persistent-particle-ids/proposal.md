# Proposal: phase7_7.4.0-vfx-persistent-particle-ids

## Why
Particle indices in an SoA buffer are reused when a slot recycles - there is no stable identity. Niagara's PersistentIDs feature assigns a monotonically increasing 64-bit ID per particle that survives across frames, enabling event payload matching, parent-child tracking, deterministic replay, and debugging. UzEngine has no equivalent, blocking event correlation (7.3.0) and cross-emitter reads (7.12.0).

## What Changes
Add an optional persistentID (uint64) SoA channel, opt-in via ParticleEmitter::emitsPersistentIDs. Spawn CS reserves IDs from a per-emitter atomic counter. Update CS carries IDs through slot recycle (ID cleared on death). Receiver side: events look up the source particle by ID.

## Impact
- Affected specs: docs/engine/25-vfx/17-particle-flipbook-pipeline.md
- Affected code: runtime/engine/vfx/core/gpu_particle_buffer.*, shaders/runtime/vfx/particle_spawn_cs.hlsl, particle_update_cs.hlsl
- Breaking change: NO (opt-in)
- User benefit: Stable particle identity for event correlation, debugging, replay determinism.

## Source
Reference: UE5 FNiagaraEmitter::bRequiresPersistentIDs + FNiagaraGPUInstanceCountManager::AllocatePersistentID.
