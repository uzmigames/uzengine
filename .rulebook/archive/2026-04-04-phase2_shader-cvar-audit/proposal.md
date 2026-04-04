# Proposal: Shader CVar Audit — Zero Hardcoded Values (Phase 2)

## Why
Audit found 60+ hardcoded values across 145 shaders that should be runtime-configurable CVars. UE5 exposes 1000+ rendering CVars — every shader parameter is configurable without recompilation. UzEngine currently hardcodes sample counts, thresholds, densities, falloffs, and colors directly in HLSL. This prevents artists from tuning rendering quality, makes debugging harder (can't isolate parameters), and blocks quality scalability presets (Low/Medium/High/Ultra).

## What Changes

### Phase 1 — High Priority (20 CVars, 1-2 days)
All values that directly affect visual quality and are frequently tuned:
- SSGI: ray count, step size, max steps, thickness, temporal alpha
- Shadows: PCSS penumbra range, contact shadow steps/thickness, ESM constant
- Bloom: knee softness
- Exposure: speed up/down, low/high percentile, Reinhard key
- God rays: samples, density, weight, decay, exposure
- Material: dielectric F0, min GGX alpha

### Phase 2 — Medium Priority (25 CVars, 2-3 days)
Fine-tuning and denoiser parameters:
- TAA: variance clip gamma, velocity threshold, anti-flicker
- SSGI spatial/temporal denoise: sigma depth/normal/luminance
- SVGF: edge-stopping sigmas
- Atmosphere: ray march steps, Mie asymmetry, ground albedo
- Clouds: density scale, noise scale, erosion, max steps

### Phase 3 — Low Priority (15 CVars, 1-2 days)
Specialist tuning values:
- Cloud FBM: amplitude, decay, lacunarity, octaves
- Atmosphere: sun disk falloff, scale heights, altitude offsets
- SSGI upsample: depth/normal sigma
- TAA: spatial weight, exposure scale

### Architecture
Each CVar flows: `UZ_CVAR_*()` declaration → read per-frame in C++ → write to PerFrameCB field → shader reads from CB. No hardcoded values remain in HLSL.

## Impact
- Affected specs: docs/specs/rendering/shader-library.md, docs/specs/rendering/renderer.md
- Affected code: ALL shaders in runtime/shaders/runtime/, PerFrameCB in constant_buffers.h, frame_constants.cpp
- Breaking change: NO — default values match current hardcoded values exactly
- User benefit: Full runtime control of every rendering parameter, quality presets, artist tuning
- Enables: Quality scalability system, per-platform presets, editor property panels
