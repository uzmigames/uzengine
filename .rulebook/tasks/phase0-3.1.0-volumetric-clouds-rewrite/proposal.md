# Proposal: phase0-3.1.0-volumetric-clouds-rewrite

## Why

Current volumetric clouds suffer from visible banding (uniform stepping without jitter),
flat dark interiors (single-octave ambient approximation instead of multi-scattering),
no ground shadows, and half-res temporal with simple EMA blend. The visual quality gap
vs production cloud renderers (Horizon Zero Dawn, Frostbite, three-geospatial) is
significant. This rewrite adapts techniques from the MIT-licensed
[takram/three-geospatial](https://github.com/takram-design-engineering/three-geospatial/tree/main/packages/clouds)
clouds package and published papers to bring UzEngine clouds to production quality.

## Reference Implementation

- **Source**: https://github.com/takram-design-engineering/three-geospatial/tree/main/packages/clouds
- **License**: MIT (free to study, adapt, reimplement)
- **Analysis**: `docs/analysis/rendering/three-geospatial-clouds-adaptation.md`

## What Changes

### Phase 1: Adaptive Stepping + Blue Noise Jitter (P0 visual quality)
- Replace uniform step size with perspective-scaled adaptive stepping
- Add STBN (Spatio-Temporal Blue Noise) 3D texture for per-pixel jitter
- Add empty space skipping when density < threshold
- New CB fields: minStepSize, maxStepSize, perspectiveStepScale

### Phase 2: Multi-Scattering Octaves (P0 visual quality)
- Replace ambient hack with Oz/Kulla 2017 octave multi-scattering (8 octaves)
- Each octave: attenuate scattering (0.5x), extinction (0.5x), phase eccentricity (0.5x)
- Add sky irradiance term with height-dependent gradient
- Optional: Draine phase function (NVIDIA approx Mie for large particles)

### Phase 3: Quarter-Res Bayer Temporal Upscale (P1 performance)
- Reduce ray march to 1/4 resolution (1/16th pixels per frame)
- 4x4 Bayer pattern cycling over 16 frames
- Variance clipping (3x3 neighborhood in YCoCg) for history validation
- Closest-depth velocity dilation for disocclusion

### Phase 4: Beer Shadow Maps (P0 visual impact)
- New shadow pass: orthographic from sun, 1-3 cascades
- BSM encoding: RGBA16F (frontDepth, meanExtinction, maxOpticalDepth, tailOpticalDepth)
- Temporal AA per cascade with variance clipping
- Integration with deferred_lighting_ps.hlsl for ground shadows
- Vogel disk 16-tap PCF sampling

### Phase 5: Aerial Perspective on Clouds (P2 completeness)
- Apply atmosphere transmittance LUT to cloud radiance
- Compute transmittance-weighted average depth for inscattering
- Integrate with existing Bruneton atmosphere system

### Phase 6: Multi-Layer + Density Profiles (P2 completeness)
- Support up to 4 cloud layers via weather texture RGBA channels
- Per-layer density profile: exp*exp(h) + linear*h + constant
- Per-layer altitude, height, shape/detail erosion, shadow flag
- Cube-sphere UV mapping for globe-scale weather textures

## Impact
- Affected specs: `docs/specs/rendering/atmosphere.md`
- Affected code:
  - MODIFY: `cloud_raymarch_cs.hlsl`, `cloud_temporal_cs.hlsl`, `cloud_pass.h/cpp`, `volumetric_clouds_component.h`
  - MODIFY: `deferred_lighting_ps.hlsl` (cloud shadow sampling)
  - NEW: `cloud_shadow_cs.hlsl`, `cloud_shadow_resolve_cs.hlsl`
  - NEW: STBN 3D texture asset (128x128x64 R8)
- Breaking change: NO (existing component parameters preserved, new ones added)
- User benefit: Production-quality volumetric clouds with ground shadows, 4x performance via temporal upscale

## Key References

| Paper | Year | Technique |
|-------|------|-----------|
| Schneider & Vos, "Real-time Volumetric Cloudscapes of Horizon: Zero Dawn" | 2015 | Noise composition, density model |
| Hillaire, "Physically Based & Unified Volumetric Rendering" (Frostbite) | 2016 | Energy-conserving integration §5.6.3, aerial perspective §5.9.1 |
| Oz & Kulla, "Production Volume Rendering" (Pixar) | 2017 | Multi-scattering octaves |
| Bowles, "Structured Volume Sampling" (volsample) | 2017 | Temporally stable shadow sampling |
| Wrenninge et al., "Oz: The Great and Volumetric" | 2013 | Beer shadow map encoding |
| Peters, "Spatio-Temporal Blue Noise" | 2019 | STBN 3D textures for jitter |
| NVIDIA, "Approximate Mie Scattering (Draine)" | 2018 | Phase function for cloud droplets |
| Bruneton & Neyret, "Precomputed Atmospheric Scattering" | 2008 | Transmittance/inscattering for aerial perspective |
