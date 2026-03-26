---
name: VFX Render Graph Integration Audit
description: Findings from the 2026-03-25 audit of VFX passes, barriers, missing wiring, and missing features
type: project
---

# VFX Render Graph Integration Audit — 2026-03-25

## Critical: Simulation Pass Is NOT Registered

`frame_particles.cpp::RegisterParticlePasses()` has the simulation pass entirely commented out with a TODO.
The GPU particle simulation (spawn CS, update CS, indirect args CS) is never dispatched inside the render graph.
Pipelines `GetParticleSpawnCS()`, `GetParticleUpdateCS()`, `GetParticleIndirectArgsCS()` do not exist in `PipelineLibrary`.
The render pass draws whatever stale state is in the GPU buffers from the previous frame.

**How to apply:** Before any simulation-related work, the three compute pipelines must be added to `PipelineLibrary` and `RegisterParticlePasses` must be uncommented.

## Barrier Architecture Issue: External Buffers Bypass the Render Graph

All GPUParticleBuffer resources (particle attribute sets, alive count, indirect args, free list) are
"external persistent resources" — they are never imported into `RgBuilder`. As a result:
- The render graph compiler has no knowledge of these resources
- Barriers between simulation (CS) and rendering (VS/DrawIndirect) are emitted MANUALLY inside pass callbacks
- This is fragile: if pass order changes the manually emitted barriers may be wrong or missing

The correct pattern is to `rg.ImportBuffer()` each handle with its current state. The compiler then
schedules all cross-pass barriers automatically.

## Barrier Correctness: Read Set State Assumption

`particle_render_pass.cpp` transitions the read-set buffers from `UnorderedAccess` → `ShaderResource`
inside the execute callback. This relies on the simulation pass having left them in `UAV` state.

Problem: when the simulation pass is commented out (current situation), the read-set was left in
`ShaderResource` from the PREVIOUS frame's render pass. The barrier call would be a UAV→SRV on a
buffer that is already in SRV state — undefined behavior.

## Barrier Correctness: Missing Back-Transition to UAV

After rendering, the read-set attribute buffers are left in `ShaderResource` state. On the next frame,
the simulation write-set (which becomes the read-set after Swap()) needs to be in `ShaderResource` state
for reading, but the previous read-set (which becomes the write-set) needs to be in `UAV` state.

No barrier returns any buffer to `UAV` state after rendering. This is only correct if the simulation
pass transitions write-set buffers from their initial UAV state. With the simulation pass absent,
the state machine is inconsistent after two frames.

## Decal Pass: Not Wired Into the Frame Loop

`AddDecalPass()` and `DecalManager` exist and are implemented, but `RegisterParticlePasses` (or any
frame registration function) never calls `AddDecalPass`. Decals are never rendered.
`decal_manager.cpp` exists but is not reached from any renderer frame path.

## Trail Pass: Not Wired Into the Frame Loop

`AddTrailRenderPass()` is implemented. `trail_system.cpp` is implemented. But no frame registration
function in `runtime/engine/renderer/` calls `AddTrailRenderPass`. Trails are never rendered.

## Distortion / Refraction: Completely Absent

No distortion pass, no separate distortion render target, no refraction vector buffer.
There is a `emissiveIntensity` field in the PS CB but no refraction/distortion capability at all.
The billboard PS does not output to a separate distortion target.

## GPU Particle Sort: Completely Absent

No bitonic sort or radix sort compute shader for particles exists under `runtime/shaders/runtime/vfx/`.
The CPU-side sort in `particle_render_pass.cpp` is a per-emitter (not per-particle) back-to-front sort
using `distanceToCamera` on the `EmitterRenderData` array. Within a single emitter, particles are drawn
in buffer-storage order, not sorted by depth. This causes incorrect alpha blending for dense emitters.

## Soft Particles: Implemented in Shader, NOT Wired in Frame Setup

The billboard VS and PS fully implement soft particle depth fading. However:
- `frame_particles.cpp` sets `softParticleFadeRange = 1.0f` (hardcoded, not configurable per emitter)
- `renderData.sceneDepthSrvIndex = UINT32_MAX` — the scene depth SRV is never populated
- `renderData.readSetSrvIndex = UINT32_MAX` — particle attribute SRVs are never populated
- `ParticleRenderPassDesc::sceneDepthTexture` field (RHITextureHandle) is never set from `m_gbuffer.depthSRV`

Soft particle fading will fail at runtime when the shader tries to sample via index `UINT32_MAX`.

## Async Compute: Not Used

All VFX compute (simulation) runs on the graphics queue (same `IRHICommandList` as geometry).
No separate compute queue, no cross-queue fence or semaphore, no split-frame overlap.

## Mesh Particle Pass: Defined but Not Wired

`particle_mesh_render_pass.h/cpp` is fully implemented. `PipelineLibrary` has no `GetParticleMeshPipeline()`.
No frame registration function calls `AddParticleMeshRenderPass`. Mesh particle emitters never render.

## Static Log Bug

`particle_simulation_pass.cpp` and `particle_render_pass.cpp` each use `static bool loggedOnce = false`
inside a lambda that is re-instantiated every frame. The `static` keyword means it persists correctly
BUT only fires for the first emitter count seen, not per-scene. Minor diagnostic issue, not a correctness bug.

## Per-emitter Sort Distance Is Always Zero

`frame_particles.cpp` sets `renderData.distanceToCamera = 0.0f` for every emitter with a comment
"TODO: use emitter world-space position from transform component". All emitters sort as equal distance.
