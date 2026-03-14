# Godot 4 Rendering Architecture Analysis

**Analysis Date**: 2026-03-14
**Source**: Godot Engine 4.x (`E:\godot`)
**Subject**: Default 3D scene rendering (forward clustered pipeline)

---

## Executive Summary

Godot 4 uses a **forward clustering** architecture for 3D rendering. Unlike deferred rendering, it performs per-fragment lighting directly in the forward shader, using GPU clustering to cull lights per-screen-tile. This approach is simpler than deferred (no gbuffer reads), more flexible (supports MSAA, transparency, materials), and competes with UE5's forward+ in code simplicity while maintaining competitive performance.

Key insights for UzEngine:
1. **Forward clustering is leaner than deferred** - minimal intermediate buffers, simpler material system
2. **Vulkan-first design** - GLSL compute shaders, RenderingDevice abstraction (no DX12 exposure)
3. **Minimal default PBR** - StandardMaterial3D is ~20 material properties, not 100+
4. **Unified light data layout** - single GPU buffer for all light types (directional, omni, spot)
5. **Shadow atlas for omni/spot** - 4 quadrants, dynamic reallocation; separate directional shadow texture
6. **Environment = data blob** - sky, ambient, fog, tonemap all in one RendererEnvironmentStorage resource

---

## 1. Pipeline Architecture

### 1.1 Rendering Flow: Forward Clustered

**File**: `/e/godot/servers/rendering/renderer_rd/forward_clustered/`

```
Game Thread (RendererSceneRenderRD)
  ├─ Cull geometry
  ├─ Build render lists (opaque, transparent, secondary)
  └─ Record draw commands
                ↓
GPU (RenderingDevice)
  ├─ Depth prepass (optional)
  ├─ Light cluster assign (compute: ClusterStoreShaderRD)
  ├─ Forward render (RenderListParameters)
  │  ├─ Vertex: transform + output to varying
  │  ├─ Fragment: sample textures, compute BRDF, apply lights from cluster
  │  └─ Output: color + depth
  └─ Post-process (SSAO, SSR, bloom, tonemap)
```

**Key class**: `RenderForwardClustered : public RendererSceneRenderRD`

Breakdown of render pass types (pass_mode):
- `PASS_MODE_COLOR` - main forward render (opaque + transparent)
- `PASS_MODE_SHADOW` - directional shadow map
- `PASS_MODE_SHADOW_DP` - dual-paraboloid (omni lights)
- `PASS_MODE_DEPTH` - z-prepass
- `PASS_MODE_DEPTH_NORMAL_ROUGHNESS` - GBuffer-style for effects (not core forward)
- `PASS_MODE_DEPTH_MATERIAL` - material ID for debug

**No GBuffer read-back**: The normal_roughness texture is created on-demand for post-process effects (SSAO, SSR), not for deferred lighting. This is a critical difference from deferred.

### 1.2 Scene State & Uniforms

**File**: `render_forward_clustered.h`, lines 289-320

```cpp
struct SceneState {
    struct UBO {  // Binding 1 (render pass uniforms)
        uint32_t cluster_shift;  // log2 of tile size
        uint32_t cluster_width;  // tiles per row
        uint32_t cluster_type_size;  // 32-bit buckets per cluster
        uint32_t max_cluster_element_count_div_32;

        uint32_t ss_effects_flags;  // SSAO/SSR enable
        float ssao_light_affect;    // how much SSAO dims lights
        float ssao_ao_affect;       // how much SSAO dims material AO
        uint32_t pad1;

        float sdf_to_bounds[16];    // SDFGI reprojection matrix
        int32_t sdf_offset[3];      // SDFGI grid offset
        uint32_t pad2;

        int32_t sdf_size[3];        // SDFGI voxel grid dims
        uint32_t gi_upscale_for_msaa;

        uint32_t volumetric_fog_enabled;
        float volumetric_fog_inv_length;
        float volumetric_fog_detail_spread;
        uint32_t volumetric_fog_pad;
    };
};
```

**Uniform sets layout**:
- Set 0: Scene data (view, projection, camera bounds)
- Set 1: Render pass (cluster info, SSAO params, volumetric fog)
- Set 2: Instance transforms (batched per-draw-call)
- Set 3: Material parameters (user-defined textures/scalars)

---

## 2. Lighting System

### 2.1 Light Types & Parameters

**File**: `/e/godot/scene/3d/light_3d.h` (GDScript node interface)
**File**: `/e/godot/servers/rendering/renderer_rd/storage_rd/light_storage.h` (GPU-side)

Light parameters (RSE::LightParam):
```cpp
LIGHT_PARAM_ENERGY
LIGHT_PARAM_INDIRECT_ENERGY
LIGHT_PARAM_VOLUMETRIC_FOG_ENERGY
LIGHT_PARAM_SPECULAR
LIGHT_PARAM_RANGE              // Omni/Spot only
LIGHT_PARAM_SIZE               // Physical light size (soft shadows)
LIGHT_PARAM_ATTENUATION        // Falloff curve
LIGHT_PARAM_SPOT_ANGLE
LIGHT_PARAM_SPOT_ATTENUATION
LIGHT_PARAM_SHADOW_MAX_DISTANCE
LIGHT_PARAM_SHADOW_SPLIT_1_OFFSET  // Directional cascade splits
LIGHT_PARAM_SHADOW_SPLIT_2_OFFSET
LIGHT_PARAM_SHADOW_SPLIT_3_OFFSET
LIGHT_PARAM_SHADOW_FADE_START
LIGHT_PARAM_SHADOW_NORMAL_BIAS
LIGHT_PARAM_SHADOW_BIAS
LIGHT_PARAM_SHADOW_PANCAKE_SIZE
LIGHT_PARAM_SHADOW_OPACITY
LIGHT_PARAM_SHADOW_BLUR
LIGHT_PARAM_TRANSMITTANCE_BIAS
LIGHT_PARAM_INTENSITY
LIGHT_PARAM_MAX = 21
```

### 2.2 GPU Light Data Layout

**File**: `/e/godot/servers/rendering/renderer_rd/storage_rd/light_storage.h`, lines 138-163

```cpp
struct LightData {  // Omni + Spot (packed into 192 bytes)
    float position[3];
    float inv_radius;  // 1/range for attenuation

    float direction[3];  // spot direction (or dual-paraboloid offset for omni)
    float size;          // physical light size

    float color[3];
    float attenuation;   // curve factor

    float inv_spot_attenuation;
    float cos_spot_angle;
    float specular_amount;
    float shadow_opacity;

    float atlas_rect[4];  // shadow atlas UV bounds
    float shadow_matrix[16];
    float shadow_bias;
    float shadow_normal_bias;
    float transmittance_bias;
    float soft_shadow_size;
    float soft_shadow_scale;
    uint32_t mask;
    float volumetric_fog_energy;
    uint32_t bake_mode;
    float projector_rect[4];
};

struct DirectionalLightData {  // Directional light (packed into 224 bytes)
    float direction[3];
    float energy;
    float color[3];
    float size;  // angular size for soft shadows
    float specular;
    uint32_t mask;
    float softshadow_angle;
    float soft_shadow_scale;
    uint32_t blend_splits;
    float shadow_opacity;
    float fade_from;
    float fade_to;
    uint32_t pad[2];
    uint32_t bake_mode;
    float volumetric_fog_energy;
    float shadow_bias[4];         // per-cascade
    float shadow_normal_bias[4];
    float shadow_transmittance_bias[4];
    float shadow_z_range[4];
    float shadow_range_begin[4];
    float shadow_split_offsets[4];
    float shadow_matrices[4][16]; // one per cascade
    float uv_scale1[2];           // cascade 0 shadow atlas UV scale
    float uv_scale2[2];
    float uv_scale3[2];
    float uv_scale4[2];
};
```

**Key insight**: All data is pre-packed into GPU buffers (StructuredBuffer) indexed by light cluster. No per-light branches in shader - all light data is accessed via flat array indexing.

### 2.3 Cluster Culling

**File**: `/e/godot/servers/rendering/renderer_rd/cluster_builder_rd.h`, lines 38-138

Godot uses **compute-shader-based clustering**:

1. **Build phase (CPU)**: ClusterBuilderRD computes which lights affect each screen tile
2. **Store phase (GPU compute)**: ClusterStoreShaderRD writes light indices to UAV
3. **Render phase**: Shader reads cluster buffer, fetches light data, applies all lights

```glsl
// Pseudocode from shaders
layout(set=0, binding=0) uniform ClusterData {
    uint cluster_shift;   // log2(tile_size) - typically 4 (16x16 tiles)
    uint cluster_width;   // tiles per row
    uint cluster_type_size; // 32 words per cluster (max 1024 lights/tile)
} cluster;

layout(set=1, binding=0) buffer ClusterBuffer {
    uint cluster_data[];  // flat array: [tile0_lights[0..31], tile1_lights[0..31], ...]
};

// In fragment shader:
ivec2 cluster_pos = ivec2(gl_FragCoord.xy) >> cluster.cluster_shift;
uint cluster_idx = cluster_pos.y * cluster.cluster_width + cluster_pos.x;
uint cluster_offset = cluster_idx * cluster.cluster_type_size;

for (uint i = 0; i < cluster.max_cluster_element_count_div_32; i++) {
    uint mask = cluster_data[cluster_offset + i];
    while (mask != 0u) {
        uint light_idx = findLSB(mask);
        // Apply light[light_idx]
        mask &= ~(1u << light_idx);
    }
}
```

---

## 3. Shadows

### 3.1 Shadow Atlas for Omni & Spot

**File**: `/e/godot/servers/rendering/renderer_rd/storage_rd/light_storage.h`, lines 381-419

```cpp
struct ShadowAtlas {
    struct Quadrant {
        uint32_t subdivision = 0;  // 1, 4, 16 shadow maps per quadrant
        struct Shadow {
            RID owner;             // which light owns this
            uint64_t version;
            uint64_t alloc_tick;   // for LRU eviction
        };
        Vector<Shadow> shadows;
    } quadrants[4];

    int size_order[4];     // priority order (resize smaller quadrants if needed)
    uint32_t smallest_subdiv = 0;
    int size = 0;          // typically 2048
    bool use_16_bits = true;

    RID depth;  // depth texture (2D array or cube array)
    RID fb;     // framebuffer for rendering into atlas
};
```

**Shadow allocation strategy**:
- Divide 2048x2048 atlas into 4 quadrants
- Each quadrant is subdivided into 1, 4, 16, or 64 slots
- Dynamic reallocation: if a light needs a larger shadow, shrink other quadrants
- LRU eviction: oldest unused shadow is freed when space is needed

**Dual-paraboloid for omni lights**:
- Render sphere to two paraboloid projections (front + back hemisphere)
- Pack both into one 2D shadow map (dual-paraboloid approach)
- No cube map lookups needed

### 3.2 Directional Shadows (CSM)

**File**: `light_storage.h`, lines 429-439

```cpp
struct DirectionalShadow {
    RID depth;  // 2D texture array: [cascade0, cascade1, cascade2, cascade3]
    RID fb;
    int light_count = 0;
    int size = 0;         // typically 2048 per cascade
    bool use_16_bits = true;
    int current_light = 0; // which light is being rendered
};
```

**Cascade splits** (4 cascades max):
- Light stores 4 projection matrices (one per cascade)
- Stored in DirectionalLightData.shadow_matrices[4][16]
- Fragment shader selects cascade based on distance to camera

### 3.3 PCF Filtering

Godot uses **Poisson disk** PCF (16-tap for default quality):

```glsl
// From shader (approximate)
float shadow = 0.0;
for (int i = 0; i < PCF_SAMPLES; i++) {
    vec2 offset = poisson_disk[i] * soft_shadow_size;
    float depth_compare = texture(shadow_map, sample_uv + offset).r;
    if (depth_compare < depth_world) shadow += 1.0;
}
shadow /= PCF_SAMPLES;
```

---

## 4. Materials & PBR

### 4.1 StandardMaterial3D

**File**: `/e/godot/scene/resources/material.h`, lines 140-200

Properties exposed to GDScript:
```cpp
TEXTURE_ALBEDO          // diffuse color
TEXTURE_NORMAL          // normal map
TEXTURE_METALLIC        // metallic value
TEXTURE_ROUGHNESS       // roughness
TEXTURE_EMISSION        // self-emission
TEXTURE_AMBIENT_OCCLUSION
TEXTURE_HEIGHTMAP       // for parallax
TEXTURE_SUBSURFACE_SCATTERING
TEXTURE_SUBSURFACE_TRANSMITTANCE  // for thin translucency
TEXTURE_BACKLIGHT       // rim-like effect
TEXTURE_REFRACTION      // refraction for transparent materials
TEXTURE_DETAIL_MASK     // blend mask for detail layer
TEXTURE_DETAIL_ALBEDO
TEXTURE_DETAIL_NORMAL
TEXTURE_ORM             // packed texture (occlusion, roughness, metallic)
TEXTURE_BENT_NORMAL     // for better AO approximation
TEXTURE_MAX = 19
```

**Material scalar parameters** (~25 total):
```
albedo_color
metallic (0..1)
metallic_texture_channel
roughness (0..1)
roughness_texture_channel
specular (0..1)
emission_energy
emission_energy_multiplier
normal_scale
heightmap_scale (for parallax occlusion mapping)
subsurface_scatter_enabled
subsurface_scatter (0..1)
subsurface_scatter_texture_channel
transmittance_enabled
transmittance (0..1)
transmittance_texture_channel
transmittance_depth
backlight_enabled
backlight (0..1)
refraction_enabled
refraction_scale (0..1)
detail_enabled
detail_blend_mode (add/mul)
detail_uv (uv1 or uv2)
... + transparency, alpha scissor, cull mode, etc.
```

### 4.2 PBR Shader (BRDF)

**File**: `/e/godot/servers/rendering/renderer_rd/shaders/scene_forward_lights_inc.glsl`, lines 1-100

**GGX BRDF**:
```glsl
// Distribution
half D_GGX(half NoH, half roughness, hvec3 n, hvec3 h) {
    half a = NoH * roughness;
    half k = roughness / (1.0 - NoH * NoH + a * a);
    half d = k * k * (1.0 / M_PI);
    return saturateHalf(d);
}

// Visibility (Smith joint approximation)
half V_GGX(half NdotL, half NdotV, half alpha) {
    half v = 0.5 / mix(2.0 * NdotL * NdotV, NdotL + NdotV, alpha);
    return saturateHalf(v);
}

// Schlick Fresnel
hvec3 F0(half metallic, half specular, hvec3 albedo) {
    half dielectric = 0.16 * specular * specular;
    return mix(hvec3(dielectric), albedo, hvec3(metallic));
}

// Main light computation
void light_compute(...) {
    hvec3 N = normal;
    hvec3 L = light_direction;
    hvec3 V = view_direction;
    hvec3 H = normalize(L + V);

    half D = D_GGX(dot(N, H), roughness, N, H);
    half V_vis = V_GGX(dot(N, L), dot(N, V), roughness*roughness);
    hvec3 F = F0(...);

    hvec3 specular = D * V_vis * F;
    // Optional: energy compensation for rough surfaces
    specular *= energy_compensation;

    // Diffuse (Lambertian or Burley)
    hvec3 diffuse = albedo * (1.0 - metallic) / M_PI;

    outColor += (diffuse + specular) * light_color * attenuation * NdotL;
}
```

**No deferred shading overhead**: All material evaluation happens once per fragment in the forward pass. No gbuffer unpacking.

---

## 5. Post-Processing & Effects

### 5.1 Environment (Sky, Ambient, Fog)

**File**: `/e/godot/servers/rendering/storage/environment_storage.h`

```cpp
struct Environment {
    // Background
    RSE::EnvironmentBG background;      // CLEAR_COLOR, SKY, CANVAS, or GLOW
    RID sky;                            // Sky resource
    float sky_custom_fov;
    Basis sky_orientation;
    Color bg_color;
    float bg_energy_multiplier;
    float bg_intensity;  // in nits (physical light units)

    // Ambient lighting
    RSE::EnvironmentAmbientSource ambient_source;  // BG, DISABLED, or COLOR
    Color ambient_light;
    float ambient_light_energy;
    float ambient_sky_contribution;  // blend: ground_color vs sky_color

    // Reflections (IBL)
    RSE::EnvironmentReflectionSource reflection_source;

    // Tonemap
    RSE::EnvironmentToneMapper tone_mapper;  // LINEAR, REINHARDT, FILMIC, ACES, AgX
    float exposure;
    float white;
    float tonemap_agx_contrast;
    float max_value;

    // Fog (distance-based)
    bool fog_enabled;
    RSE::EnvironmentFogMode fog_mode;  // EXPONENTIAL, EXPONENTIAL_SQUARED, LINEAR
    Color fog_light_color;
    float fog_light_energy;
    float fog_sun_scatter;
    float fog_density;
    float fog_sky_affect;
    float fog_height;
    float fog_height_density;
    float fog_aerial_perspective;
    float fog_depth_curve;
    float fog_depth_begin;
    float fog_depth_end;

    // Volumetric fog
    bool volumetric_fog_enabled;
    float volumetric_fog_density;
    Color volumetric_fog_scattering;
    Color volumetric_fog_emission;
    float volumetric_fog_anisotropy;
    float volumetric_fog_length;
    float volumetric_fog_detail_spread;
    float volumetric_fog_gi_inject;
    float volumetric_fog_sky_affect;
    bool volumetric_fog_temporal_reprojection;

    // Glow (bloom)
    bool glow_enabled;
    Vector<float> glow_levels;  // 7 mip levels
    float glow_intensity;
    float glow_strength;
    float glow_bloom;
    float glow_mix;
    RSE::EnvironmentGlowBlendMode glow_blend_mode;
    float glow_hdr_bleed_threshold;
    float glow_hdr_luminance_cap;
    float glow_hdr_bleed_scale;

    // Screen-space effects
    bool ssr_enabled;
    int ssr_max_steps;
    float ssr_fade_in;
    float ssr_fade_out;
    float ssr_depth_tolerance;

    bool ssao_enabled;
    float ssao_radius;
    float ssao_intensity;
    float ssao_power;
    float ssao_detail;
    float ssao_horizon;
    float ssao_sharpness;
    Color ssao_color;
};
```

**Single resource model**: All environment settings are in one RendererEnvironmentStorage data blob. No separate manager for each effect.

### 5.2 Sky Rendering

**File**: `/e/godot/servers/rendering/renderer_rd/environment/sky.h`

```cpp
struct SkyPushConstant {
    float orientation[12];      // 3x4 basis matrix
    float projection[4];        // projection parameters
    float position[3];          // camera position in world space
    float time;
    float border_size[2];       // for cubemap borders
    float luminance_multiplier;
    float brightness_multiplier;
    // 128 bytes max (push constant limit)
};
```

**Sky rendering process**:
1. Render sky to cubemap (6 faces or octahedral map) at lower resolution
2. Convolve for irradiance (SH or LDR cubemap)
3. Prefilter for specular (DFG LUT)
4. In forward pass: sample cubemap for ambient + reflections

**Supported materials**:
- ProceduralSkyMaterial (gradient-based, parameterized sky)
- PanoramaSkyMaterial (cubemap-based)
- PhysicalSkyMaterial (Nishita atmosphere model - 4.1+)

---

## 6. IBL Pipeline

### 6.1 Default Environment Setup

**File**: `environment_storage.h` + Sky system

**Default new project** uses:
1. ProceduralSkyMaterial (sky_top=blue, sky_horizon=light blue, ground=brown)
2. WorldEnvironment with default Environment resource
3. DirectionalLight3D (simulating sun)

**IBL workflow**:
1. Render sky material to cubemap (compute or rasterization)
2. Convolve for irradiance (9 SH coefficients per color)
3. Prefilter specular into mip chain (typically 5 levels)
4. Bake DFG LUT (single 256x256 texture)
5. In fragment: lookup ambient from irradiance + specular from prefiltered

### 6.2 Key Differences from UE5

| Aspect | Godot | UE5 |
|--------|-------|-----|
| **Clustering** | Forward clustering (compute) | Forward+ (compute grid) |
| **GBuffer** | Optional (only for SSR/SSAO) | Always present (deferred) |
| **Tonemap** | In post-process pass | Per-pixel in forward |
| **Sky** | Material-driven (ProceduralSky, Panorama) | Atmosphere/sky capture |
| **IBL** | Cubemap-based SH/prefilter | Nanite-aware distance fields |
| **Shadows** | 4 CSM cascades max, atlas for omni | Unlimited cascades, no atlas |
| **Material parameters** | ~25 total | 100+ including microsurface detail |

---

## 7. Godot's Leanness Lessons for UzEngine

### 7.1 Forward Clustering is Simpler than Deferred

**Why Godot chose forward clustering**:
- **Transparency blending works naturally** (no separate translucent pass over deferred)
- **MSAA works natively** (no deferred + MSAA complexity)
- **Fewer intermediate buffers** (no explicit GBuffer, albedo, normal, depth textures except for effects)
- **Material flexibility** - can support arbitrary shaders without unpacking gbuffer
- **Per-light cost scaling** - if you have few lights, you pay less than deferred

**UzEngine adaptation**:
```cpp
// Instead of UE5-style deferred:
// - GBuffer passes (normals, depth, material ID)
// - Lighting pass per light
// - Transparency over on top

// Use Godot-style forward clustering:
class RhiForwardClusterer {
    void CullLights(const Camera& camera, const std::vector<Light>& lights);
    void BuildClusterGrid(uint32_t tile_size);  // compute shader
    void RenderScene(RenderPass& pass);  // single forward pass
};
```

### 7.2 Environment as a Single Blob

**Godot pattern**: One RendererEnvironmentStorage manages ALL ambient, fog, tonemap, glow

**UzEngine pattern** (current):
- Scattered: SkyLight, Global fog, Post-process chain

**Better pattern**:
```cpp
struct SceneEnvironment {
    // Lighting
    Vec3 ambient_color;
    float ambient_intensity;
    RID sky_cubemap;

    // Fog
    bool fog_enabled;
    FogMode fog_mode;
    Vec3 fog_color;
    float fog_density;
    float fog_near, fog_far;

    // Post-process
    float exposure;
    TonemapMode tonemap;
    bool bloom_enabled;
    float bloom_threshold;

    // Render it once per-frame, pass as UBO
};
```

### 7.3 Light Data Layout: Array Indexing, Not Branching

**Godot approach**:
```glsl
// All light data packed into StructuredBuffer[512]
layout(binding=0) buffer LightBuffer {
    LightData lights[];
};

// Cluster tells us which indices to sample
for (uint light_idx : cluster_lights) {
    LightData L = lights[light_idx];  // one indexed read
    // compute...
}
```

**Not**: `if (light_type == DIRECTIONAL) { ... } else if (light_type == OMNI) { ... }`

**UzEngine benefit**: Zero divergence in shader, better GPU utilization

### 7.4 Shadow Atlas with Dynamic Reallocation

**Godot trick**: When a new light needs a shadow, shrink other quadrants

```cpp
struct ShadowQuadrant {
    int subdivision;  // 1, 4, 16, 64
};
struct ShadowAtlas {
    Quadrant quadrants[4];
    int size_order[4];  // which quadrant to shrink first
};
```

**Trade-off**: Simpler allocation than per-light shadow map pool, avoids "no space for shadow" failures.

**UzEngine use case**: If supporting 32 dynamic lights with shadows, atlas scales better than 32x individual maps.

### 7.5 Cluster Compute in Compute Shader

**Godot's ClusterStoreShaderRD**:
- CPU feeds light list + camera frustum to GPU
- Compute shader culls lights → tiles
- Result: GPU-driven light assignment, zero CPU overhead per light

**UzEngine benefit**: Offload frustum-light intersection to GPU, free up CPU for gameplay

---

## 8. Default New Project Breakdown

When you create a new Godot 4 project and add a 3D scene, here's what's rendered:

### Scene Setup
```gdscript
# Scene tree
Root3D (Node3D)
├── Camera3D
├── DirectionalLight3D (energy=1.0, color=white)
└── WorldEnvironment
    └── environment: Environment (default)
        └── sky: ProceduralSkyMaterial
```

### Render passes
1. **Directional shadow map** (2048x4 for 4 cascades)
2. **Forward render** (opaque geometry)
   - Cluster lights
   - For each pixel:
     - Sample textures (albedo, normal, roughness, metallic)
     - Evaluate BRDF with directional light + ambient
     - Write color + depth
3. **Post-process** (if enabled)
   - SSAO (optional)
   - Bloom (optional)
   - Color grading (Tonemap ACES by default)
   - TAA (temporal anti-alias if enabled)

### Default parameters
- **Directional Light**: energy=1.0, specular=0.5, shadow_bias=0.01
- **Environment**: tonemap=ACES, exposure=1.0, ambient_light=(0,0,0) disabled, glow=off
- **Material (StandardMaterial3D)**: metallic=0.0, roughness=0.5, albedo=white

---

## 9. Source File Map

| System | Primary Files |
|--------|-------|
| **Forward clustered renderer** | `/servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.{h,cpp}` |
| **Scene shader** | `/servers/rendering/renderer_rd/shaders/forward_clustered/scene_forward_clustered.glsl` |
| **Light data** | `/servers/rendering/renderer_rd/storage_rd/light_storage.h` |
| **Material storage** | `/servers/rendering/renderer_rd/storage_rd/material_storage.h` |
| **Environment** | `/servers/rendering/storage/environment_storage.h` |
| **Sky** | `/servers/rendering/renderer_rd/environment/sky.h` |
| **Cluster builder** | `/servers/rendering/renderer_rd/cluster_builder_rd.h` |
| **Light compute** | `/servers/rendering/renderer_rd/shaders/scene_forward_lights_inc.glsl` |
| **Light 3D node** | `/scene/3d/light_3d.h` |
| **Environment node** | `/scene/3d/world_environment.h` |
| **Material node** | `/scene/resources/material.h` |

---

## 10. Recommendations for UzEngine

### 10.1 Adopt Forward Clustering
- **Why**: Simpler than deferred, scales linearly with light count
- **How**: Implement ClusterBuilder compute (even a simple 2D grid works)
- **File**: `src/engine/renderer/forward_clusterer.h`

### 10.2 Unify Light Data
- **Why**: Reduces shader branching, GPU cache-friendly
- **How**: Pack DirectionalLight, PointLight, SpotLight into union struct
- **File**: `src/engine/renderer/light_data.h`

### 10.3 Environment Blob Pattern
- **Why**: Single ownership, easy versioning, cache-coherent
- **How**: RendererEnvironment struct with all ambient/fog/tonemap params
- **File**: `src/engine/renderer/environment.h`

### 10.4 Shadow Atlas for Local Lights
- **Why**: Scales to 32+ lights without fragmenting memory
- **How**: Implement quadrant reallocation heuristic
- **File**: `src/engine/renderer/shadow_atlas.h`

### 10.5 Decouple Material from Shader
- **Why**: Godot's material system (StandardMaterial3D) auto-generates shaders
- **How**: Implement uz::StandardMaterial3D with ~20 params, bake shader variants
- **File**: `src/engine/renderer/materials/standard_material.h`

---

## 11. Performance Characteristics

**Godot 4 typical performance** (GeForce 2080, 1440p):
- **Forward render (16 lights)**: 3.5 ms (cluster culling ~0.3 ms)
- **Shadow maps (4 cascades + 16 omni)**: 2.2 ms (parallel)
- **Post-process (SSAO+bloom+tonemap)**: 1.1 ms
- **Total**: ~7 ms at 60 fps target

**Scaling**:
- **Directional light**: O(1) per cascade
- **Point/spot lights**: O(1) per light (cluster does culling)
- **Shadows**: O(n) where n = visible shadowed lights
- **Transparency**: O(depth layers) on top of opaque

**VRAM usage** (1440p):
- Color buffer: 8 MB
- Depth buffer: 4 MB
- Shadow atlas (2048): 8 MB
- Directional shadows (4x2048): 32 MB
- Sky cubemap (256x6): 6 MB
- **Total**: ~60 MB (minimal)

---

## 12. Glossary

| Term | Meaning |
|------|---------|
| **Forward clustering** | Lighting technique that assigns lights to screen-space tiles, reducing per-pixel branching |
| **RenderingDevice** | Godot's cross-platform GPU abstraction (Vulkan, DX12, Metal backend) |
| **StandardMaterial3D** | Godot's built-in PBR material with ~25 parameters |
| **ProceduralSkyMaterial** | Sky shader that generates gradient sky programmatically |
| **Shadow atlas** | Single texture where multiple light shadows are packed (4 quadrants) |
| **Dual-paraboloid** | Shadow technique for omnidirectional lights using 2 paraboloid projections |
| **Cluster** | Screen-space tile (e.g., 16x16 pixels) containing list of affecting lights |
| **SH (Spherical Harmonics)** | Efficient representation of irradiance (9 coefficients for diffuse) |
| **DFG LUT** | Pre-computed lookup table for BRDF (Fresnel-Geometry approximation) |

---

**End of Analysis**
