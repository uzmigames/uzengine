# VFX GPU Particle Core — Specification

## ADDED Requirements

### Requirement: GPU Particle Buffer (SoA Layout with Ping-Pong)
The VFX system SHALL store particle attributes in Structure-of-Arrays layout on GPU using a **double-buffered (ping-pong) pattern** (adapted from Niagara's FNiagaraDataBuffer). Each frame, compute reads from buffer set A and writes to buffer set B, then they swap. This avoids read-after-write hazards on GPU.

#### Scenario: Buffer Creation
Given a GPUParticleBuffer with capacity 50000
When Create(device, 50000) is called
Then 2 sets of 7 structured buffers SHALL be allocated (positions float3, velocities float3, ages float, lifespans float, sizes float, colors float4, rotations float) — one read set, one write set
And 1 indirect args buffer SHALL be allocated (5 × uint32) (shared)
And 1 free list buffer SHALL be allocated (capacity × uint32) (shared)
And 1 free list head counter buffer SHALL be allocated (1 × uint32, atomic) (shared)
And 1 id_to_index mapping buffer SHALL be allocated (capacity × uint32) (shared, maps ParticleID→BufferIndex)
And total GPU memory SHALL be approximately capacity × 104 bytes (2 × 52 bytes per ping-pong set)

#### Scenario: Ping-Pong Swap
Given a GPUParticleBuffer after a simulation frame
When Swap() is called
Then the read buffer set SHALL become the write target
And the write buffer set SHALL become the read source
And the renderer SHALL read from the newly completed write set

#### Scenario: Buffer Destruction
Given an allocated GPUParticleBuffer
When Destroy(device) is called
Then all RHIBufferHandle resources SHALL be released
And all handles SHALL be invalidated (IsValid() == false)

#### Scenario: Initial State
Given a newly created GPUParticleBuffer
When initialization completes
Then alive_count SHALL be 0
And free_list SHALL contain indices [0, 1, 2, ..., capacity-1]
And indirect_args.instanceCount SHALL be 0

### Requirement: Particle Budget System
The VFX system SHALL enforce a global particle budget that limits total GPU particle allocations across all active emitters.

#### Scenario: Budget Allocation Within Limits
Given a ParticleBudget with max_total_particles=500000 and particles_used=100000
When AllocateParticles(50000, &allocated) is called
Then allocated SHALL be 50000
And particles_used SHALL be 150000
And the method SHALL return true

#### Scenario: Budget Allocation Over Limit
Given a ParticleBudget with max_total_particles=500000 and particles_used=480000
When AllocateParticles(50000, &allocated) is called
Then allocated SHALL be 20000 (remaining available)
And particles_used SHALL be 500000
And the method SHALL return true

#### Scenario: Budget Fully Exhausted
Given a ParticleBudget with max_total_particles=500000 and particles_used=500000
When AllocateParticles(1000, &allocated) is called
Then allocated SHALL be 0
And the method SHALL return false

### Requirement: Particle Spawn via Free List
The spawn compute shader SHALL use an atomic decrement on the free list counter to acquire particle indices, avoiding CPU-GPU synchronization.

#### Scenario: Spawn New Particles
Given an emitter with emission_rate=1000 and dt=0.016s (60fps)
When the spawn compute shader dispatches
Then approximately 16 particles SHALL be spawned this frame
And each spawned particle SHALL acquire its index from the free list via InterlockedDecrement
And spawned particles SHALL have age=0, random lifespan within [lifespan_min, lifespan_max]
And spawn velocity SHALL be randomized within the emitter's spread cone angle

#### Scenario: Fractional Spawn Accumulator
Given an emitter with emission_rate=10 and dt=0.016s
When the system ticks for 7 consecutive frames
Then the CPU accumulator SHALL carry fractional particles across frames
And total spawned particles over 7 frames SHALL be approximately 1 (10 × 0.016 × 7 ≈ 1.12)

### Requirement: Particle Update Compute Shader
The update compute shader SHALL process all alive particles in parallel, applying physics (gravity, damping), advancing age, and recycling dead particles.

#### Scenario: Physics Update
Given an alive particle with velocity=[5,0,0] and gravity=[0,-9.81,0] and damping=0.98 and dt=0.016
When the update compute shader processes this particle
Then velocity SHALL become approximately [4.9, -0.157, 0] (vel = (vel + gravity*dt) * damping)
And position SHALL be advanced by vel*dt

#### Scenario: Particle Death and Recycling
Given an alive particle with age=4.99 and lifespan=5.0 and dt=0.016
When the update compute shader processes this particle
Then age SHALL exceed lifespan (5.006 >= 5.0)
And the particle index SHALL be appended to the free list via InterlockedIncrement
And alive_count SHALL be decremented atomically

### Requirement: Indirect Draw Without CPU Readback
The VFX system SHALL use GPU-written indirect draw arguments so the CPU never reads back particle counts.

#### Scenario: Indirect Args Update
Given 5000 alive particles after the update compute pass
When the indirect args compute shader dispatches (1 thread)
Then indirect_args.vertexCountPerInstance SHALL be 6 (two triangles per billboard quad)
And indirect_args.instanceCount SHALL be 5000 (current alive count from GPU counter)
And the CPU SHALL NOT read or query the alive count

### Requirement: Billboard Rendering
Particle rendering SHALL expand each particle into a camera-facing billboard quad using the vertex shader and SV_VertexID.

#### Scenario: Billboard Expansion
Given a particle at position=[10,5,0] with size=2.0
When the vertex shader processes vertex_id=24 (particle_id=4, vertex_in_quad=0)
Then the vertex SHALL be expanded into a 2.0×2.0 billboard facing the camera
And the billboard SHALL use the view matrix right and up vectors for orientation

#### Scenario: Transparent Rendering
Given billboard rendering is active
When the PSO is configured
Then blend mode SHALL be SrcAlpha/OneMinusSrcAlpha (premultiplied alpha)
And depth test SHALL be enabled (read depth buffer)
And depth write SHALL be disabled (transparent objects don't write depth)
And backface culling SHALL be disabled

### Requirement: Render Graph Integration
VFX compute and render passes SHALL be registered in the render graph with proper resource dependencies for automatic barrier scheduling.

#### Scenario: Simulation Pass Scheduling
Given the render graph is building
When AddParticleSimulationPass() is called
Then compute passes SHALL be scheduled: spawn → UAV barrier → update → UAV barrier → indirect args
And all particle buffers SHALL be declared as UAV writes

#### Scenario: Render Pass Scheduling
Given the simulation pass has completed
When AddParticleRenderPass() is called in the transparent phase
Then particle buffers SHALL be transitioned from UAV to SRV state
And the render graph SHALL schedule this after all opaque passes

### Requirement: ParticleEmitter ECS Component
ParticleEmitter SHALL be an ECS component attachable to entities, with configurable emission parameters.

#### Scenario: Emitter Initialization
Given an entity with a ParticleEmitter component
When Initialize(device, budget) is called
Then GPU buffers SHALL be allocated from the particle budget
And the emitter SHALL be registered with VFXSystem

#### Scenario: Emitter Shutdown
Given an active emitter
When Shutdown(device, budget) is called
Then GPU buffers SHALL be released
And budget allocation SHALL be returned
And the emitter SHALL be unregistered from VFXSystem

### Requirement: Performance Target
The GPU particle system SHALL render 100K particles in under 1.0ms total (compute simulation + billboard rendering) on an NVIDIA RTX 3060 or equivalent.

#### Scenario: Performance Benchmark
Given 100000 active particles with gravity and damping
When one frame is rendered
Then compute simulation time SHALL be under 0.5ms
And billboard render time SHALL be under 0.5ms
And total VFX frame time SHALL be under 1.0ms
