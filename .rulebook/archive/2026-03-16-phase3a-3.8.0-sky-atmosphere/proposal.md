# Proposal: Sky & Atmosphere — Physical Sky + Height Fog (Phase 3A)

## Why
The 0.1 POC had a Bruneton-style physical atmosphere with Rayleigh/Mie scattering, configurable planet parameters, and exponential height fog. Currently 0.2 renders a solid clear color behind geometry — no sky, no horizon, no atmospheric perspective. A physical sky is essential for outdoor scenes and dramatically improves visual quality. Height fog adds depth and atmosphere. Together they feed into the IBL pipeline (3.7.0) for dynamic environment lighting that matches the sky.

## What Changes

### New Systems
1. **SkyAtmosphere renderer** — Bruneton physical atmosphere with Rayleigh scattering, Mie scattering, ozone absorption, configurable planet radius and atmosphere height
2. **Sky vertex/pixel shaders** — Fullscreen pass that ray-marches atmosphere scattering, outputs HDR sky color behind scene geometry
3. **SkyAtmosphere component** — Scene data: Rayleigh coefficients, Mie coefficient/anisotropy, scale heights, planet radius, sun illuminance, ground albedo
4. **Exponential height fog** — Post-scene fog pass with configurable density, color, height falloff, base height, max opacity
5. **Sky-to-IBL integration** — Bake sky atmosphere into cubemap for IBL pipeline (irradiance + prefiltered), re-bake on sun direction or atmosphere parameter change

### Refactoring
- Render graph gets new sky pass (after geometry, before post-processing)
- Clear color becomes irrelevant when sky is active (sky fills background)

## Impact
- Affected specs: `docs/engine/12-rendering/06-lighting.md`
- Affected code: `runtime/shaders/atmosphere/`, `runtime/engine/renderer/passes/`
- Breaking change: NO — sky is additive, scenes without SkyAtmosphere keep solid clear color
- User benefit: Photorealistic outdoor environments, time-of-day changes, atmospheric depth
- Enables: Volumetric clouds (3.10.0) composite on top of atmosphere
