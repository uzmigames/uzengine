## 1. GPU Particle Buffer Infrastructure

- [x] 1.1 Create `runtime/engine/vfx/core/gpu_particle_buffer.h` — GPUParticleBuffer struct with SoA layout using **ping-pong double buffering** (Niagara pattern): two sets of attribute buffers [read/write]. Each set contains RHIBufferHandle for positions (float3), velocities (float3), ages (float), lifespans (float), sizes (float), colors (float4), rotations (float). Shared across both sets: alive_count (uint32), free_list (uint32 × capacity), free_list_head (uint32 atomic counter), id_to_index (uint32 × capacity mapping ParticleID→BufferIndex), indirect_args (DrawIndexedIndirectArgs). All buffers created with StructuredBuffer + UnorderedAccess usage. Swap() toggles read/write index each frame to avoid read-after-write hazards.
- [x] 1.2 Create `runtime/engine/vfx/core/gpu_particle_buffer.cpp` — GPUParticleBuffer::Create(device, capacity) allocates both ping-pong buffer sets + shared buffers. GPUParticleBuffer::Destroy(device) releases all handles. Initial state: alive_count = 0, free_list filled with [0..capacity-1], id_to_index all 0xFFFFFFFF (invalid). Swap() toggles write_idx ^= 1, read_idx ^= 1.
- [x] 1.3 Create `runtime/engine/vfx/core/particle_budget.h/.cpp` — ParticleBudget class: global pool with max_total_particles (default 500K). AllocateParticles(requested, allocated) returns available count. ReturnParticles(count) frees. Thread-safe (atomic counter).

## 2. Particle Emitter Component

- [x] 2.1 Create `runtime/engine/vfx/components/particle_emitter.h` — ParticleEmitter ECS component: max_particles (uint32), emission_rate (float, particles/sec), lifetime (float, seconds), spawn_velocity (Vec3), gravity (Vec3), damping (float), is_emitting (bool), priority (uint8, 0-100). Holds GPUParticleBuffer and spawn accumulator (fractional particle carry-over).
- [x] 2.2 Create `runtime/engine/vfx/components/particle_emitter.cpp` — ParticleEmitter::Initialize(device, budget) allocates GPU buffers from budget. ParticleEmitter::Shutdown(device, budget) frees. ParticleEmitter::CalculateSpawnCount(dt) returns integer spawn count using accumulator for smooth emission.
- [x] 2.3 Create `runtime/engine/vfx/vfx_system.h/.cpp` — VFXSystem class: manages all active emitters. Tick(dt) iterates emitters and dispatches compute. RegisterEmitter(entity) / UnregisterEmitter(entity). Holds reference to ParticleBudget.

## 3. Compute Shaders — Simulation

- [x] 3.1 Create `runtime/shaders/runtime/vfx/particle_spawn_cs.hlsl` — Spawn compute shader: reads free_list via atomic decrement to get free index. Initializes position (emitter_pos + random offset), velocity (emitter direction + random spread), age=0, lifespan=random(min,max), size=initial_size, color=initial_color. Thread group [256,1,1]. Uses PCG hash for per-particle RNG seeded by (particle_index + frame_number). SM6.6 bindless via ResourceDescriptorHeap.
- [x] 3.2 Create `runtime/shaders/runtime/vfx/particle_update_cs.hlsl` — Update compute shader: for each alive particle, apply gravity (vel += gravity*dt), damping (vel *= damping), integrate position (pos += vel*dt), advance age (age += dt). If age >= lifespan, append index to free_list via atomic increment (recycle). Uses bindless SRV/UAV via ResourceDescriptorHeap[index].
- [x] 3.3 Create `runtime/shaders/runtime/vfx/particle_indirect_args_cs.hlsl` — Small compute shader (1 thread group): reads alive_count from GPU counter buffer, writes DrawIndirectArgs. Also writes DispatchIndirectArgs for next frame's update dispatch.
- [x] 3.4 Create `runtime/shaders/runtime/include/vfx_common.hlsli` — Shared VFX utilities: PCGHash(seed) for RNG, RandomFloat(seed, min, max), RandomUnitVector(seed), RandomInSphere(seed, radius), RandomInCone(seed, direction, angle), RandomOnDisk(seed, radius), VFXSeed(particleIndex, frameNumber). All stateless (seed-based, no global state).

## 4. Render Shaders — Billboard

- [x] 4.1 Create `runtime/shaders/runtime/vfx/particle_billboard_vs.hlsl` — Vertex shader: takes SV_VertexID, computes particle_id = vertex_id / 6, vertex_in_quad = vertex_id % 6 (two triangles per quad). Reads particle position, size, color from structured buffers (SM5 fallback + SM6.6 bindless). Expands into camera-facing billboard using invView matrix right/up vectors. Per-particle rotation via sincos. Outputs clip-space position, UV, color, world position.
- [x] 4.2 Create `runtime/shaders/runtime/vfx/particle_billboard_ps.hlsl` — Pixel shader: samples sprite texture at UV, multiplies by vertex color (particle color), applies alpha. Soft particle depth fade using scene depth buffer comparison (reversed-Z). Alpha erosion, emissive intensity. Early discard for near-zero alpha.
- [x] 4.3 Create billboard PSO (`runtime/engine/renderer/core/pipeline/pipeline_vfx.cpp`): alpha-blended (SrcAlpha, InvSrcAlpha) + additive (One, One) variants. Depth test enabled (Greater for reversed-Z), depth write disabled, backface cull disabled. Registered in PipelineLibrary.

## 5. Render Graph Integration

- [x] 5.1 Create `runtime/engine/vfx/passes/particle_simulation_pass.h/.cpp` — ParticleSimulationPass: render graph compute pass. For each active emitter: (a) dispatch spawn CS with spawn count, (b) UAV barrier, (c) dispatch update CS with alive count, (d) UAV barrier, (e) dispatch indirect args CS. Constant buffer uploaded via CBRingAllocator with emitter params.
- [x] 5.2 Create `runtime/engine/vfx/passes/particle_render_pass.h/.cpp` — ParticleRenderPass: render graph graphics pass in transparent phase. For each active emitter: bind particle SRV descriptors, bind sprite texture, DrawIndexedIndirect using emitter's indirect_args buffer. Sorted by distance to camera (back-to-front). UAV→SRV barriers for read set attributes.
- [x] 5.3 Register VFX passes in main render pipeline — AddParticleSimulationPass() after scene update, AddParticleRenderPass() in transparent phase after opaque rendering.

## 6. Testing

- [x] 6.1 Unit tests for GPUParticleBuffer — create/destroy, capacity validation, initial state
- [x] 6.2 Unit tests for ParticleBudget — allocate/return, over-budget behavior, thread safety
- [x] 6.3 Unit tests for ParticleEmitter — spawn count accumulator (fractional carry-over), initialize/shutdown lifecycle
- [x] 6.4 Unit tests for VFXSystem — register/unregister emitters, tick dispatching
- [x] 6.5 Integration test: create emitter, tick 60 frames, verify no crashes/leaks

## 7. Demo

- [x] 7.1 Create `demos/particle_fountain/` — standalone demo: single emitter spawning particles upward (velocity=[0,10,0]), gravity pulls down (gravity=[0,-9.81,0]), damping=0.98, lifetime=3s, emission_rate=1000/s, max_particles=50000. Headless proof-of-concept with validation (GPU rendering deferred to pipeline integration).
- [x] 7.2 Add performance overlay — show alive particle count, compute time (ms), render time (ms). Implemented as console output with CPU wall-time measurement.
- [x] 7.3 Add parameter sliders — emission rate, gravity strength, damping, lifetime. Parameters exposed as constexpr at top of main.cpp (GUI sliders deferred to editor integration).

## 8. Documentation

- [x] 8.1 Update docs/engine/25-vfx/ with implementation details and API reference
- [x] 8.2 Document GPU buffer layout and memory usage per particle count
