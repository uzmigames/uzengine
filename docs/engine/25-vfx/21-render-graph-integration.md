<!-- doc-meta
title: VFX Render Graph Integration
category: architecture
engine: uzengine
theme: rendering
created: 2026-03-25
updated: 2026-03-25
status: active
author: docs-operator
-->

## Overview

This document describes how the VFX system integrates with the UzEngine render graph. It covers pass ordering, barrier state management, pipeline PSO registration, and the relationship between particle simulation, rendering, and other render graph subsystems.

## Context

The VFX system uses GPU compute for particle simulation and graphics passes for rendering. These operations are integrated into the render graph framework via `RegisterParticlePasses()` in the scene renderer's frame loop. Understanding the ordering, synchronization, and data flow is essential for implementing new VFX features and debugging rendering issues.

See: [02-particle-system.md](02-particle-system.md), [12-deferred-decal-pipeline.md](12-deferred-decal-pipeline.md), [15-trail-rendering-pipeline.md](15-trail-rendering-pipeline.md)

## Pass Ordering in SceneRenderer::RenderFrame

The VFX passes integrate into the global render graph execution order as follows:

```
1. RegisterShadowPasses
   └─ Shadow atlas generation (all light sources)

2. RegisterGeometryPasses
   └─ GBuffer fill (opaque static/dynamic geometry)

3. RegisterDecalPasses
   └─ Deferred decal projection onto GBuffer
   └─ Generates: GBuffer RT0-3 (RGBA, Normal, Material, Emissive)

4. RegisterLightingPasses
   └─ Deferred lighting resolve (GBuffer → HDR output)
   └─ Generates: HDR scene color

5. RegisterParticlePasses [VFX INTEGRATION]
   ├─ Per-emitter loop:
   │  ├─ VFX_ParticleSimulation (compute)
   │  │  ├─ Spawn dispatch (per-emitter alive count)
   │  │  ├─ UAV barrier (attributes SRV → UAV)
   │  │  ├─ Update dispatch (per-emitter)
   │  │  ├─ Indirect args dispatch (aliveCount → drawIndirect args)
   │  │  └─ IndirectArgument barrier
   │  └─ Continue to next emitter
   ├─ VFX_ParticleBillboardRender (graphics, transparent)
   │  └─ Back-to-front sorted billboard quads (DrawIndirect)
   ├─ VFX_TrailRender (graphics, transparent)
   │  └─ Back-to-front sorted ribbon trails (DrawIndexed)
   ├─ VFX_ParticleMeshRender (graphics, opaque/transparent)
   │  └─ Instanced mesh particles (DrawIndexedIndirect) [not yet active]
   └─ Ping-pong swap: read set → write set for next frame

6. RegisterPostProcessPasses
   └─ TAA, bloom, tonemap, FXAA

7. RegisterOverlayPasses
   └─ Debug overlay, UI
```

### Registration Source

- **Frame loop**: `runtime/engine/renderer/frame/frame_particles.cpp::RegisterParticlePasses()`
- **Geometry/decals**: `runtime/engine/renderer/frame/frame_geometry.cpp`, `frame_decals.cpp`
- **Lighting**: `runtime/engine/renderer/frame/frame_lighting.cpp`

## Barrier State Machine

The particle system uses a ping-pong double-buffer architecture with explicit barriers to transition between states.

### Per-Frame Lifecycle

#### Frame N Render (Simulation)
1. **Transition phase** (start of `RegisterParticlePasses`):
   - Call `TransitionWriteSetToUAV()`: All 7 particle attribute buffers + aliveCount shift from SRV → UAV
   - Call `TransitionIndirectArgsToUAV()`: Indirect args buffers shift from IndirectArgument → UAV

2. **Spawn dispatch**:
   - Read RNG seed, spawn parameters (CB)
   - Write to `writeSet.positions`, `writeSet.velocities`, etc. (UAV)
   - Emit `EmitSimulationUAVBarriers()` after spawn (UAV → UAV sync)

3. **Update dispatch**:
   - Read `writeSet.*` (UAV), apply forces, gravity, collision
   - Write to `writeSet.*` (UAV)
   - Emit `EmitSimulationUAVBarriers()` after update

4. **Indirect args dispatch**:
   - Read `writeSet.aliveCount` (UAV)
   - Write `indirectArgsBuffer` (DrawIndirect args, UAV)
   - Emit `EmitIndirectArgsBarriers()` after dispatch (UAV → IndirectArgument)

5. **Render phase**:
   - Billboard/Trail/Mesh passes read `readSet.*` (SRV) and render with indirect args

6. **Ping-pong swap** (end of `RegisterParticlePasses`):
   - `readSet` ← `writeSet` (swap for next frame)

#### Frame N+1 Simulation (repeat from step 1)

### Barrier Operations

All barrier operations are declared in `GPUParticleBuffer`:

```cpp
/// Transition all 7 particle attribute buffers from SRV to UAV
void TransitionWriteSetToUAV(RenderGraph& graph);

/// Transition indirect args buffer from IndirectArgument to UAV
void TransitionIndirectArgsToUAV(RenderGraph& graph);

/// UAV barrier between dispatches (same resource read/written)
void EmitSimulationUAVBarriers(RenderGraph& graph);

/// IndirectArgument barrier after indirect args generation
void EmitIndirectArgsBarriers(RenderGraph& graph);
```

**Key point**: The `readSet` is never transitioned (remains SRV for the full frame). Only the `writeSet` is transitioned to UAV during simulation, then back to SRV implicitly at frame swap.

## Pipeline Library PSOs

The VFX system registers the following compute and graphics PSOs in the pipeline library:

### Compute Pipelines

| PSO Name | Source File | Purpose |
|----------|-------------|---------|
| `particle_spawn_cs` | `pipeline_vfx.cpp` | Spawn compute shader: lifetime init, position randomization |
| `particle_update_cs` | `pipeline_vfx.cpp` | Update compute shader: physics, forces, lifetime decay |
| `particle_indirect_args_cs` | `pipeline_vfx.cpp` | Generate DrawIndirect/DispatchIndirect args from alive count |
| `particle_mesh_instance_cs` | `pipeline_vfx.cpp` | Transform instance transforms (position, rotation) for mesh particles |

### Graphics Pipelines

| PSO Name | Source File | Purpose | Blend Mode |
|----------|-------------|---------|-----------|
| `particle_billboard_ps` | `pipeline_vfx.cpp` | Billboard particle rendering (quad per particle) | Alpha blend |
| `trail_ribbon_ps` | `pipeline_vfx.cpp` | Trail ribbon rendering (line-strip ribbons) | Alpha blend |
| `particle_mesh_opaque` | `pipeline_vfx.cpp` | Mesh particle rendering (instanced mesh) | Opaque |
| `particle_mesh_transparent` | `pipeline_vfx.cpp` | Mesh particle rendering with transparency | Alpha blend |

### Decal Pipeline (Related)

| PSO Name | Source File | Purpose |
|----------|-------------|---------|
| `decal_deferred_ps` | `pipeline_decals.cpp` | Deferred decal projection (4 GBuffer RTs) |

**Accessor methods** (in `PipelineLibrary`):

```cpp
RHIComputePipeline GetParticleSpawnCS();
RHIComputePipeline GetParticleUpdateCS();
RHIComputePipeline GetParticleIndirectArgsCS();
RHIComputePipeline GetParticleMeshInstanceCS();
RHIGraphicsPipeline GetParticleBillboard();
RHIGraphicsPipeline GetParticleMeshOpaque();
RHIGraphicsPipeline GetParticleMeshTransparent();
RHIGraphicsPipeline GetDecalDeferred();
RHIGraphicsPipeline GetTrailRibbon();
```

**Registration source**: `runtime/engine/renderer/core/pipeline/pipeline_vfx.cpp`

## Bindless Descriptor Status

All VFX passes currently use **legacy descriptor heap binding** (per-dispatch SRV/UAV binding via `RenderGraph::AddResourceDependency`).

### Current State

- `GPUParticleBuffer` exposes raw `RHIBufferHandle` members:
  - `positionBuffer`, `velocityBuffer`, `colorBuffer`, `rotationBuffer`, `scaleBuffer`, `ageBuffer`, `typeBuffer`
  - `aliveCountBuffer`, `indirectArgsBuffer`
- Descriptor heap indices are **NOT YET ASSIGNED** (UINT32_MAX placeholders)
- Render graph tracks SRV/UAV transitions; descriptor binding is implicit via `RenderGraph::AddResourceDependency`

### Bindless Wiring (Pending — Phase 3.67.0)

When descriptor heaps are fully integrated:
1. Assign persistent descriptor heap indices to each buffer at `GPUParticleBuffer::Init()`
2. Update compute/graphics shaders to read indices from root constants or root descriptors
3. Replace `RenderGraph::AddResourceDependency` with root constant SRV/UAV index bindings
4. Update barrier logic to track descriptor heap state instead of resource state

See: [20-light-particles.md](20-light-particles.md) for similar descriptor indexing patterns.

## Key Source Files

### Render Graph Integration

| File | Purpose |
|------|---------|
| `runtime/engine/renderer/frame/frame_particles.cpp` | `RegisterParticlePasses()` — pass registration, per-emitter loop |
| `runtime/engine/renderer/frame/frame_decals.cpp` | `RegisterDecalPasses()` — decal integration |

### VFX Core

| File | Purpose |
|------|---------|
| `runtime/engine/vfx/gpu_particle_buffer.h` | Buffer lifecycle, ping-pong swap, barrier operations |
| `runtime/engine/vfx/passes/particle_simulation_pass.cpp` | Spawn, update, indirect args dispatches |
| `runtime/engine/vfx/passes/particle_render_pass.cpp` | Billboard rendering with DrawIndirect |
| `runtime/engine/vfx/passes/trail_render_pass.cpp` | Trail rendering with indexed geometry |
| `runtime/engine/vfx/passes/particle_mesh_render_pass.cpp` | Mesh particle rendering (instanced or indirect) |
| `runtime/engine/vfx/gpu_particle_emitter.h/.cpp` | Emitter state, lifetime, parameters |

### Pipeline & Shaders

| File | Purpose |
|------|---------|
| `runtime/engine/renderer/core/pipeline/pipeline_vfx.cpp` | VFX PSO creation and registration |
| `runtime/shaders/vfx/particle_spawn_cs.hlsl` | Spawn compute shader |
| `runtime/shaders/vfx/particle_update_cs.hlsl` | Update compute shader |
| `runtime/shaders/vfx/particle_indirect_args_cs.hlsl` | Indirect args generation |
| `runtime/shaders/vfx/particle_billboard_ps.hlsl` | Billboard rendering pixel shader |
| `runtime/shaders/vfx/trail_ribbon_ps.hlsl` | Trail rendering pixel shader |

## Data Flow: Simulation → Render

```
[Frame N Simulation Start]
  ↓
readSet (SRV) ─┐
  ↓ [swap]     │
writeSet (now renderable)
  ↓            │
TransitionWriteSetToUAV()
  ↓
Spawn CS (reads RNG, writes spawn params)
  ↓
UAV Barrier
  ↓
Update CS (reads/writes writeSet)
  ↓
UAV Barrier
  ↓
IndirectArgs CS (reads alive count, writes draw args)
  ↓
IndirectArgument Barrier
  ↓
[Frame N Render Phase]
  ↓
Billboard/Trail/Mesh PS (reads readSet SRV, uses indirect args)
  ↓ [end of RegisterParticlePasses]
Ping-Pong Swap (writeSet ← readSet)
  ↓
[Frame N+1 Simulation Start] ───→ repeat
```

## Debugging VFX Render Graph Issues

### Common Problems

1. **No particles visible** → Check pass ordering. Ensure `RegisterParticlePasses()` runs AFTER `RegisterLightingPasses()` (particles render on top of lit scene).

2. **Particles flicker or disappear** → Likely a barrier issue. Verify `EmitSimulationUAVBarriers()` is called after spawn and update. Check that readSet remains SRV throughout render phase.

3. **Particle mesh instancing not rendering** → `VFX_ParticleMeshRender` is not yet active. Verify `particle_mesh_instance_cs` is invoked before mesh render pass.

4. **RenderDoc shows empty buffers** → Ping-pong swap may not have executed. Verify `GPUParticleBuffer::SwapBuffers()` is called at end of frame.

### GPU Debugging

Use the buffer visualization grid to inspect particle state:

```bash
# Capture particle buffer state (requires custom debug pass)
demo.exe --headless --screenshot-stdout --screenshot-frame 5 --frames 8
```

RenderDoc integration:
```bash
renderdoccmd capture -c particle_debug.rdc demo.exe --frames 10
qrenderdoc --python analyze_particles.py particle_debug.rdc
```

## Key Takeaways

- VFX passes execute after deferred lighting in `RegisterParticlePasses()`
- Particle simulation uses a **ping-pong double-buffer** with explicit barrier transitions
- The **read set remains SRV** for the entire frame; only the **write set transitions to UAV** during simulation
- Barriers must be emitted between spawn/update/indirectargs dispatches
- All PSOs are registered in `pipeline_vfx.cpp` and accessed via `PipelineLibrary` methods
- Bindless descriptor indexing is pending; current implementation uses legacy `RenderGraph::AddResourceDependency`

## References

- [02-particle-system.md](02-particle-system.md) — GPU particle simulation details
- [12-deferred-decal-pipeline.md](12-deferred-decal-pipeline.md) — Decal render pass integration
- [15-trail-rendering-pipeline.md](15-trail-rendering-pipeline.md) — Trail rendering pipeline
- [19-mesh-particle-rendering.md](19-mesh-particle-rendering.md) — Mesh particle rendering
- [20-light-particles.md](20-light-particles.md) — Light particle integration (descriptor heap patterns)

## Changelog

| Date | Change |
|------|--------|
| 2026-03-25 | Initial creation: render graph pass ordering, barrier state machine, PSO registration, bindless status |

