# Shader Include / Library System Spec

## Overview

UzEngine provides a library of shared HLSL include files (`.hlsli`) that contain reusable functions for BRDF computation, lighting evaluation, shadow sampling, skinning, tonemapping, noise, and global constants. All shaders include from a common directory, and the shader compiler tracks include dependencies for hot-reload.

---

## Directory Structure

```
shaders/
  include/                      # Shared include files (*.hlsli)
    common.hlsli                # Math constants, utility functions
    brdf.hlsli                  # GGX NDF, Smith G, Schlick F
    lighting.hlsli              # Point/spot/directional light evaluation
    shadow.hlsli                # CSM sampling, PCF kernels
    skinning.hlsli              # Bone matrix palette, vertex skinning
    tonemapping.hlsli           # ACES, Reinhard, Uncharted2
    noise.hlsli                 # Perlin, simplex, hash functions
    global_cbuffer.hlsli        # GlobalParams cbuffer (b15)
  builtin/                      # Built-in shaders
    pbr.hlsl
    unlit.hlsl
    wireframe.hlsl
    depth.hlsl
    skybox.hlsl
    fullscreen.hlsl
    error.hlsl
    shadow_depth.hlsl
    gbuffer.hlsl
    deferred_lighting.hlsl
```

All `.hlsli` files use include guards with `#ifndef` / `#define` / `#endif`.

---

## Include Files

### `common.hlsli` -- Math Constants and Utilities

```hlsl
#ifndef UZ_COMMON_HLSLI
#define UZ_COMMON_HLSLI

// Mathematical constants
static const float PI        = 3.14159265358979323846;
static const float TWO_PI    = 6.28318530717958647692;
static const float HALF_PI   = 1.57079632679489661923;
static const float INV_PI    = 0.31830988618379067153;
static const float EPSILON   = 1e-6;

// Utility functions
float  sq(float x)            { return x * x; }
float3 safe_normalize(float3 v) { return normalize(v + float3(EPSILON, 0, 0)); }
float  remap(float v, float lo, float hi) { return saturate((v - lo) / (hi - lo)); }

// Depth reconstruction from depth buffer + inverse view-proj
float3 reconstruct_world_pos(float2 uv, float depth, float4x4 inv_view_proj) {
    float4 ndc = float4(uv * 2.0 - 1.0, depth, 1.0);
    ndc.y = -ndc.y;  // DX12 NDC convention
    float4 world = mul(inv_view_proj, ndc);
    return world.xyz / world.w;
}

// Octahedral encoding for normals (compact 2-channel storage)
float2 oct_encode(float3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    if (n.z < 0.0) {
        n.xy = (1.0 - abs(n.yx)) * (n.xy >= 0.0 ? 1.0 : -1.0);
    }
    return n.xy * 0.5 + 0.5;
}

float3 oct_decode(float2 e) {
    e = e * 2.0 - 1.0;
    float3 n = float3(e.xy, 1.0 - abs(e.x) - abs(e.y));
    if (n.z < 0.0) {
        n.xy = (1.0 - abs(n.yx)) * (n.xy >= 0.0 ? 1.0 : -1.0);
    }
    return normalize(n);
}

#endif // UZ_COMMON_HLSLI
```

### `brdf.hlsli` -- PBR BRDF Functions

```hlsl
#ifndef UZ_BRDF_HLSLI
#define UZ_BRDF_HLSLI

#include "common.hlsli"

// GGX / Trowbridge-Reitz Normal Distribution Function
// a = roughness^2 (alpha)
float D_GGX(float NdotH, float a) {
    float a2  = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom + EPSILON);
}

// Smith GGX Geometry Function (single direction)
float G_SchlickGGX(float NdotX, float k) {
    return NdotX / (NdotX * (1.0 - k) + k + EPSILON);
}

// Smith geometry for direct lighting
// k = (roughness + 1)^2 / 8  (direct lighting)
float G_Smith(float NdotV, float NdotL, float roughness) {
    float k = sq(roughness + 1.0) / 8.0;
    return G_SchlickGGX(NdotV, k) * G_SchlickGGX(NdotL, k);
}

// Smith geometry for IBL
// k = roughness^2 / 2  (IBL)
float G_Smith_IBL(float NdotV, float NdotL, float roughness) {
    float k = sq(roughness) / 2.0;
    return G_SchlickGGX(NdotV, k) * G_SchlickGGX(NdotL, k);
}

// Schlick Fresnel approximation
float3 F_Schlick(float HdotV, float3 F0) {
    return F0 + (1.0 - F0) * pow(saturate(1.0 - HdotV), 5.0);
}

// Schlick Fresnel with roughness (for IBL)
float3 F_SchlickRoughness(float NdotV, float3 F0, float roughness) {
    float3 max_reflect = max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0);
    return F0 + (max_reflect - F0) * pow(saturate(1.0 - NdotV), 5.0);
}

// Full Cook-Torrance specular BRDF for a single light
float3 CookTorrance(float3 N, float3 V, float3 L, float3 albedo,
                    float roughness, float metallic) {
    float3 H    = normalize(V + L);
    float  a    = roughness * roughness;
    float NdotH = saturate(dot(N, H));
    float NdotV = max(dot(N, V), EPSILON);
    float NdotL = saturate(dot(N, L));
    float HdotV = saturate(dot(H, V));

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    float  D = D_GGX(NdotH, a);
    float  G = G_Smith(NdotV, NdotL, roughness);
    float3 F = F_Schlick(HdotV, F0);

    // Specular
    float3 spec = D * G * F / max(4.0 * NdotV * NdotL, EPSILON);

    // Diffuse (metals have no diffuse)
    float3 kD = (1.0 - F) * (1.0 - metallic);
    float3 diff = kD * albedo * INV_PI;

    return (diff + spec) * NdotL;
}

#endif // UZ_BRDF_HLSLI
```

### `lighting.hlsli` -- Light Evaluation

```hlsl
#ifndef UZ_LIGHTING_HLSLI
#define UZ_LIGHTING_HLSLI

#include "common.hlsli"
#include "brdf.hlsli"

// Directional light structure (matches per-frame CBV layout)
struct DirLight {
    float3 direction;   // normalized, pointing toward light
    float  intensity;
    float3 color;
    float  _pad0;
};

// Point light structure (matches StructuredBuffer<PointLight>)
struct PointLight {
    float3 position;
    float  radius;
    float3 color;
    float  intensity;
};

// Spot light structure (matches StructuredBuffer<SpotLight>)
struct SpotLight {
    float3 position;
    float  range;
    float3 direction;   // normalized, cone axis
    float  intensity;
    float3 color;
    float  inner_cos;   // cos(inner_angle / 2)
    float  outer_cos;   // cos(outer_angle / 2)
    float3 _pad0;
};

// Inverse-square attenuation with smooth fade at radius boundary
// Prevents hard cutoff at light radius
float attenuation_point(float dist, float radius) {
    float d2 = dist * dist;
    float r2 = radius * radius;
    // Smooth window function: (1 - (d/r)^2)^2
    float window = sq(saturate(1.0 - sq(dist / radius)));
    // Physical inverse-square with window
    return window / (d2 + 1.0);
}

// Spot light angular attenuation
float attenuation_spot(float3 L, float3 spot_dir, float inner_cos, float outer_cos) {
    float cos_angle = dot(-L, spot_dir);
    return saturate((cos_angle - outer_cos) / (inner_cos - outer_cos + EPSILON));
}

// Evaluate directional light contribution
float3 eval_dir_light(DirLight light, float3 N, float3 V, float3 world_pos,
                      float3 albedo, float roughness, float metallic, float shadow) {
    float3 L = light.direction;
    float3 radiance = light.color * light.intensity * shadow;
    return CookTorrance(N, V, L, albedo, roughness, metallic) * radiance;
}

// Evaluate point light contribution
float3 eval_point_light(PointLight light, float3 N, float3 V, float3 world_pos,
                        float3 albedo, float roughness, float metallic) {
    float3 L_vec = light.position - world_pos;
    float  dist  = length(L_vec);
    float3 L     = L_vec / (dist + EPSILON);
    float  atten = attenuation_point(dist, light.radius);
    float3 radiance = light.color * light.intensity * atten;
    return CookTorrance(N, V, L, albedo, roughness, metallic) * radiance;
}

// Evaluate spot light contribution
float3 eval_spot_light(SpotLight light, float3 N, float3 V, float3 world_pos,
                       float3 albedo, float roughness, float metallic) {
    float3 L_vec = light.position - world_pos;
    float  dist  = length(L_vec);
    float3 L     = L_vec / (dist + EPSILON);
    float  atten = attenuation_point(dist, light.range);
    float  spot  = attenuation_spot(L, light.direction, light.inner_cos, light.outer_cos);
    float3 radiance = light.color * light.intensity * atten * spot;
    return CookTorrance(N, V, L, albedo, roughness, metallic) * radiance;
}

#endif // UZ_LIGHTING_HLSLI
```

### `shadow.hlsli` -- Shadow Sampling

```hlsl
#ifndef UZ_SHADOW_HLSLI
#define UZ_SHADOW_HLSLI

#include "common.hlsli"

// PCF 2x2 shadow sampling (LOW quality / point+spot lights)
float shadow_pcf_2x2(Texture2D shadow_map, SamplerComparisonState shadow_sampler,
                     float3 shadow_coord, float2 texel_size) {
    float shadow = 0.0;
    [unroll]
    for (int y = 0; y <= 1; ++y) {
        [unroll]
        for (int x = 0; x <= 1; ++x) {
            float2 offset = float2(x, y) * texel_size;
            shadow += shadow_map.SampleCmpLevelZero(shadow_sampler,
                shadow_coord.xy + offset, shadow_coord.z);
        }
    }
    return shadow * 0.25;
}

// PCF 3x3 shadow sampling (HIGH quality / directional CSM)
float shadow_pcf_3x3(Texture2D shadow_map, SamplerComparisonState shadow_sampler,
                     float3 shadow_coord, float2 texel_size) {
    float shadow = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            float2 offset = float2(x, y) * texel_size;
            shadow += shadow_map.SampleCmpLevelZero(shadow_sampler,
                shadow_coord.xy + offset, shadow_coord.z);
        }
    }
    return shadow / 9.0;
}

// Cascade selection for CSM
// Returns cascade index (0, 1, or 2) based on view-space depth
uint select_cascade(float view_depth, float4 cascade_splits) {
    uint cascade = 0;
    [unroll]
    for (uint i = 0; i < NUM_CASCADES; ++i) {
        if (view_depth > cascade_splits[i]) {
            cascade = i + 1;
        }
    }
    return min(cascade, NUM_CASCADES - 1);
}

// Transform world position to shadow map UV + depth for a given cascade
float3 world_to_shadow(float3 world_pos, float4x4 light_view_proj) {
    float4 shadow_clip = mul(light_view_proj, float4(world_pos, 1.0));
    float3 shadow_ndc  = shadow_clip.xyz / shadow_clip.w;
    // NDC [-1,1] to UV [0,1], flip Y
    float2 shadow_uv = shadow_ndc.xy * float2(0.5, -0.5) + 0.5;
    return float3(shadow_uv, shadow_ndc.z);
}

#endif // UZ_SHADOW_HLSLI
```

### `skinning.hlsli` -- Skeletal Mesh Skinning

```hlsl
#ifndef UZ_SKINNING_HLSLI
#define UZ_SKINNING_HLSLI

// Bone matrix palette stored as StructuredBuffer
// Max 256 bones per skeleton
static const uint MAX_BONES = 256;

struct SkinVertex {
    float3 position;
    float3 normal;
    float3 tangent;
};

// Apply bone transforms to a vertex (4 bone influences)
SkinVertex skin_vertex(float3 pos, float3 normal, float3 tangent,
                       uint4 bone_indices, float4 bone_weights,
                       StructuredBuffer<float4x4> bone_palette) {
    SkinVertex result;
    result.position = float3(0, 0, 0);
    result.normal   = float3(0, 0, 0);
    result.tangent  = float3(0, 0, 0);

    [unroll]
    for (uint i = 0; i < 4; ++i) {
        float    w = bone_weights[i];
        float4x4 m = bone_palette[bone_indices[i]];

        result.position += mul(m, float4(pos, 1.0)).xyz * w;
        result.normal   += mul((float3x3)m, normal) * w;
        result.tangent  += mul((float3x3)m, tangent) * w;
    }

    result.normal  = normalize(result.normal);
    result.tangent = normalize(result.tangent);
    return result;
}

#endif // UZ_SKINNING_HLSLI
```

### `tonemapping.hlsli` -- Tone Mapping Operators

```hlsl
#ifndef UZ_TONEMAPPING_HLSLI
#define UZ_TONEMAPPING_HLSLI

// ACES Filmic (the engine default)
// Input: linear HDR color. Output: LDR [0,1].
float3 ACES_Filmic(float3 x) {
    // sRGB fitted ACES curve (Krzysztof Narkowicz approximation)
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// Reinhard tone mapping (simple)
float3 Reinhard(float3 x) {
    return x / (x + 1.0);
}

// Reinhard extended (with white point)
float3 Reinhard_Extended(float3 x, float white_point) {
    float3 numerator = x * (1.0 + x / (white_point * white_point));
    return numerator / (1.0 + x);
}

// Linear to sRGB gamma
float3 linear_to_srgb(float3 color) {
    return pow(saturate(color), 1.0 / 2.2);
}

// sRGB to linear
float3 srgb_to_linear(float3 color) {
    return pow(saturate(color), 2.2);
}

#endif // UZ_TONEMAPPING_HLSLI
```

### `noise.hlsli` -- Noise Functions

```hlsl
#ifndef UZ_NOISE_HLSLI
#define UZ_NOISE_HLSLI

// Hash function for noise (good distribution, fast)
float hash11(float p) {
    p = frac(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return frac(p);
}

float hash21(float2 p) {
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float3 hash33(float3 p) {
    p = float3(dot(p, float3(127.1, 311.7, 74.7)),
               dot(p, float3(269.5, 183.3, 246.1)),
               dot(p, float3(113.5, 271.9, 124.6)));
    return frac(sin(p) * 43758.5453123);
}

// Classic Perlin noise (3D)
float perlin_noise(float3 p) {
    float3 i = floor(p);
    float3 f = frac(p);
    // Quintic interpolation curve
    float3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);

    return lerp(lerp(lerp(dot(hash33(i + float3(0,0,0)) * 2.0 - 1.0, f - float3(0,0,0)),
                          dot(hash33(i + float3(1,0,0)) * 2.0 - 1.0, f - float3(1,0,0)), u.x),
                     lerp(dot(hash33(i + float3(0,1,0)) * 2.0 - 1.0, f - float3(0,1,0)),
                          dot(hash33(i + float3(1,1,0)) * 2.0 - 1.0, f - float3(1,1,0)), u.x), u.y),
                lerp(lerp(dot(hash33(i + float3(0,0,1)) * 2.0 - 1.0, f - float3(0,0,1)),
                          dot(hash33(i + float3(1,0,1)) * 2.0 - 1.0, f - float3(1,0,1)), u.x),
                     lerp(dot(hash33(i + float3(0,1,1)) * 2.0 - 1.0, f - float3(0,1,1)),
                          dot(hash33(i + float3(1,1,1)) * 2.0 - 1.0, f - float3(1,1,1)), u.x), u.y), u.z);
}

// Simplex-like noise (3D, based on Ashima Arts)
float simplex_noise(float3 v) {
    // Simplified 3D simplex noise
    const float2 C = float2(1.0 / 6.0, 1.0 / 3.0);
    float3 i  = floor(v + dot(v, C.yyy));
    float3 x0 = v - i + dot(i, C.xxx);

    float3 g = step(x0.yzx, x0.xyz);
    float3 l = 1.0 - g;
    float3 i1 = min(g.xyz, l.zxy);
    float3 i2 = max(g.xyz, l.zxy);

    float3 x1 = x0 - i1 + C.xxx;
    float3 x2 = x0 - i2 + C.yyy;
    float3 x3 = x0 - 0.5;

    float4 w = max(0.6 - float4(dot(x0,x0), dot(x1,x1), dot(x2,x2), dot(x3,x3)), 0.0);
    w = w * w * w * w;

    float4 d;
    d.x = dot(hash33(i) * 2.0 - 1.0, x0);
    d.y = dot(hash33(i + i1) * 2.0 - 1.0, x1);
    d.z = dot(hash33(i + i2) * 2.0 - 1.0, x2);
    d.w = dot(hash33(i + 1.0) * 2.0 - 1.0, x3);

    return dot(d, w) * 52.0;
}

// Fractal Brownian Motion (FBM)
float fbm(float3 p, int octaves, float lacunarity, float gain) {
    float sum   = 0.0;
    float amp   = 1.0;
    float freq  = 1.0;
    float total = 0.0;
    for (int i = 0; i < octaves; ++i) {
        sum   += perlin_noise(p * freq) * amp;
        total += amp;
        freq  *= lacunarity;
        amp   *= gain;
    }
    return sum / total;
}

#endif // UZ_NOISE_HLSLI
```

### `global_cbuffer.hlsli` -- Global Parameters

```hlsl
#ifndef UZ_GLOBAL_CBUFFER_HLSLI
#define UZ_GLOBAL_CBUFFER_HLSLI

// Global parameters available to ALL shaders
// Bound at register b15 — reserved slot, never used by materials
// Updated once per frame by the engine

cbuffer GlobalParams : register(b15) {
    float  g_Time;            // seconds since engine start (wraps at 3600s)
    float  g_DeltaTime;       // frame delta time in seconds
    float2 g_ScreenSize;      // viewport width, height in pixels

    float3 g_WindDirection;   // normalized world-space wind direction
    float  g_WindStrength;    // wind intensity [0..1] — used by vegetation shaders

    float4 g_FogColor;       // fog color (RGB) + unused (A)
    float  g_FogDensity;     // exponential fog density [0..1]
    float  g_FogStart;       // linear fog start distance (meters)
    float  g_FogEnd;         // linear fog end distance (meters)
    float  g_FogMode;        // 0 = off, 1 = linear, 2 = exponential, 3 = exp2
};

// Fog computation helpers
float compute_fog_factor(float distance_to_camera) {
    if (g_FogMode < 0.5) return 0.0; // off

    float fog = 0.0;
    if (g_FogMode < 1.5) {
        // Linear fog
        fog = saturate((distance_to_camera - g_FogStart) / (g_FogEnd - g_FogStart));
    } else if (g_FogMode < 2.5) {
        // Exponential fog
        fog = 1.0 - exp(-g_FogDensity * distance_to_camera);
    } else {
        // Exponential squared fog
        float f = g_FogDensity * distance_to_camera;
        fog = 1.0 - exp(-f * f);
    }
    return saturate(fog);
}

float3 apply_fog(float3 color, float3 world_pos, float3 camera_pos) {
    float dist = length(world_pos - camera_pos);
    float fog  = compute_fog_factor(dist);
    return lerp(color, g_FogColor.rgb, fog);
}

#endif // UZ_GLOBAL_CBUFFER_HLSLI
```

---

## DXC Include Path

The shader compiler passes `-I shaders/include` to DXC so that `#include "common.hlsli"` resolves correctly:

```
dxc.exe -T ps_6_0 -E PSMain -I shaders/include shaders/builtin/pbr.hlsl
```

### ShaderCompiler API

```cpp
// src/renderer/shader_compiler/shader_compiler.h

// Set the root include path for all shader compilations
void set_include_path(const char* path);

// Get list of files included by a shader (for dependency tracking)
// Returned paths are relative to include_path
Array<StringId> get_includes(const char* shader_path, Arena* scratch);
```

---

## Include Dependency Tracking

The shader compiler tracks which `.hlsli` files are included by each shader. This enables correct hot-reload when a shared include file changes.

### How It Works

1. When compiling a shader, DXC reports included files via `IDxcResult::GetOutput(DXC_OUT_EXTRA_OUTPUTS)`.
2. The compiler stores the mapping: `shader_path -> [included_file_1, included_file_2, ...]`.
3. When a `.hlsli` file changes (detected by file watcher):
   a. Find all shaders that include the changed file (reverse lookup).
   b. Recompile all variants of each affected shader.
   c. Swap PSOs atomically between frames.

### Data Structures

```cpp
// src/renderer/shader_compiler/include_tracker.h

struct IncludeTracker {
    // Forward: shader -> set of includes it uses
    HashMap<StringId, Array<StringId>> shader_includes;

    // Reverse: include file -> set of shaders that depend on it
    HashMap<StringId, Array<StringId>> include_dependents;

    // Update after a shader compilation
    void update(StringId shader_path, Span<StringId> includes);

    // Get all shaders affected by an include file change
    Span<StringId> get_dependents(StringId include_path) const;
};
```

### Hot-Reload Cascade

```
common.hlsli changed
    ↓ (reverse lookup)
    ├── pbr.hlsl uses common.hlsli → recompile all pbr.hlsl variants
    ├── unlit.hlsl uses common.hlsli → recompile all unlit.hlsl variants
    └── skybox.hlsl uses common.hlsli → recompile all skybox.hlsl variants
```

Transitive includes are tracked: if `brdf.hlsli` includes `common.hlsli`, and `pbr.hlsl` includes `brdf.hlsli`, then changing `common.hlsli` triggers recompilation of `pbr.hlsl`.

---

## CPU-Side GlobalParams Upload

The `GlobalParams` cbuffer is updated once per frame and bound at `b15` in the root signature:

```cpp
// src/renderer/global_params.h

struct GlobalParamsData {
    float    time;
    float    delta_time;
    float    screen_width;
    float    screen_height;

    float    wind_dir[3];
    float    wind_strength;

    float    fog_color[4];
    float    fog_density;
    float    fog_start;
    float    fog_end;
    float    fog_mode;
};
static_assert(sizeof(GlobalParamsData) == 64, "GlobalParams must be 64 bytes (4 float4s)");

// Updated in render_frame() before any pass executes
void update_global_params(GlobalParamsData* dst, const FrameContext& ctx);
```

### Root Signature

`b15` is reserved as a global CBV in the root signature, always bound:

```
Root Param 0: b0 (Per-Object CBV)
Root Param 1: b1 (Per-Frame CBV)
Root Param 2: Descriptor Table (SRVs t0..t7)
Root Param 3: Descriptor Table (Samplers s0..s3)
Root Param 4: b15 (GlobalParams CBV) ← always bound
```

---

## IPC Handlers

```json
shader.setGlobalParam { "name": "g_WindStrength", "value": 0.8 }
                  --> { "ok": true }

shader.getGlobalParams {}
                  --> {
                        "g_Time": 42.5,
                        "g_DeltaTime": 0.016,
                        "g_WindDirection": [1, 0, 0],
                        "g_WindStrength": 0.5,
                        "g_FogColor": [0.7, 0.7, 0.8, 1.0],
                        "g_FogDensity": 0.02,
                        "g_FogStart": 50.0,
                        "g_FogEnd": 500.0
                      }
```

---

## Usage Example

A shader that uses the include library:

```hlsl
// shaders/builtin/pbr.hlsl

#include "common.hlsli"
#include "brdf.hlsli"
#include "lighting.hlsli"
#include "shadow.hlsli"
#include "global_cbuffer.hlsli"

#ifdef SKINNED
#include "skinning.hlsli"
#endif

// ... rest of shader uses shared functions
float4 PSMain(VSOut i) : SV_TARGET {
    // Reconstruct from G-Buffer or forward input
    float3 N = normalize(i.normal);
    float3 V = normalize(camera_pos - i.world);

    float3 color = float3(0, 0, 0);

    // Directional light + CSM shadow
    float shadow = shadow_pcf_3x3(shadow_atlas, shadow_sampler,
                                   world_to_shadow(i.world, cascade_vp[cascade_idx]),
                                   texel_size);
    color += eval_dir_light(dir_light, N, V, i.world, albedo, roughness, metallic, shadow);

    // Point lights
    for (uint p = 0; p < num_point_lights; ++p) {
        color += eval_point_light(point_lights[p], N, V, i.world, albedo, roughness, metallic);
    }

    // Fog
    color = apply_fog(color, i.world, camera_pos);

    // Tonemapping is done in post-process pass
    return float4(color, 1.0);
}
```
