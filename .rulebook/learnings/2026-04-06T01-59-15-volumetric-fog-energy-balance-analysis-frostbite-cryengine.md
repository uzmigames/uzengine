# Volumetric Fog Energy Balance Analysis - Frostbite & CryEngine
**Source**: manual
**Date**: 2026-04-06
**Tags**: volumetric-fog, energy-conservation, Frostbite, CryEngine, rendering, froxel, ambient-light
Completed comprehensive analysis of volumetric fog rendering in Frostbite and CryEngine engines, focusing on preventing scene darkening.

## Key Findings

### 1. Extinction vs Scattering Albedo
- extinction = absorption + scattering
- scattering_albedo = scattering / extinction (0=pure absorb, 1=pure scatter)
- Real-world water fog/clouds: albedo ~0.99-1.0 (nearly pure scattering)
- Game engines MUST use albedo >= 0.9 to avoid unrealistic darkening

### 2. Frostbite's Solution (Hillaire 2016)
- 160x90x64 froxel grid with logarithmic depth distribution
- Light injection from ALL sources:
  * Directional: phase(angle) * visibility * intensity
  * Point/Spot: phase * attenuation * shadow_visibility
  * **Sky/Ambient (CRITICAL)**: sky_irradiance * (1 - exp(-extinction * depth))
- Multi-scattering compensation: boost ambient by mix(1.0, 2.0, phase_anisotropy)
- Temporal reprojection at 0.8:0.2 (current:previous) for stability

### 3. CryEngine's Approach
- Split anisotropy into two parameters:
  * Atmosphere (global): 0.0-0.4 (prevents shadow darkening)
  * Radial (sun bloom): 0.5-1.0 (concentrates forward scatter)
- Allows artists to tune diffuse vs directional scattering independently
- Ambient boost: (1 + anisotropy_radial) factor

### 4. Henyey-Greenstein Phase Function
- p(cos θ, g) = (1 - g²) / (4π(1 + g² - 2g cos θ)^1.5)
- Production values:
  * Frostbite: g=0.6-0.7 (fog), g=0.8-0.9 (clouds)
  * CryEngine: g=0.2-0.4 (global), g=0.9 (sun)
  * Ubisoft: g=0.5-0.6
- Lower g = less darkening in shadows (more isotropic)
- Higher g = more directional, risk of shadow darkening

### 5. Default Parameters
- Froxel grid: 160x90x64 exponential
- Extinction: 0.01-0.05 m^-1 (typical), 0.1 (dense)
- Albedo: 0.95 (nearly pure scattering)
- Ambient phase: g=0.2-0.3 (isotropic, prevent directional shadows)

### 6. Critical Anti-Darkening Techniques
1. **Sky light injection** into every froxel (prevents pitch-black)
2. **High scattering albedo** (0.9+) to match water physics
3. **Anisotropy-based multi-scatter boost**: low g gets 1.5x boost, high g gets 1.0x
4. **Temporal reprojection** for smooth transitions

## Implementation Priority for UzEngine
1. Implement sky/ambient injection first (fixes worst darkening)
2. Ensure scattering_albedo >= 0.9
3. Add Henyey-Greenstein phase function
4. Implement multi-scatter boost based on anisotropy
5. Add temporal reprojection last (optimization)

## Document Location
`e:/UzEngine/docs/analysis/volumetric-fog-energy-balance.md` (257 lines, complete)