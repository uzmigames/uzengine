<!-- doc-meta
title: VFX Vector Field Creation and Configuration Guide
category: guide
engine: uzengine
theme: rendering
created: 2026-03-25
updated: 2026-03-25
status: active
author: docs-operator
-->

# VFX Vector Field Creation and Configuration Guide

## Overview

Vector fields drive particle motion using pre-computed 3D textures that store velocity or direction data. This guide covers creating vector field textures, configuring the `VectorFieldModule`, and practical examples like wind tunnels and animated flow effects.

The module maps particle world-space positions into a 3D texture bounding box, samples the field, and applies the sampled velocity as either additive force or direct velocity replacement, controlled by the `tightness` blending parameter.

## Context

Vector fields are a core technique in visual effects for creating complex fluid-like or directional motion without per-particle simulation. They're implemented in `VectorFieldModule`, which uses a `Texture3D` resource bound via bindless SRV. The field is referenced in the standard UzEngine VFX constant buffer layout, allowing runtime updates without recompilation.

@ref Stam 1999 "Stable Fluids" — mathematical foundation for vector field advection
@ref Bridson 2007 "Fluid Simulation for Computer Graphics" — practical implementation of velocity field sampling and temporal coherence

## 3D Texture Format and Encoding

Vector field textures store directional information in three-channel RGB format using a normalized encoding scheme.

### Direction Encoding

Directions in the range [-1, 1] are encoded in texture RGB channels [0, 1] using:

```
textureRGB = direction * 0.5 + 0.5
```

When sampling in the shader, decode back to the original range:

```hlsl
float3 direction = texSample.rgb * 2.0 - 1.0;
```

This encoding is necessary because GPU texture formats store unsigned bytes or floats in [0, 1]; to represent bidirectional data (e.g., wind that can blow left or right), we shift and scale.

### Recommended Formats

| Format | Precision | Size | Best Use |
|--------|-----------|------|----------|
| R8G8B8A8_UNORM | 8-bit (low) | 4 bytes/texel | Fast, compact directions; acceptable for dense fields. |
| RGBA16_FLOAT | 16-bit (medium) | 8 bytes/texel | High-quality directions; supports magnitude in alpha. |
| RGBA32_FLOAT | 32-bit (high) | 16 bytes/texel | Maximum precision; use for critical fields only. |

**Recommendation**: RGBA16_FLOAT balances quality and memory. Use RGBA32_FLOAT only if the field carries per-texel magnitude or other derived data in the alpha channel.

### Optional: Magnitude in Alpha

If your field needs to vary magnitude per-texel (e.g., a wind field that is strongest at the center and weak at boundaries), store magnitude in the alpha channel:

```
textureAlpha = magnitude [0, 1]
```

Then in the shader:

```hlsl
float3 direction = tex.rgb * 2.0 - 1.0;
float magnitude = tex.a;
float3 fieldVelocity = direction * magnitude * intensity;
```

## Configuration Parameters

All settings are properties of the `VectorFieldModule` and control how the field is sampled and applied.

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `fieldTexture` | Bindless SRV Index | Invalid | The 3D texture resource SRV index in the bindless heap. |
| `intensity` | Float | 1.0 | Force magnitude multiplier applied to sampled field vectors. |
| `tightness` | Float | 0.0 | Blending factor: 0 = additive force, 1 = full velocity replacement. |
| `bounds` | Vec3 | {10, 10, 10} | World-space half-extents of the field volume (cm). |
| `fieldOffset` | Vec3 | {0, 0, 0} | World-space centre of the field volume (cm). |
| `timeScale` | Float | 0.0 | Animated field scroll speed along the W axis (time-varying). |

## UVW Mapping and Bounding Box

Particles are mapped into the 3D texture's UV coordinate space using a world-space axis-aligned bounding box (AABB).

### Bounding Box Definition

The field volume is defined by `fieldOffset` (center) and `bounds` (half-extents):

```
min = fieldOffset - bounds
max = fieldOffset + bounds
```

A particle is inside the field when:
```
abs(particlePosition - fieldOffset) <= bounds  (component-wise)
```

Particles outside the bounding box are unaffected.

### UVW Coordinate Computation

For a particle inside the box, the normalized UVW coordinates are computed as:

```hlsl
float3 localPos = particlePosition - fieldOffset;
float3 uvw = (localPos + bounds) / (2.0 * bounds);
// uvw is now in [0, 1] x [0, 1] x [0, 1] space
```

This maps the world-space box to the unit cube of the 3D texture.

### Animated Fields: W-Axis Scrolling

When `timeScale` is non-zero, the field animates by scrolling along the W (temporal) axis:

```hlsl
float uvw_w = frac(fieldUVW.w + timeScale * elapsedTime);
float3 uvw_animated = float3(fieldUVW.xyz, uvw_w);
float4 fieldSample = fieldTexture.SampleLevel(fieldSampler, uvw_animated, 0.0);
```

This creates time-varying flow without uploading new texture data each frame.

## Tightness Blending Modes

The `tightness` parameter interpolates between two behaviors:

### Tightness = 0.0 (Additive Force)

Field velocity is added as an acceleration impulse:

```hlsl
velocity += fieldVelocity * intensity * dt;
```

Particles retain their original motion direction and accelerate in the field direction. This mode is suitable for turbulence, gusts, or perturbations to existing motion.

**Use case**: A wind field that nudges particles without overriding their trajectory.

### Tightness = 1.0 (Full Replacement)

Particle velocity is fully replaced by the field velocity:

```hlsl
velocity = fieldVelocity * intensity;
```

Particles immediately adopt the field's motion, losing any prior velocity. This mode is suitable for strong flows or fluid simulations.

**Use case**: A water current that carries particles along a defined path.

### Tightness = Intermediate (0 < t < 1)

Blending between additive and replacement:

```hlsl
velocity = lerp(velocity, fieldVelocity * intensity, tightness * dt);
```

The blend factor is scaled by `dt` so that higher `tightness` values converge to the field velocity more quickly. Typical range is [0.1, 0.9] for smooth interpolation.

**Use case**: A "sticky" field that gradually guides particles toward a desired flow.

## Creating Vector Field Textures

Vector field textures can be created offline using simulation tools or procedurally.

### Option 1: Offline Simulation (Recommended)

1. Run a 3D fluid simulation in a tool like Maya, Houdini, or OpenVDB.
2. Export the velocity field at each voxel as a 3D texture atlas or sequence.
3. Apply the encoding: `texel = velocity * 0.5 + 0.5`.
4. Save in your engine's texture format (DDS, PNG, or proprietary).
5. Load and bind the texture to the bindless SRV heap.

### Option 2: Procedural Generation

Generate the field at runtime using a noise function:

```cpp
// Pseudo-code: generate a wind tunnel from Perlin noise
const uint32_t resX = 32, resY = 32, resZ = 32;
std::vector<uint8_t> textureData;

for (uint32_t z = 0; z < resZ; ++z) {
    for (uint32_t y = 0; y < resY; ++y) {
        for (uint32_t x = 0; x < resX; ++x) {
            float noiseSample = PerlinNoise3D(x * 0.1f, y * 0.1f, z * 0.1f);
            float3 velocity = {noiseSample, 0.0f, 0.0f};  // Wind along X

            // Encode to [0, 1]
            uint8_t r = (uint8_t)((velocity.x * 0.5 + 0.5) * 255);
            uint8_t g = (uint8_t)((velocity.y * 0.5 + 0.5) * 255);
            uint8_t b = (uint8_t)((velocity.z * 0.5 + 0.5) * 255);
            uint8_t a = 255;  // Full intensity

            textureData.push_back(r);
            textureData.push_back(g);
            textureData.push_back(b);
            textureData.push_back(a);
        }
    }
}

// Create a 32x32x32 RGBA8 Texture3D from textureData
```

## HLSL Integration Overview

The module's GPU code samples the 3D field at each particle's position and applies the sampled velocity according to the tightness mode.

Key shader operations:

```hlsl
// Normalize particle position to texture coordinates [0, 1]
float3 localPos = particlePosition - fieldOffset;
float3 uvw = (localPos + bounds) / (2.0 * bounds);

// Clamp to valid texture space (particles outside bounds get no force)
if (any(abs(localPos) > bounds)) {
    return;  // Particle is outside the field volume
}

// Sample the field texture
float4 fieldSample = fieldTexture.SampleLevel(fieldSampler, uvw, 0.0);
float3 fieldVelocity = (fieldSample.rgb * 2.0 - 1.0) * intensity;

// Apply based on tightness mode
if (tightness < 0.01) {
    // Additive
    velocity += fieldVelocity * dt;
} else if (tightness > 0.99) {
    // Replace
    velocity = fieldVelocity;
} else {
    // Interpolated
    velocity = lerp(velocity, fieldVelocity, tightness * dt);
}
```

## Examples

### Example 1: Wind Tunnel (Constant Additive)

Particles are buffeted by a gentle wind that perturbs their motion without controlling it.

```cpp
VectorFieldModule wind;
wind.fieldTexture = /* bindless SRV index of wind field */;
wind.intensity = 5.0f;     // 5 m/s wind speed equivalent
wind.tightness = 0.0f;     // Purely additive (perturbation)
wind.bounds = {50.0f, 50.0f, 50.0f};  // 100x100x100 cm zone
wind.fieldOffset = {0.0f, 0.0f, 0.0f};
wind.timeScale = 0.0f;     // Static field
```

**Visual result**: Particles drift and flutter as they move through the wind zone. Their original motion is preserved; the wind nudges them sideways.

### Example 2: Water Current (High Tightness)

Particles in a river are swept along a defined flow path.

```cpp
VectorFieldModule river;
river.fieldTexture = /* bindless SRV index of river flow field */;
river.intensity = 20.0f;   // Strong flow
river.tightness = 0.8f;    // Heavy influence; particles converge to field velocity
river.bounds = {10.0f, 30.0f, 5.0f};  // Narrow, elongated zone
river.fieldOffset = {0.0f, 0.0f, 100.0f};  // Positioned downstream
river.timeScale = 0.0f;    // Static field (or could be animated)
```

**Visual result**: Particles entering the river zone are quickly swept along the current path. Their initial velocity is overridden by the strong field effect.

### Example 3: Animated Vortex (Time-Varying)

A rotating vortex field that animates over time, creating a spinning effect.

```cpp
VectorFieldModule vortex;
vortex.fieldTexture = /* bindless SRV index of pre-computed vortex field sequence */;
vortex.intensity = 15.0f;
vortex.tightness = 0.5f;   // Moderate influence
vortex.bounds = {30.0f, 30.0f, 30.0f};
vortex.fieldOffset = {100.0f, 0.0f, 0.0f};
vortex.timeScale = 2.0f;   // Scroll field at 2 units/second along W axis
```

**Visual result**: As time progresses, the field scrolls along its W-axis, creating an animation loop. Particles spiral and swirl as they move through the vortex.

### Example 4: Magic Dust Trail (Low Intensity Additive)

A subtle field that adds a whimsical curliness to floating dust particles.

```cpp
VectorFieldModule magic;
magic.fieldTexture = /* bindless SRV index of swirly noise field */;
magic.intensity = 1.0f;    // Very subtle
magic.tightness = 0.1f;    // Slight blending toward field
magic.bounds = {20.0f, 20.0f, 20.0f};
magic.fieldOffset = {0.0f, 50.0f, 0.0f};  // Positioned above ground
magic.timeScale = 0.5f;    // Slow animation loop
```

**Visual result**: Dust particles flutter gently in a magical swirl, driven primarily by their own physics but with a subtle guiding influence from the field.

## Performance Considerations

- **GPU cost**: Per-particle, vector field sampling requires one 3D texture lookup (~10-20 GPU cycles). The tightness blending adds minimal cost.
- **Memory**: A 32×32×32 RGBA16_FLOAT field consumes approximately 256 KB of VRAM. Larger fields (64×64×64) consume ~2 MB.
- **Bandwidth**: Texture sampling scales with cache efficiency; 3D texture lookups have slightly higher latency than 2D sampling due to lower coherence in Z-axis memory layout.
- **Scaling**: Multiple fields (e.g., wind + vortex) require separate modules, each adding one texture sample per particle.

## Common Issues

**Particles freeze or move incorrectly at field boundaries**:
Ensure the bounding box (`bounds` and `fieldOffset`) matches the particles' expected zone. Particles outside the box receive no force. Check that `intensity` is non-zero.

**Field effect is too strong or too weak**:
Adjust `intensity` to scale the sampled velocity. For additive mode (tightness = 0), try increasing intensity. For replacement mode (tightness = 1), ensure the field texture's magnitude is appropriate.

**Animated field is not animating**:
Check that `timeScale` is non-zero. A value of 0 disables animation. Ensure the field texture contains multiple frames along the W-axis or is procedurally varied.

**Texture appears as noise or garbage**:
Verify that `fieldTexture` is a valid bindless SRV index. Invalid indices may cause undefined sampling. Check texture format encoding — directions must be in [-1, 1] encoded as [0, 1] in the texture.

## Key Takeaways

- **3D Texture Encoding**: Directions [-1, 1] are stored as texture values [0, 1] using `texel = dir * 0.5 + 0.5`.
- **Bounding Box Mapping**: Particles inside the world-space AABB sample the field; particles outside are unaffected.
- **Tightness Blending**: 0 = additive perturbation, 1 = full velocity replacement, intermediate = smooth interpolation.
- **Animated Fields**: Use `timeScale` to scroll the field along the W-axis over time without updating texture data.
- **GPU-Executed**: Field sampling runs on the GPU as part of the main simulation compute dispatch.

## References

- `runtime/engine/vfx/modules/vector_field_module.h` — VectorFieldModule configuration and API
- `runtime/shaders/runtime/vfx/vfx_vector_field.hlsli` — HLSL sampling and blending implementation
- @ref Stam 1999 "Stable Fluids" — vector field advection and time-varying field theory
- @ref Bridson 2007 "Fluid Simulation for Computer Graphics" — practical velocity field sampling and divergence-free fields

## Changelog

| Date | Change |
|------|--------|
| 2026-03-25 | Initial creation with texture encoding, bounding box mapping, tightness blending, and practical examples. |
