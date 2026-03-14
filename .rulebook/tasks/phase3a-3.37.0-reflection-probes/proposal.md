# Reflection Probes

## Why

Currently all reflections come from a single sky IBL cubemap — every surface reflects the same environment regardless of location. In any UE5 scene, reflection captures provide local IBL that varies per-room/area, making metallic and glossy surfaces reflect nearby geometry. P2 gap from the 2026-03-14 gap analysis. Without local probes, interiors and enclosed spaces look wrong on reflective materials.

## What Changes

### New Systems
1. **ReflectionProbeComponent** — ECS component with capture position, influence radius, blend distance, resolution (64-256), priority
2. **ReflectionProbeCapture** — runtime cubemap capture from probe position (render 6 faces), prefilter for roughness mips
3. **ReflectionProbeArray** — TextureCubeArray (or individual cubemaps, max 8-16 probes) uploaded to bindless heap
4. **Probe blending in lighting pass** — per-pixel: find overlapping probes by world position, blend by distance/priority, fallback to sky IBL outside all probes

### Refactoring
- Deferred lighting shader extended to sample from probe array instead of (or blended with) sky IBL
- PerFrameCB extended with active probe count and probe data buffer SRV index

## Impact
- Affected specs: `docs/engine/12-rendering/06-lighting.md`
- Affected code: `runtime/engine/renderer/passes/lighting_pass.*`, `runtime/shaders/deferred_lighting_ps.hlsl`, `runtime/shaders/include/ibl.hlsli`
- Breaking change: NO (sky IBL remains fallback)
- User benefit: Metallic/glossy surfaces reflect local environment, not just sky
- Enables: SSR (3.16.0) can blend with probe reflections for hybrid approach
