# Volumetric Fog Energy Balance - Frostbite & CryEngine Analysis

## Executive Summary

A critical problem in volumetric fog rendering is scene darkening in shadowed regions. Without proper ambient/sky light injection and energy-conservative scattering parameters, fog turns pitch-black in areas without direct sunlight.

Frostbite and CryEngine solve this through:
1. Explicit ambient/sky light injection into froxel volumes
2. High scattering albedo (0.9-1.0) for water droplets
3. Single + multi-scattering approximations
4. Henyey-Greenstein phase function (g = 0.6-0.8)

## Part 1: Extinction vs Scattering

### Fundamental Formula

extinction = absorption + scattering
scattering_albedo = scattering / extinction

### Value Ranges

- 0.0: Pure absorption (black)
- 0.5: 50/50 scatter/absorb
- 0.9-1.0: Nearly pure scattering (water fog/clouds)
- 1.0+: INVALID (violates conservation)

### Real-World Water Droplets

Per NASA ESPO research:
- At visible (0.65 um): albedo ~1.0
- At near-IR (1.63 um): albedo ~0.99

CRITICAL: Game engines must use albedo >= 0.9 for realistic water fog.

## Part 2: Frostbite Implementation

@ref Hillaire 2016 "Physically Based & Unified Volumetric Rendering in Frostbite" SIGGRAPH 2015

### Froxel Grid

Default: 160x90x64
- 160 pixels wide
- 90 pixels tall
- 64 slices (logarithmic depth)

Stores per froxel:
1. Extinction coefficient
2. In-scattering radiance
3. Temporal reprojection state

### Light Injection

#### Directional Light
visibility = SampleShadowMap(P)
L_dir = light_intensity * visibility * phase(angle)
F.scattering += L_dir

#### Point & Spot
distance = length(P - L.position)
attenuation = 1.0 / (distance * distance)
visibility = shadow_visibility(P, L)
F.scattering += L.intensity * attenuation * visibility * phase(angle)

#### Sky/Ambient Light (CRITICAL)
sky_irradiance = SampleSkyAtmosphereIrradiance(P.world)
sky_factor = 1.0 - exp(-extinction * depth_step)
F.scattering += sky_irradiance * sky_factor

This prevents pitch-black fog in shadows.

### Energy Conservation

#### Single-Scattering Ray March
radiance_out = 0.0
transmittance = 1.0

for each step:
    transmittance_step = exp(-extinction * depth)
    accumulated = FetchFroxel(pos).scattering
    radiance_out += transmittance * accumulated * (1 - transmittance_step)
    transmittance *= transmittance_step

#### Multi-Scattering Boost
multi_scatter = mix(1.0, 2.0, phase_anisotropy)
sky_multi = sky_irradiance * multi_scatter

Rationale: Real scattering bounces light in shadows. Boost ambient based on anisotropy to approximate.

## Part 3: CryEngine System

@ref Wronski 2014 "Volumetric Fog: Unified Compute Shader" SIGGRAPH 2014

### Core Parameters

Anisotropy (atmosphere): 0.0-1.0
- Low (0.0-0.2): distributed scattering, less shadow darkening
- High (0.8-1.0): forward scatter, sun bloom

Anisotropy (radial): 0.5-1.0 (default 0.9)

### In-Scattering

float3 ComputeInScattering(float3 pos, Light light) {
    float phase = HG(dot(to_camera, to_light), anisotropy);
    float vis = SampleShadowMap(pos, light);
    float att = 1.0 - length(pos - light) / light.range;
    float boost = lerp(1.0, 1.5, phase);
    return light.intensity * vis * phase * att * boost;
}

float3 ambient = SkyIrradiance(pos);

## Part 4: Phase Function

### Henyey-Greenstein

p(cos(angle), g) = (1 - g^2) / (4*pi * (1 + g^2 - 2*g*cos(angle))^1.5)

g range: -1 to +1
- 0.0: Isotropic
- 0.5: Forward bias (standard)
- 0.8: Strong forward
- 0.95+: Nearly forward

### Production Values

Frostbite:
- Atmospheric: g = 0.6-0.7
- Clouds: g = 0.8-0.9

CryEngine:
- Global: g = 0.2-0.4
- Sun bloom: g = 0.9

Ubisoft:
- Standard: g = 0.5-0.6

### Energy Conservation

Phase function integral = 1 (automatically satisfied by HG)

Issue: Not phase conservation, but scattering albedo controlling participation.

## Part 5: Anti-Darkening Strategies

### Problem

At shadows:
1. Directional: ZERO
2. Point lights: LOW
3. Ambient: MISSING
Result: Froxel ~= 0 -> BLACK

### Solution 1: Sky Injection

sky = SampleSkyIrradiance(pos)
phase = HG(0.0, 0.2)  // low-g, isotropic
froxel += sky * phase * extinction

Why: Sky is diffuse but everywhere. Prevents absolute darkness.

### Solution 2: Albedo >= 0.9

struct FogVolume {
    float extinction = 0.01;  // m^-1
    float albedo = 0.95;
    float scatter = extinction * albedo;
    float absorb = extinction * (1 - albedo);
};

High albedo: light stays via scattering.

### Solution 3: Anisotropy Boost

float g = 0.6;
float boost = mix(1.5, 1.0, g);
float3 ambient_boosted = sky * boost;
froxel += ambient_boosted;

Low g: isotropic -> boost 1.5x
High g: forward -> boost 1.0x

### Solution 4: Temporal Reprojection

float3 current = AccumulateLights(froxel);
float3 prev = SamplePreviousFrame(reprojected);
float3 final = mix(current, prev, 0.8);

Smooths transients, prevents jitter.

## Part 6: Reference Parameters

### Frostbite (Battlefield, Dragon Age)

FROXEL: 160x90x64 (exponential depth)
EXTINCTION: 0.01-0.05 m^-1 (fog), 0.1 (dense)
ALBEDO: 0.95
PHASE: g=0.6 (fog), g=0.75 (dense)
SKY: enabled, scale 1-exp(-ext*depth), boost 2.0x

### CryEngine (Far Cry, Hunt)

DENSITY: 0.01-0.2 (artist scale)
ANISO_ATMOS: 0.0-0.4 (default 0.2)
ANISO_RADIAL: 0.5-1.0 (default 0.9)
AMBIENT: from probes/sky, boost (1+aniso)
PHASE: Henyey-Greenstein

### Ubisoft (AC4: Black Flag)

COMPUTE: 3D texture stores light, visibility, phase
DISPATCH: one per light
PHASE: HG with g=0.5
INTEGRATE: single-scatter ray march, no ambient (env bake)

## Part 7: UzEngine Implementation

Phase 1: Froxel Grid
- 160x90x64 frustum voxel
- Logarithmic depth
- GPU scattering texture

Phase 2: Light Injection
- Directional: phase * vis * intensity
- Point/spot: phase * atten * shadow
- Sky (CRITICAL): sky * (1 - exp(-ext*depth))

Phase 3: Energy
- albedo >= 0.9
- scatter = extinction * albedo
- absorb = extinction * (1 - albedo)
- use in transmittance

Phase 4: Phase Function
- HG(cos_angle, g)
- Standard: g=0.6
- Dense: g=0.75-0.8
- Ambient: g=0.2-0.3

Phase 5: Multi-Scatter
- boost = mix(1.5, 1.0, aniso)
- apply to ambient

Phase 6: Temporal
- velocity buffer
- blend 0.8:0.2

## Sources

- [EA Frostbite - Physically Based Volumetric Rendering](https://www.ea.com/frostbite/news/physically-based-unified-volumetric-rendering-in-frostbite)
- [SIGGRAPH 2014 - Volumetric Fog](https://advances.realtimerendering.com/s2014/)
- [CryEngine 5 Volumetric Fog](https://www.cryengine.com/docs/static/engines/cryengine-5/)
- [NASA ESPO - Water Droplet Albedo](https://espo.nasa.gov/content/)
- [Unreal Engine 5.7 Volumetric Fog](https://dev.epicgames.com/documentation/)
- [PBR Book - Volume Scattering](https://pbr-book.org/3ed-2018/)
- [Flax Engine Volumetric Fog](https://docs.flaxengine.com/)

