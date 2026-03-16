<!-- doc-meta
type: index
category: rendering
theme: atmosphere
title: Sky & Atmosphere Analysis — Bruneton, SkyLight, IBL
created: 2026-03-16
updated: 2026-03-16
status: complete
author: Claude
-->

# Sky & Atmosphere Analysis — Bruneton, SkyLight, IBL

Complete analysis of UE5 sky rendering, atmospheric scattering (Bruneton 2017), transmittance LUT precomputation, and SkyLight IBL ambient lighting.

## Files

- [sky-architecture-for-uzengine.md](sky-architecture-for-uzengine.md) — UzEngine-specific adaptation of UE5 sky atmosphere system
- [transmittance-lut-analysis-summary.md](transmittance-lut-analysis-summary.md) — Executive summary of transmittance LUT generation, integration, and validation
- [transmittance-lut-index.md](transmittance-lut-index.md) — Index of transmittance LUT concepts: ray marching, media thickness, density parametrization
- [transmittance-lut-quick-reference.md](transmittance-lut-quick-reference.md) — Quick reference for LUT dimensions, UV mapping, sampling formulas
- [ue5-sky-atmosphere-executive-summary.md](ue5-sky-atmosphere-executive-summary.md) — High-level overview of UE5's sky atmosphere component and rendering pipeline
- [ue5-sky-atmosphere-system.md](ue5-sky-atmosphere-system.md) — Complete system architecture: component structure, parameters, compute passes
- [ue5-sky-atmosphere-transmittance-lut.md](ue5-sky-atmosphere-transmittance-lut.md) — Detailed analysis of transmittance LUT: generation, storage format, sampling
- [ue5-sky-atmosphere-uzengine-adaptation.md](ue5-sky-atmosphere-uzengine-adaptation.md) — How to adapt UE5's system to UzEngine architecture (DX12, compute shaders)
- [ue5-skyatmosphere-coordinate-system.md](ue5-skyatmosphere-coordinate-system.md) — Coordinate systems: world cm to km, planet-centric spheres, ray directions
- [ue5-skylight-component-analysis.md](ue5-skylight-component-analysis.md) — SkyLight component: cubemap capture, ambient irradiance, DFAO occlusion, lower hemisphere
- [ue5-skylight-cubemap-capture-ibl.md](ue5-skylight-cubemap-capture-ibl.md) — Image-based lighting (IBL): cubemap capture modes, diffuse/specular contribution, mipmap LOD
- [ue5-skylight-quick-reference.md](ue5-skylight-quick-reference.md) — Quick reference for SkyLight parameters, capture resolution, update frequency
- [ue5-transmittance-lut-hlsl-snippets.md](ue5-transmittance-lut-hlsl-snippets.md) — HLSL code snippets for transmittance LUT generation and sampling

## Quick Navigation

### Understanding Sky Atmosphere from First Principles?
1. [ue5-sky-atmosphere-executive-summary.md](ue5-sky-atmosphere-executive-summary.md) — Start here for overview
2. [ue5-skyatmosphere-coordinate-system.md](ue5-skyatmosphere-coordinate-system.md) — Understand unit systems (cm to km)
3. [ue5-sky-atmosphere-system.md](ue5-sky-atmosphere-system.md) — Full architecture details

### Implementing Transmittance LUT?
- [ue5-sky-atmosphere-transmittance-lut.md](ue5-sky-atmosphere-transmittance-lut.md) — LUT generation and storage
- [ue5-transmittance-lut-hlsl-snippets.md](ue5-transmittance-lut-hlsl-snippets.md) — HLSL implementation
- [transmittance-lut-quick-reference.md](transmittance-lut-quick-reference.md) — Formulas and parameters
- [transmittance-lut-analysis-summary.md](transmittance-lut-analysis-summary.md) — Testing and validation

### Implementing SkyLight IBL?
- [ue5-skylight-component-analysis.md](ue5-skylight-component-analysis.md) — Component overview
- [ue5-skylight-cubemap-capture-ibl.md](ue5-skylight-cubemap-capture-ibl.md) — Cubemap capture and IBL
- [ue5-skylight-quick-reference.md](ue5-skylight-quick-reference.md) — Parameters and modes

### Adapting to UzEngine?
- [sky-architecture-for-uzengine.md](sky-architecture-for-uzengine.md) — Specific adaptation guidance
- [ue5-sky-atmosphere-uzengine-adaptation.md](ue5-sky-atmosphere-uzengine-adaptation.md) — Implementation patterns for DX12

## Key Concepts

### Bruneton 2017 Atmosphere Model
UE5 uses Bruneton's 2017 atmospheric scattering model:
- **Rayleigh scattering** — Blue light from sky (shorter wavelengths scatter more)
- **Mie scattering** — Haze and aerial perspective (longer wavelengths, more directional)
- **Ozone absorption** — UV absorption band in upper atmosphere
- **Transmittance LUT** — Precomputed lookup table for light transmission through atmosphere

### Transmittance LUT
- **Purpose**: Cache light transmission through atmosphere at all altitudes and view angles
- **Dimensions**: Typically 256 × 64 (u=cos zenith angle, v=altitude)
- **Format**: Half-precision RGB (RG16F or RGB10A2)
- **Update**: Once per frame or cached if sky doesn't change
- **Sampling**: Bilinear with special coordinate mapping

### SkyLight Component
- **Captures** the sky dome into a cubemap for IBL ambient lighting
- **Modes**: Real-time capture, static capture, or precomputed texture
- **Irradiance**: Diffuse contribution stored in lower resolution, sampled via normal direction
- **DFAO**: Optional distance field ambient occlusion for ground contact shadows
- **Lower Hemisphere**: Can be overridden with specific color for indoor scenes

### Coordinate Systems
- **World**: Centimeters (UE5 parity)
- **Atmosphere Radius**: Kilometers (bottom=6360 km, top=6420 km by default)
- **Density Profiles**: Scale heights (Rayleigh=8 km, Mie=1.2 km)
- **Ray Direction**: Unit vector from eye toward pixel

## Related Documentation

- **Rendering Overview**: `docs/analysis/rendering/general/`
- **Shadow Mapping**: `docs/analysis/rendering/shadows/`
- **Post-Processing**: `docs/analysis/rendering/post-process/`
- **UzEngine Sky Specs**: `docs/specs/rendering/`
