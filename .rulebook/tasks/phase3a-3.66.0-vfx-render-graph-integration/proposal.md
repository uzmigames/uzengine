# Proposal: phase3a-3.66.0-vfx-render-graph-integration

## Why
All VFX subsystems (particle simulation, billboard rendering, decals, trails, mesh particles) are implemented but never registered in the render graph. Zero VFX pixels appear on screen. This phase wires every pass into the actual rendering pipeline so the full VFX system becomes visible and functional.

## What Changes
- PipelineLibrary gains compute PSOs for all VFX simulation shaders
- ParticleSimulationPass is activated and connected to render graph with correct UAV/SRV barriers
- GPUParticleBuffer handles are imported into render graph for state tracking
- Billboard render pass receives real depth SRV and attribute SRVs (replacing UINT32_MAX placeholders)
- DecalPass is inserted between GBuffer and lighting passes
- TrailRenderPass is inserted in the transparent phase
- MeshParticleRenderPass is registered for both opaque and transparent mesh particles
- softParticleFadeRange and distanceToCamera are computed correctly per emitter

## Impact
- Affected specs: vfx-gpu-particle-core, vfx-modules-spawn-update-render, vfx-decal-system, vfx-trail-ribbon-renderer, vfx-advanced-modules-lod-budget
- Affected code: runtime/engine/vfx/, runtime/engine/renderer/, runtime/shaders/vfx/
- Breaking change: NO
- User benefit: VFX effects become visible in-game for the first time — particles, decals, trails, and mesh particles all render correctly
