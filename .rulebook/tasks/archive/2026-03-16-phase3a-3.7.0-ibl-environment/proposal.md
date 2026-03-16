# Proposal: IBL — Image-Based Lighting & Environment Maps (Phase 3A)

## Why
The 0.1 POC had full IBL with irradiance cubemap convolution, specular prefiltered environment maps, and a BRDF integration LUT — all baked via compute shaders. Current 0.2 uses a flat constant ambient color which produces unrealistic, lifeless surfaces. IBL is the single biggest quality jump for PBR materials: metals reflect the environment, dielectrics pick up subtle color from surroundings, and roughness variation becomes visually meaningful.

## What Changes

### New Systems
1. **BRDF LUT generator** — Compute shader that precomputes the split-sum BRDF integration lookup table (512x512 R16G16), baked once at engine init
2. **Irradiance convolution** — Compute shader convolving HDR environment cubemap into low-frequency irradiance cubemap for diffuse IBL
3. **Prefiltered environment map** — Compute shader generating mip-chain cubemap with increasing roughness blur for specular IBL (GGX importance sampling)
4. **SkyLight component** — Scene component referencing an HDR environment map, triggers IBL bake on load/change
5. **IBL sampling in PBR shader** — Replace constant ambient with irradiance lookup (diffuse) + prefiltered env + BRDF LUT (specular)

### Refactoring
- Ambient color in PerFrameCB augmented with IBL cubemap SRV indices
- mesh_ps.hlsl ambient section rewritten to sample irradiance + prefiltered cubemaps

## Impact
- Affected specs: `docs/engine/12-rendering/06-lighting.md`
- Affected code: `runtime/shaders/`, `runtime/engine/renderer/`, `constant_buffers.h`
- Breaking change: NO — ambient color remains as fallback when no SkyLight present
- User benefit: Photorealistic environment reflections, physically correct ambient lighting
- Enables: Sky atmosphere (3.8.0) can feed into IBL pipeline
