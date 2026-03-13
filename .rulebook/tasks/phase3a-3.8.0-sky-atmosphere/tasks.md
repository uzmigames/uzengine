## 1. Implementation

- [ ] 1.1 Implement sky atmosphere vertex shader (fullscreen triangle, world-space ray direction)
- [ ] 1.2 Implement sky atmosphere pixel shader (Rayleigh + Mie single-scatter ray march)
- [ ] 1.3 Add SkyAtmosphereData struct (Rayleigh coefficients, Mie coefficient/anisotropy, scale heights, planet radius, sun illuminance, ground albedo)
- [ ] 1.4 Implement sky render pass (render behind scene geometry using depth test)
- [ ] 1.5 Add multi-scattering approximation (Hillaire 2020 fast lookup)
- [ ] 1.6 Implement exponential height fog shader (density, color, height falloff, base height, max opacity)
- [ ] 1.7 Add height fog render pass (composited after lighting, before post-processing)
- [ ] 1.8 Implement sky-to-cubemap capture for IBL integration (render sky to 6-face cubemap)
- [ ] 1.9 Register sky and fog passes in render graph with correct ordering
- [ ] 1.10 Create sky atmosphere demo (sun at different angles, fog layer, time-of-day cycle)

## 2. Testing

- [ ] 2.1 Test SkyAtmosphereData struct defaults (Earth-like parameters)
- [ ] 2.2 Test sky shader compilation (both SM6.6 and SM5 paths)
- [ ] 2.3 Test fog attenuation math (exponential falloff, zero at base height with zero density)
- [ ] 2.4 Test sky-to-cubemap output (6 valid faces, HDR values)
- [ ] 2.5 Test render graph pass ordering (sky after geometry, fog after lighting)
- [ ] 2.6 Test fallback (no SkyAtmosphere → solid clear color, no regression)
- [ ] 2.7 Verify 95%+ coverage on all new code

## 3. Documentation

- [ ] 3.1 Add `docs/engine/12-rendering/10-sky-atmosphere.md` with atmosphere model and parameters
- [ ] 3.2 Add fog configuration guide and visual tuning reference
