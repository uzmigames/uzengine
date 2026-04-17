<!-- doc-meta
type: index
category: analysis
theme: atmosphere
title: Sky & Atmosphere Analysis — Bruneton, SkyLight, IBL
created: 2026-03-19
updated: 2026-03-19
status: complete
author: Claude
-->

# Sky & Atmosphere Analysis

Complete analysis of UE5 sky rendering, atmospheric scattering (Bruneton 2017), transmittance LUT precomputation, height fog integration, and SkyLight IBL ambient lighting.

## Files

- [fog-line-number-reference.md](fog-line-number-reference.md) — Exact line numbers for fog parameters in UE5 source, useful for rapid implementation reference
- [sky-atmosphere-fog-index.md](sky-atmosphere-fog-index.md) — Index of key concepts: ray marching, media density, transmittance, fog layers
- [ue5-exponential-height-fog-defaults.md](ue5-exponential-height-fog-defaults.md) — Default fog parameter values from UE5, fog density profiles
- [ue5-fog-cpp-data-flow.md](ue5-fog-cpp-data-flow.md) — C++ data structures for fog, FExponentialHeightFog analysis, cbuffer layout
- [ue5-fog-shader-code-reference.md](ue5-fog-shader-code-reference.md) — HLSL shader code snippets from UE5 fog rendering pipeline
- [ue5-fog-visual-breakdown.md](ue5-fog-visual-breakdown.md) — Visual breakdown of fog components: directional inscattering, skylight injection, depth-based blending
- [ue5/README.md](ue5/README.md) — Sky atmosphere detailed analysis entry point
- [ue5/sky-architecture-for-uzengine.md](ue5/sky-architecture-for-uzengine.md) — UzEngine-specific adaptation of UE5 sky atmosphere system
- [ue5/ue5-sky-atmosphere-executive-summary.md](ue5/ue5-sky-atmosphere-executive-summary.md) — High-level overview of UE5 sky atmosphere component and rendering pipeline
- [ue5/ue5-sky-atmosphere-system.md](ue5/ue5-sky-atmosphere-system.md) — Complete system architecture: component structure, parameters, compute passes
- [ue5/ue5-sky-atmosphere-transmittance-lut.md](ue5/ue5-sky-atmosphere-transmittance-lut.md) — Detailed analysis of transmittance LUT: generation, storage format, sampling
- [ue5/ue5-skyatmosphere-coordinate-system.md](ue5/ue5-skyatmosphere-coordinate-system.md) — Coordinate systems: world cm to km, planet-centric spheres, ray directions
- [ue5/ue5-skylight-component-analysis.md](ue5/ue5-skylight-component-analysis.md) — SkyLight component: cubemap capture, ambient irradiance, DFAO occlusion, lower hemisphere
- [ue5/ue5-skylight-cubemap-capture-ibl.md](ue5/ue5-skylight-cubemap-capture-ibl.md) — Image-based lighting (IBL): cubemap capture modes, diffuse/specular contribution, mipmap LOD
- [ue5/ue5-skylight-quick-reference.md](ue5/ue5-skylight-quick-reference.md) — Quick reference for SkyLight parameters, capture resolution, update frequency
- [ue5/ue5-transmittance-lut-hlsl-snippets.md](ue5/ue5-transmittance-lut-hlsl-snippets.md) — HLSL code snippets for transmittance LUT generation and sampling
- [ue5/transmittance-lut-analysis-summary.md](ue5/transmittance-lut-analysis-summary.md) — Executive summary of transmittance LUT generation, integration, and validation
- [ue5/transmittance-lut-index.md](ue5/transmittance-lut-index.md) — Index of transmittance LUT concepts: ray marching, media thickness, density parametrization
- [ue5/transmittance-lut-quick-reference.md](ue5/transmittance-lut-quick-reference.md) — Quick reference for LUT dimensions, UV mapping, sampling formulas

## Quick Navigation

### Understanding Sky Atmosphere from First Principles?
1. [ue5/ue5-sky-atmosphere-executive-summary.md](ue5/ue5-sky-atmosphere-executive-summary.md) — Start here for overview
2. [ue5/ue5-skyatmosphere-coordinate-system.md](ue5/ue5-skyatmosphere-coordinate-system.md) — Understand unit systems (cm to km)
3. [ue5/ue5-sky-atmosphere-system.md](ue5/ue5-sky-atmosphere-system.md) — Full architecture details

### Implementing Transmittance LUT?
- [ue5/ue5-sky-atmosphere-transmittance-lut.md](ue5/ue5-sky-atmosphere-transmittance-lut.md) — LUT generation and storage
- [ue5/ue5-transmittance-lut-hlsl-snippets.md](ue5/ue5-transmittance-lut-hlsl-snippets.md) — HLSL implementation
- [ue5/transmittance-lut-quick-reference.md](ue5/transmittance-lut-quick-reference.md) — Formulas and parameters
- [ue5/transmittance-lut-analysis-summary.md](ue5/transmittance-lut-analysis-summary.md) — Testing and validation

### Implementing Height Fog?
- [ue5-exponential-height-fog-defaults.md](ue5-exponential-height-fog-defaults.md) — Default parameters
- [ue5-fog-cpp-data-flow.md](ue5-fog-cpp-data-flow.md) — C++ data structures
- [ue5-fog-shader-code-reference.md](ue5-fog-shader-code-reference.md) — Shader implementation
- [fog-line-number-reference.md](fog-line-number-reference.md) — Source file line numbers

### Implementing SkyLight IBL?
- [ue5/ue5-skylight-component-analysis.md](ue5/ue5-skylight-component-analysis.md) — Component overview
- [ue5/ue5-skylight-cubemap-capture-ibl.md](ue5/ue5-skylight-cubemap-capture-ibl.md) — Cubemap capture and IBL
- [ue5/ue5-skylight-quick-reference.md](ue5/ue5-skylight-quick-reference.md) — Parameters and modes

### Adapting to UzEngine?
- [ue5/sky-architecture-for-uzengine.md](ue5/sky-architecture-for-uzengine.md) — Specific adaptation guidance

## Key Concepts

### Bruneton 2017 Atmosphere Model
UE5 uses Bruneton's 2017 atmospheric scattering model with three physical light interactions:
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

### Height Fog
- **Two-layer system**: uniform fog + exponential height-based fog
- **Directional inscattering**: Fog color brightens toward light direction (sun disk effect)
- **Sky injection**: Fog inherits sky color for realistic atmospheric blending
- **Volumetric integration**: Fog participates in post-process effects (bloom, tonemap)
