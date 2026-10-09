# VFX GPU Particle Core — SoA Buffers, Compute Simulation, Indirect Draw

## Summary

Implement the foundational GPU-driven particle system for UzEngine. This is the core infrastructure on which all VFX features build: GPU particle buffers in Structure-of-Arrays layout, compute shader simulation pipeline, dead/alive particle management via atomic counters (no CPU readback), and indirect draw for variable-count rendering. Inspired by Unreal's Niagara GPU simulation architecture but adapted to UzEngine's RHI and render graph.

## Why

UzEngine currently has zero VFX capabilities. Every modern game engine requires a GPU-driven particle system for effects like fire, smoke, explosions, sparks, rain, snow, and ambient dust. A GPU-driven approach is essential because:

1. **Scale**: CPU particle systems max out at ~10K particles. GPU compute enables 1M+ particles with minimal CPU overhead.
2. **No CPU-GPU sync**: Using indirect draw args written by compute shaders, the CPU never reads back particle counts — eliminating pipeline stalls.
3. **SoA layout**: Structure-of-Arrays is cache-efficient for GPU compute (coalesced memory access).
4. **Foundation**: Every other VFX feature (modules, trails, decals, events) depends on this core.

## UE5 Niagara Reference (Clean-Room)

### Key Patterns to Adapt

1. **FNiagaraGPUInstanceCountManager**: Manages a global GPU buffer of instance counts. Each emitter has a slot in this buffer. Compute shaders atomically increment/decrement counts. DrawIndirect reads from this buffer — CPU never touches particle counts.

2. **Dead/Alive List Pattern**: Niagara uses a free list on GPU. Dead particles append their index to a free list buffer. Spawn reads from free list to recycle indices. This avoids compaction and keeps memory stable.

3. **Double-Buffered Particle Data**: Niagara ping-pongs between two sets of particle attribute buffers. Update reads from buffer A, writes to buffer B. Next frame they swap. This allows safe read-while-write on GPU.

4. **Per-Emitter Dispatch**: Each emitter dispatches its own compute shader. Thread group count = ceil(alive_count / 256). The alive count comes from the GPU counter buffer.

5. **Simulation Stages**: Niagara separates simulation into stages (Spawn → Update → Kill). Each stage is a separate compute dispatch with barriers between them. UzEngine will use render graph passes for automatic barrier management.

### What UzEngine Does Differently

- **JSON-based graphs** instead of Blueprint/visual scripting
- **Render graph integration** for automatic barrier scheduling (Niagara manages barriers manually)
- **Bindless descriptors** (ResourceDescriptorHeap) instead of explicit descriptor tables
- **Simpler hierarchy**: ParticleSystem → Emitter (no System-level aggregation yet)

## Impact

- **New directory**: `runtime/engine/vfx/` — all VFX source code
- **New shaders**: `runtime/shaders/vfx/` — particle compute + render shaders
- **New render passes**: Particle simulation (compute) + particle render (transparent)
- **RHI usage**: Compute dispatch, structured buffers, indirect draw, UAV barriers
- **Performance target**: 100K particles in < 1.0ms total (compute + render)

## Dependencies

- RHI compute shader support (✅ exists)
- Structured buffer + UAV support (✅ exists)
- Indirect draw support (✅ exists)
- Render graph compute pass support (✅ exists — GpuCullPass as reference)
- CB ring allocator (✅ exists)

## Acceptance Criteria

1. GPU particle buffer allocates SoA arrays (position, velocity, age, lifespan, size, color, rotation) as structured buffers
2. Spawn compute shader initializes new particles from free list, respects emission rate
3. Update compute shader applies gravity + damping, advances age
4. Dead particles (age >= lifespan) are recycled into free list via atomic append
5. Indirect draw args buffer is updated by compute shader (no CPU readback)
6. Billboard vertex shader expands particles into camera-facing quads
7. Pixel shader samples particle color and alpha, applies to billboard
8. Render graph integration: simulation pass → barrier → render pass
9. ParticleEmitter ECS component with basic properties (max_particles, emission_rate, lifetime, velocity)
10. Demo: fountain effect — particles spawn upward, fall with gravity, fade out over lifetime
11. Performance: 100K particles renders in < 1.0ms on RTX 3060+
12. All unit tests pass, 95%+ coverage on CPU-side code
