# Shader Authoring Guide

This guide covers writing and iterating on HLSL shaders for UzEngine with hot-reload support and access to a shared include library.

---

## Overview

UzEngine shaders are **HLSL files** compiled by the **DXC compiler** and hot-reloaded instantly via IPC. Each shader is a standalone `.hlsl` file that can reference shared includes and binds to materials via `.uzmat` files.

### Key Principles

- **Plain text**: Shaders are human-readable and AI-generatable
- **Hot-reload**: Save a `.hlsl` file and see changes immediately in the viewport
- **Shared library**: Use includes for BRDF, lighting, skinning, noise, and utilities
- **Variants**: Same shader can compile multiple times with different `#define` constants for optimization
- **Error feedback**: DXC compilation errors show in VSCode with line numbers and diagnostics

---

## File Organization

### Directory Structure

```
project/
├── shaders/
│   ├── materials/
│   │   ├── pbr.hlsl           ; Main material shader
│   │   ├── glass.hlsl         ; Custom glass shader
│   │   └── fire.hlsl          ; Custom effect shader
│   ├── post/
│   │   ├── bloom.hlsl         ; Post-process bloom
│   │   └── tone_mapping.hlsl  ; Post-process tone mapping
│   ├── compute/
│   │   ├── particle_sim.hlsl  ; GPU particle simulation
│   │   └── lightmap_gen.hlsl  ; Lightmap generation
│   └── include/
│       ├── common.hlsli       ; Utility macros and functions
│       ├── brdf.hlsli         ; GGX BRDF
│       ├── lighting.hlsli     ; Light evaluation functions
│       ├── shadow.hlsli       ; Shadow sampling
│       ├── skinning.hlsli     ; GPU skinning
│       ├── tonemapping.hlsli  ; HDR tone mapping
│       ├── noise.hlsli        ; Procedural noise
│       └── global_cbuffer.hlsli ; Shared constant buffer layout
```

### Naming Conventions

- **Material shaders**: `shaders/materials/*.hlsl`
- **Post-process shaders**: `shaders/post/*.hlsl`
- **Compute shaders**: `shaders/compute/*.hlsl`
- **Includes**: `shaders/include/*.hlsli` (note: `.hlsli` for headers)

---

## Shader Include Library

All shaders have access to a shared library of includes. Include them like this:

```hlsl
#include "include/common.hlsli"
#include "include/brdf.hlsli"
#include "include/lighting.hlsli"
```

### common.hlsli

Utility macros and helper functions:

```hlsl
// Saturate dot product: clamp(dot(a, b), 0, 1)
float saturate_dot(float3 a, float3 b);

// Remap value from [0, 1] to [min, max]
float remap(float value, float min_val, float max_val);

// Fused multiply-add: a * b + c (optimized on GPU)
float3 fma(float3 a, float3 b, float3 c);

// Linear to sRGB conversion
float3 linear_to_srgb(float3 linear_color);

// sRGB to linear conversion
float3 srgb_to_linear(float3 srgb_color);

// Fast reciprocal square root
float fast_rsqrt(float x);

// Pack/unpack normal to/from RG11B10 format
float3 unpack_normal_rg11b10(uint packed);
uint pack_normal_rg11b10(float3 normal);
```

Usage example:

```hlsl
#include "include/common.hlsli"

float4 PSMain(VSOut i) : SV_TARGET {
    float3 N = normalize(i.normal);
    float3 V = normalize(CameraPos - i.world);
    float ndotv = saturate_dot(N, V);

    float3 diffuse = AlbedoTex.Sample(Sampler, i.uv).rgb;
    float3 final = diffuse * ndotv;
    return float4(linear_to_srgb(final), 1.0);
}
```

### brdf.hlsli

Cook-Torrance PBR BRDF implementation:

```hlsl
// GGX Normal Distribution Function
// roughness: [0, 1], N: surface normal, H: half vector
float ggx_ndf(float roughness, float3 N, float3 H);

// Smith G (Geometry attenuation)
// roughness: [0, 1], N: surface normal, V: view dir, L: light dir
float smith_g(float roughness, float3 N, float3 V, float3 L);

// Fresnel-Schlick approximation
// F0: base reflectivity (albedo for dielectric, metal color for metal)
// VoH: dot(view, half-vector)
float3 fresnel_schlick(float3 F0, float VoH);

// Full Cook-Torrance BRDF
// Inputs: albedo, roughness, metallic, N, V, L, light_color
// Returns: radiance contribution
float3 cook_torrance_brdf(float3 albedo, float roughness, float metallic,
                          float3 N, float3 V, float3 L, float3 light_color);
```

Usage example:

```hlsl
#include "include/brdf.hlsli"

float4 PSMain(VSOut i) : SV_TARGET {
    float3 albedo = AlbedoTex.Sample(Sampler, i.uv).rgb;
    float roughness = RoughnessTex.Sample(Sampler, i.uv).r;
    float metallic = MetallicTex.Sample(Sampler, i.uv).r;

    float3 N = normalize(NormalTex.Sample(Sampler, i.uv).rgb * 2.0 - 1.0);
    float3 V = normalize(CameraPos - i.world);
    float3 L = normalize(LightDir);

    float3 radiance = cook_torrance_brdf(albedo, roughness, metallic,
                                         N, V, L, LightColor);
    return float4(radiance, 1.0);
}
```

### lighting.hlsli

Light evaluation for different light types:

```hlsl
// Evaluate directional light contribution
// Returns radiance contribution
float3 evaluate_directional_light(float3 albedo, float roughness, float metallic,
                                  float3 N, float3 V, DirectionalLight light);

// Evaluate point light contribution
// attenuation: distance falloff (1 / distance^2)
float3 evaluate_point_light(float3 albedo, float roughness, float metallic,
                           float3 N, float3 V, float3 world_pos,
                           PointLight light, float attenuation);

// Evaluate spot light contribution with cone falloff
float3 evaluate_spot_light(float3 albedo, float roughness, float metallic,
                          float3 N, float3 V, float3 world_pos,
                          SpotLight light, float attenuation);

// Compute distance attenuation: falloff over radius
float light_distance_attenuation(float distance, float radius);

// Compute spot cone falloff
float spot_cone_falloff(float3 light_to_pixel, float3 light_dir,
                       float inner_angle, float outer_angle);
```

Usage example:

```hlsl
#include "include/lighting.hlsli"

float4 PSMain(VSOut i) : SV_TARGET {
    float3 albedo = AlbedoTex.Sample(Sampler, i.uv).rgb;
    float3 N = normalize(i.normal);
    float3 V = normalize(CameraPos - i.world);

    float3 radiance = evaluate_directional_light(albedo, 0.5, 0.0, N, V, g_SunLight);
    radiance += evaluate_point_light(albedo, 0.5, 0.0, N, V, i.world,
                                    g_PointLights[0], 1.0);
    return float4(radiance, 1.0);
}
```

### shadow.hlsli

Shadow map sampling with filtering:

```hlsl
// Sample shadow map with 2×2 PCF
// Returns: shadow term [0, 1] (1 = lit, 0 = shadowed)
float sample_shadow_map_pcf(Texture2D shadow_map, SamplerState sampler,
                            float3 shadow_coords, float depth_bias);

// Paraboloid projection for point lights (dual paraboloid)
// Converts world position to shadow map UV and depth
void paraboloid_project(float3 world_pos, float3 light_pos,
                       out float3 shadow_coords);

// Cascade selection for directional light shadows
// Returns: cascade index [0, num_cascades-1]
uint cascade_select(float depth, float4 cascade_splits);

// Sample cascaded shadow map with automatic cascade selection
float sample_cascade_shadow(Texture2DArray cascade_maps, SamplerState sampler,
                           float3 world_pos, float3 normal, float3 light_dir);
```

Usage example:

```hlsl
#include "include/shadow.hlsli"

float4 PSMain(VSOut i) : SV_TARGET {
    float shadow = sample_cascade_shadow(g_ShadowMaps, g_ShadowSampler,
                                        i.world, i.normal, g_SunDir);
    float3 lit = float3(1, 1, 1) * shadow;  ; Darken by shadow
    return float4(lit, 1.0);
}
```

### skinning.hlsli

GPU skinning for skeletal meshes:

```hlsl
// Maximum bones per vertex
#define MAX_BONE_INFLUENCES 4

// Skinning structure
struct SkinInfluence {
    uint bone_indices[4];   ; Bone palette indices
    float weights[4];       ; Blend weights
};

// Apply bone transformations to vertex position
// bone_matrices: array of bone transforms (float4x4 each)
// influence: skin weights and bone indices
// local_pos: position in local/bind space
float3 compute_skinned_position(float4x4 bone_matrices[256],
                                SkinInfluence influence,
                                float3 local_pos);

// Apply bone transformation to normal vector
float3 compute_skinned_normal(float4x4 bone_matrices[256],
                              SkinInfluence influence,
                              float3 local_normal);
```

Usage example:

```hlsl
#include "include/skinning.hlsli"

struct VSIn {
    float3 pos : POSITION;
    float3 normal : NORMAL;
    uint bone_idx[4] : BONEINDEX;
    float weight[4] : BONEWEIGHT;
};

VSOut VSMain(VSIn v) {
    SkinInfluence skin;
    skin.bone_indices = v.bone_idx;
    skin.weights = v.weight;

    float3 skinned_pos = compute_skinned_position(g_BoneMatrices, skin, v.pos);
    float3 skinned_normal = compute_skinned_normal(g_BoneMatrices, skin, v.normal);

    VSOut o;
    o.world = mul(float4(skinned_pos, 1.0), g_Model).xyz;
    o.normal = mul(skinned_normal, (float3x3)g_Model);
    return o;
}
```

### tonemapping.hlsli

HDR to SDR tone mapping:

```hlsl
// ACES filmic tone mapping (industry standard)
// Provides natural, film-like color compression
float3 aces_tonemapping(float3 hdr_color);

// Reinhard tone mapping (simple, fast)
float3 reinhard_tonemapping(float3 hdr_color);

// Uncharted 2 tone mapping (strong contrast)
float3 uncharted2_tonemapping(float3 hdr_color);

// Apply gamma correction (sRGB gamma = 2.2)
float3 gamma_correct(float3 linear_color, float gamma);
```

Usage example:

```hlsl
#include "include/tonemapping.hlsli"

float4 PSMain(VSOut i) : SV_TARGET {
    float3 hdr_color = compute_lighting(...);
    float3 sdr_color = aces_tonemapping(hdr_color);
    float3 gamma_corrected = gamma_correct(sdr_color, 2.2);
    return float4(gamma_corrected, 1.0);
}
```

### noise.hlsli

Procedural noise functions:

```hlsl
// Perlin noise — smooth continuous noise
// pos: 2D or 3D position, returns value in [0, 1]
float perlin_noise_2d(float2 pos);
float perlin_noise_3d(float3 pos);

// Simplex noise — faster than Perlin, fewer artifacts
float simplex_noise_2d(float2 pos);
float simplex_noise_3d(float3 pos);

// Fractional Brownian motion — layered noise
// octaves: number of noise layers, lacunarity: frequency multiplier, gain: amplitude multiplier
float fbm_2d(float2 pos, int octaves, float lacunarity, float gain);
float fbm_3d(float3 pos, int octaves, float lacunarity, float gain);

// Worley noise (cell noise) — used for cracks, canyons, etc.
float worley_noise_2d(float2 pos);
float worley_noise_3d(float3 pos);
```

Usage example:

```hlsl
#include "include/noise.hlsli"

float4 PSMain(VSOut i) : SV_TARGET {
    float displacement = fbm_3d(i.world, 4, 2.0, 0.5);
    float3 color = float3(displacement, displacement, 0.5);
    return float4(color, 1.0);
}
```

### global_cbuffer.hlsli

Shared constant buffer at slot `b15` (reserved for engine globals):

```hlsl
cbuffer GlobalParams : register(b15) {
    float g_Time;                  ; Elapsed time (seconds)
    float g_DeltaTime;             ; Frame delta time (seconds)
    float2 g_ScreenSize;           ; Render target dimensions (pixels)

    float4x4 g_ViewProj;           ; Composed view-projection matrix
    float4x4 g_InvViewProj;        ; Inverse for reprojection

    float3 g_CameraPos;            ; World space camera position
    float g_FarPlane;              ; Far clip distance

    float3 g_SunDir;               ; Directional light direction (normalized)
    float g_SunIntensity;          ; Sun light intensity

    float3 g_WindDirection;        ; World space wind direction (normalized)
    float g_WindStrength;          ; Wind effect magnitude

    float g_Exposure;              ; Exposure for tonemapping
    float g_Saturation;            ; Color saturation (1.0 = neutral)
    float3 g_Tint;                 ; Color tint multiplier
};
```

Usage example:

```hlsl
#include "include/global_cbuffer.hlsli"

float4 PSMain(VSOut i) : SV_TARGET {
    float3 color = i.base_color;

    ; Animate with time
    float wave = sin(g_Time * 2.0 + i.world.x * 0.5) * 0.5 + 0.5;

    ; Apply exposure
    color = color * g_Exposure;

    ; Sway with wind
    float sway = length(g_WindDirection) * g_WindStrength;

    return float4(color, 1.0);
}
```

---

## G-Buffer Layout

When writing deferred-rendering shaders, output to multiple render targets (G-Buffer):

```hlsl
struct PSOut {
    float4 albedo_ao : SV_TARGET0;      ; RGB = albedo, A = ambient occlusion
    float4 normal_params : SV_TARGET1;  ; RGB = normal (compressed), A = roughness, metallic
    float3 emissive : SV_TARGET2;       ; RGB = emissive color (HDR)
    float2 velocity : SV_TARGET3;       ; RG = screen-space velocity (for TAA)
};

PSOut PSMain(VSOut i) {
    PSOut o;

    ; Sample textures
    float3 albedo = AlbedoTex.Sample(Sampler, i.uv).rgb;
    float ao = AlbedoTex.Sample(Sampler, i.uv).a;

    float3 normal_map = NormalTex.Sample(Sampler, i.uv).rgb * 2.0 - 1.0;
    float3 normal = normalize(mul(normal_map, TBN));

    float roughness = RoughnessTex.Sample(Sampler, i.uv).r;
    float metallic = MetallicTex.Sample(Sampler, i.uv).r;
    float3 emissive = EmissiveTex.Sample(Sampler, i.uv).rgb;

    ; Compute velocity for TAA
    float4 current_pos = i.pos;
    float4 previous_pos = mul(float4(i.world, 1.0), g_PrevViewProj);
    float2 velocity = (current_pos.xy / current_pos.w) - (previous_pos.xy / previous_pos.w);

    ; Pack outputs
    o.albedo_ao = float4(albedo, ao);
    o.normal_params = float4(normal * 0.5 + 0.5, roughness);  ; Pack metallic into A if needed
    o.emissive = emissive;
    o.velocity = velocity;

    return o;
}
```

---

## Writing a Material Shader

### Complete PBR Material Example

File: `shaders/materials/pbr.hlsl`

```hlsl
#include "include/common.hlsli"
#include "include/brdf.hlsli"
#include "include/lighting.hlsli"
#include "include/shadow.hlsli"
#include "include/global_cbuffer.hlsli"

; Material constants (bound at slot b0)
cbuffer PerMaterial : register(b0) {
    float4 albedo_factor;
    float roughness_factor;
    float metallic_factor;
    float normal_scale;
    float emissive_intensity;
};

; Textures
Texture2D AlbedoTex : register(t0);
Texture2D NormalTex : register(t1);
Texture2D RoughnessTex : register(t2);
Texture2D MetallicTex : register(t3);
Texture2D AoTex : register(t4);
Texture2D EmissiveTex : register(t5);
Texture2DArray ShadowMaps : register(t10);

SamplerState LinearSampler : register(s0);
SamplerComparisonState ShadowSampler : register(s1);

; Vertex input
struct VSIn {
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float2 uv : TEXCOORD0;
};

; Vertex output
struct VSOut {
    float4 pos : SV_POSITION;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 binormal : BINORMAL;
    float2 uv : TEXCOORD0;
    float3 world : TEXCOORD1;
};

; Vertex shader
VSOut VSMain(VSIn v) {
    VSOut o;

    float4 world = mul(float4(v.pos, 1.0), g_Model);
    o.world = world.xyz;
    o.pos = mul(world, g_ViewProj);

    o.normal = mul(v.normal, (float3x3)g_Model);
    o.tangent = mul(v.tangent, (float3x3)g_Model);
    o.binormal = cross(o.normal, o.tangent);

    o.uv = v.uv;
    return o;
}

; Pixel shader
float4 PSMain(VSOut i) : SV_TARGET {
    ; Construct TBN matrix
    float3x3 tbn = float3x3(
        normalize(i.tangent),
        normalize(i.binormal),
        normalize(i.normal)
    );

    ; Sample textures
    float3 albedo = AlbedoTex.Sample(LinearSampler, i.uv).rgb * albedo_factor.rgb;
    float3 normal_map = NormalTex.Sample(LinearSampler, i.uv).rgb * 2.0 - 1.0;
    normal_map.xy *= normal_scale;

    float roughness = RoughnessTex.Sample(LinearSampler, i.uv).r * roughness_factor;
    float metallic = MetallicTex.Sample(LinearSampler, i.uv).r * metallic_factor;
    float ao = AoTex.Sample(LinearSampler, i.uv).r;
    float3 emissive = EmissiveTex.Sample(LinearSampler, i.uv).rgb * emissive_intensity;

    ; Normal mapping
    float3 normal = normalize(mul(normal_map, tbn));

    ; View direction
    float3 view = normalize(g_CameraPos - i.world);

    ; Evaluate sun light
    float shadow = sample_cascade_shadow(ShadowMaps, ShadowSampler,
                                        i.world, normal, g_SunDir);
    float3 sun_radiance = evaluate_directional_light(albedo, roughness, metallic,
                                                     normal, view, g_SunDir);
    sun_radiance *= shadow;

    ; Combine lighting
    float3 final_color = sun_radiance + emissive;
    final_color *= ao;

    return float4(final_color, 1.0);
}
```

---

## Shader Variants

Compile the same shader multiple times with different `#define` constants for optimization.

### Defining Variants

Inside the shader, use `#ifdef` guards:

```hlsl
#ifdef HAS_ALBEDO_TEX
    float3 albedo = AlbedoTex.Sample(Sampler, i.uv).rgb;
#else
    float3 albedo = AlbedoFactor.rgb;  ; Use constant instead
#endif

#ifdef HAS_NORMAL_TEX
    float3 normal = UnpackNormal(NormalTex.Sample(Sampler, i.uv));
#else
    float3 normal = i.normal;
#endif

#ifdef INSTANCED
    ; Use instance buffer data
    float4x4 model = InstanceData[instance_id].model;
#else
    ; Use constant buffer
    float4x4 model = g_Model;
#endif

#ifdef SKINNED
    ; Use skinning
    float3 skinned_pos = compute_skinned_position(...);
#else
    ; Use direct position
    float3 skinned_pos = v.pos;
#endif
```

### Registering Variants in .uzmat

In the `.uzmat` file, specify which variants to compile:

```ini
[material]
name            = "Character"
shader          = "res://shaders/materials/pbr.hlsl"

[variants]
HAS_ALBEDO_TEX  = true
HAS_NORMAL_TEX  = true
INSTANCED       = false
SKINNED         = true
```

The engine will compile **all combinations** of these defines. Example: `{true, true, false, true}` generates a single PSO for skinned, textured characters.

### Variant Best Practices

- **Common variants**: `HAS_*_TEX` for optional textures
- **Performance variants**: `INSTANCED`, `SKINNED`, `LOD_LEVEL`
- **Feature variants**: `USE_POM` (parallax occlusion mapping), `USE_SSS` (subsurface scattering)
- **Limit combinations**: Avoid exponential PSO explosion (max 32 variants per shader)

---

## Compute Shaders

GPU-driven compute for particles, lightmapping, post-processing.

### Structure

```hlsl
#include "include/global_cbuffer.hlsli"

; Shared compute constants
cbuffer ComputeParams : register(b0) {
    uint param_count;
    float param_strength;
};

; Read-write buffers
RWStructuredBuffer<float4> ParticlePositions : register(u0);
RWStructuredBuffer<float4> ParticleVelocities : register(u1);
RWStructuredBuffer<uint> ParticleAlive : register(u2);

; Read-only textures
Texture2D InputTexture : register(t0);

; Read-write images
RWTexture2D<float4> OutputImage : register(u3);

; Compute shader entry point
[numthreads(8, 8, 1)]
void CSMain(uint3 thread_id : SV_DispatchThreadID,
            uint3 group_id : SV_GroupID,
            uint3 group_thread_id : SV_GroupThreadID) {

    uint particle_id = thread_id.x;
    if (particle_id >= param_count) return;

    ; Load particle
    float4 pos = ParticlePositions[particle_id];
    float4 vel = ParticleVelocities[particle_id];

    ; Simulate
    vel.xyz += float3(0, -9.8, 0) * g_DeltaTime;  ; Gravity
    pos.xyz += vel.xyz * g_DeltaTime;

    ; Store back
    ParticlePositions[particle_id] = pos;
    ParticleVelocities[particle_id] = vel;
}
```

### Compute Thread Groups

- **`[numthreads(X, Y, Z)]`**: Dispatch group size
- **Max threads per group**: 1024
- **Typical values**: `[8,8,1]` (64 threads), `[16,16,1]` (256 threads)
- **Dispatch**: From C++: `dispatch(256, 1, 1)` to process 256 particles in groups of 64

---

## Hot-Reload Workflow

The typical iteration loop:

1. **Edit** `.hlsl` file in VSCode
2. **Save** (Ctrl+S)
3. **DXC compiles** (automatic, ~50 ms)
4. **Shader reloads** into viewport
5. **Errors appear** as red diagnostics
6. **Fix and repeat**

### Seeing Compilation Errors

In VSCode, errors appear inline with line numbers:

```
shaders/materials/pbr.hlsl
  Line 45: error X3004: undeclared identifier 'AlbedoSampler'
  Line 120: warning X3571: implicit cast from 'float4' to 'float3'
```

Or in the Output → Engine Console:

```
[ERROR] Shader compilation failed: res://shaders/materials/pbr.hlsl
  Line 45: undeclared identifier 'AlbedoSampler'
```

### Debugging with Screenshots

Use screenshots to visually inspect shader output:

```lua
-- In-game console
screenshot   ; Captures current viewport
```

Or via IPC (for AI iteration):

```json
{
    "command": "engine.screenshot"
}
```

---

## Common Shader Patterns

### Dissolve Effect

```hlsl
#include "include/noise.hlsli"

float4 PSMain(VSOut i) : SV_TARGET {
    float3 base_color = sample_base_color(i.uv);

    ; Time-based dissolve
    float dissolve = fbm_3d(i.world * 2.0, 3, 2.0, 0.5) + g_Time * 0.5;

    ; Threshold: fade out when dissolve < 0
    if (dissolve < 0.0) discard;

    ; Edge glow
    float edge = saturate(dissolve * 5.0);
    float3 glow = float3(1, 0.5, 0) * edge;

    return float4(base_color + glow, 1.0);
}
```

### Wind Sway

```hlsl
#include "include/global_cbuffer.hlsli"

VSOut VSMain(VSIn v) {
    VSOut o;

    ; Vertex sway based on height and wind
    float3 pos = v.pos;
    pos.xz += g_WindDirection.xz * sin(g_Time + v.pos.y * 5.0) * 0.1;

    float4 world = mul(float4(pos, 1.0), g_Model);
    o.pos = mul(world, g_ViewProj);
    o.world = world.xyz;
    return o;
}
```

### Vertex Displacement

```hlsl
float4 PSMain(VSOut i) : SV_TARGET {
    float3 displacement = perlin_noise_3d(i.world * 0.5) * 0.5;
    float3 normal = i.normal + displacement;
    return float4(normalize(normal) * 0.5 + 0.5, 1.0);
}
```

### Custom Lighting

```hlsl
#include "include/brdf.hlsli"

float4 PSMain(VSOut i) : SV_TARGET {
    float3 N = normalize(i.normal);
    float3 V = normalize(CameraPos - i.world);
    float3 L = normalize(LightDir);

    ; Toon shading effect
    float ndotl = saturate_dot(N, L);
    float shade = step(0.5, ndotl);  ; Binary shadow

    float3 base = float3(1, 1, 1) * shade;
    return float4(base, 1.0);
}
```

---

## Performance Tips

### General Optimization

1. **Use half precision**: `float16_t` instead of `float` for intermediate calculations
2. **Avoid divergent branching**: GPUs are SIMD; all threads in a warp must follow same path
3. **Cache texture samples**: Sample once, reuse result
4. **Minimize texture bandwidth**: Use compressed formats (BC7, BC5)
5. **Wave intrinsics**: Use `WaveActiveSum()`, `WaveReadLaneAt()` for group communication

### Example: Optimized PBR

```hlsl
; BEFORE: Slow (lots of texture samples)
float3 sample_all_textures(float2 uv) {
    float3 albedo = AlbedoTex.Sample(S, uv).rgb;
    float roughness = RoughnessTex.Sample(S, uv).r;
    float metallic = MetallicTex.Sample(S, uv).r;
    float ao = AoTex.Sample(S, uv).r;
    float3 normal = NormalTex.Sample(S, uv).rgb;
    return albedo + roughness + metallic + ao + normal;  ; Terrible!
}

; AFTER: Fast (pack into fewer samples)
; Pack: Albedo (RGB) + AO (A) into one sample
; Pack: Roughness (R) + Metallic (G) + Normal.Z (B) into another
float3 sample_optimized(float2 uv) {
    float4 packed_a = PackedTexA.Sample(S, uv);  ; Albedo + AO
    float4 packed_b = PackedTexB.Sample(S, uv);  ; Rough + Metal + Normal.Z
    return packed_a.rgb + packed_b.rgb;
}
```

### Async Compute

For heavy compute (particles, lightmapping):

```hlsl
; Mark shader as async compute
; Allows GPU to interleave with regular rendering
#pragma async_compute_hint
```

---

## Next Steps

- **Learn C++ integration**: See `docs/engine/cpp-architecture.md`
- **Editor workflow**: See [editor-workflow.md](editor-workflow.md)
- **Shader spec**: See `docs/specs/shader-system.md`
- **Example projects**: See `docs/guides/ai-development-guide.md` for shader co-authoring with Claude

