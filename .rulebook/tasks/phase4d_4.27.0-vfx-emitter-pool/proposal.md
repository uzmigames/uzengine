# Proposal: phase4d_4.27.0-vfx-emitter-pool

## Why
VFX-heavy scenes (explosions, impacts, projectiles) frequently create and destroy emitters.
Each emitter creation allocates 7+ GPU buffers (positions, velocities, ages, etc.) and each
destruction releases them. GPU memory allocation is expensive and causes frame hitches.
Niagara uses `FNiagaraGPUInstanceCountManager` + pooled data buffers to avoid per-emitter
alloc/free. An emitter pool pre-allocates GPU resources and recycles them, turning
create/destroy into O(1) slot assignment instead of GPU heap operations.

## What Changes
- New `VFXEmitterPool` class: pre-allocates N emitter GPU resource "slots"
- `AcquireSlot()` returns a pre-allocated GPUParticleBuffer, `ReleaseSlot()` returns it
- Released slots are reset (alive_count=0, free_list refilled) without GPU deallocation
- VFXSystem routes emitter creation through the pool instead of raw GPUParticleBuffer::Create()
- Configurable pool size (default: 32 slots) with overflow fallback to dynamic allocation

## Impact
- Affected specs: particle-system.md (add emitter pooling section)
- Affected code: vfx_emitter_pool.h/.cpp (new), vfx_system.cpp, gpu_particle_buffer.cpp
- Breaking change: NO (internal optimization, same external API)
- User benefit: Eliminates frame hitches from frequent emitter spawn/destroy
