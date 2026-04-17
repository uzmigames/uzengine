# Godot 4 vs UzEngine Rendering: Side-by-Side Comparison

**Date**: 2026-03-14
**Context**: How Godot solves rendering challenges with minimal code complexity

---

## 1. Pipeline Architecture

### Godot: Forward Clustering

```cpp
// File: /e/godot/servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.h
class RenderForwardClustered : public RendererSceneRenderRD {
    // Per-frame work
    void _render_scene(const RenderDataRD *p_render_data, ...) {
        // 1. Cull geometry
        _cull_scene(scene, frustum);

        // 2. Sort by material (opaque, alpha, secondary)
        _fill_render_lists(scene, camera, ...);

        // 3. Render directional shadows (if needed)
        for (int i = 0; i < directional_light_count; i++) {
            _render_shadow_pass(PASS_MODE_SHADOW, ...);
        }

        // 4. Render omni/spot shadows to atlas
        _render_shadow_atlas(...);

        // 5. Build light clusters (GPU compute)
        cluster_builder->build(lights, camera, ...);

        // 6. Forward render (single pass)
        _render_list(PASS_MODE_COLOR, opaque_list, ...);
        _render_list(PASS_MODE_COLOR, alpha_list, ...);

        // 7. Post-process
        _apply_bloom_aces_tonemap(...);
        _apply_ssao_ssr(...);
    }

    RenderBufferDataForwardClustered framebuffer_data;
    ClusterBuilderRD cluster_builder;
    SceneState scene_state;
};
```

**Key insight**: Single render pass for all lights. Clustering happens on GPU.

### UzEngine (Current): Decoupled Passes

```cpp
// File: e:/UzEngine/src/engine/renderer/renderer.h
class Renderer {
    void RenderFrame(Scene& scene, Camera& camera) {
        // 1. Render CSM cascades
        for (int i = 0; i < 4; i++) {
            render_pass_manager->Render(
                shadow_pass_cascade[i],
                directional_light.GetShadowMatrix(i)
            );
        }

        // 2. Render point light shadows (if <= 8)
        for (int i = 0; i < point_lights.size(); i++) {
            if (point_lights[i].shadow_enabled) {
                render_pass_manager->Render(shadow_pass_point[i], ...);
            }
        }

        // 3. Light culling (currently on CPU!)
        std::vector<Light*> visible_lights;
        for (auto& light : lights) {
            if (camera.frustum.Intersects(light.bounds)) {
                visible_lights.push_back(&light);
            }
        }

        // 4. GBuffer (deferred) pass
        render_pass_manager->Render(gbuffer_pass, ...);

        // 5. Lighting pass (per-light)
        for (auto light : visible_lights) {
            render_pass_manager->Render(lighting_pass, light);
        }

        // 6. Transparency (forward)
        render_pass_manager->Render(forward_transparent_pass, ...);

        // 7. Post-process
        bloom.Apply();
        tonemapper.Apply();
    }
};
```

**Problem**:
- CPU culling (light frustum test)
- Deferred overhead (GBuffer reads)
- Per-light pass = more draw calls

### UzEngine (Recommended): Forward Clustering

```cpp
// Adopt Godot's approach
class ForwardClusterer {
    struct Cluster {
        uint32_t light_indices[8];  // up to 8 lights per tile
        uint32_t light_count;
    };

    void Build(const Camera& camera, const Light* lights, size_t light_count) {
        // Compute on GPU
        ClusterBuildShader shader;
        shader.cluster_shift = 4;  // 16x16 tiles
        shader.cluster_grid = camera.GetClusterGrid();

        cmd.Dispatch(
            (camera.width / 16 + 15) / 16,
            (camera.height / 16 + 15) / 16,
            1
        );
        // Result: cluster_buffer[num_tiles] with light indices
    }
};

class Renderer {
    void RenderFrame(Scene& scene, Camera& camera) {
        // 1. Shadows (same as before)
        RenderShadows();

        // 2. Build clusters (GPU, ~0.3ms)
        clusterer.Build(lights, lights.size());

        // 3. Single forward pass with clustering
        forward_pass.SetClusterBuffer(clusterer.output_buffer);
        render_pass_manager->Render(forward_pass, geometry);

        // 4. Post-process (same as before)
        PostProcess();
    }
};
```

**Benefit**: One forward pass, no CPU light culling, better GPU utilization.

---

## 2. Light Data Layout

### Godot: Union of Light Types

```cpp
// File: /e/godot/servers/rendering/renderer_rd/storage_rd/light_storage.h, line 138
struct LightData {
    float position[3];
    float inv_radius;  // Only used by omni/spot

    float direction[3];  // omni: dual-paraboloid offset, spot: direction
    float size;

    float color[3];
    float attenuation;

    float inv_spot_attenuation;  // Only used by spot
    float cos_spot_angle;        // Only used by spot
    float specular_amount;
    float shadow_opacity;

    float atlas_rect[4];
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

// Separate struct for directional
struct DirectionalLightData {
    float direction[3];
    float energy;
    float color[3];
    float size;
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
    float shadow_matrices[4][16]; // 4 cascades
    float uv_scale1[2];           // cascade UV scale
    float uv_scale2[2];
    float uv_scale3[2];
    float uv_scale4[2];
};

// In shader: index into structured buffer
layout(binding=0) buffer LightBuffer {
    LightData omni_lights[];
    LightData spot_lights[];
    DirectionalLightData dir_light;
};

void light_compute(...) {
    if (light_type == OMNI) {
        LightData L = omni_lights[light_idx];
        float dist = length(L.position - fragment_pos);
        float attenuation = pow(max(0, 1 - pow(dist * L.inv_radius, 2)), 2);
        // Apply light...
    } else if (light_type == SPOT) {
        LightData L = spot_lights[light_idx];
        float spot_factor = smoothstep(L.cos_spot_angle, L.cos_spot_angle + 0.1, ...);
        // Apply light...
    } else {
        DirectionalLightData L = dir_light;
        // No attenuation, just direction
        // Apply light...
    }
}
```

### UzEngine (Current): Per-Light-Type Loops

```cpp
// File: e:/UzEngine/src/engine/renderer/rhi/rhi.h
struct DirectionalLightCBuffer {
    alignas(16) Vec3 direction;
    alignas(16) Vec3 color;
    alignas(4) float energy;
    alignas(16) Mat4 shadow_matrix[4];  // CSM
};

struct PointLightCBuffer {
    alignas(16) Vec3 position;
    alignas(4) float range;
    alignas(16) Vec3 color;
    alignas(4) float energy;
};

class LightingPass {
    void Render(const Light* light) {
        if (light->type == LightType::Directional) {
            cmd.SetConstantBuffer(0, (DirectionalLightCBuffer*)light->data);
        } else if (light->type == LightType::Point) {
            cmd.SetConstantBuffer(0, (PointLightCBuffer*)light->data);
        }
        // ... per-light draw call
    }
};
```

**Problem**: Multiple constant buffer slots, branching in shader, per-light draw calls.

### UzEngine (Recommended): Unified Light Buffer

```cpp
// File: src/engine/renderer/light_data.h
struct LightDataGPU {
    enum Type : uint32_t { DIRECTIONAL = 0, POINT = 1, SPOT = 2 };

    Type type;
    uint32_t flags;           // shadow_enabled, volumetric, etc.
    Vec3 position;            // point/spot only
    float range;              // point/spot only
    Vec3 direction;           // directional/spot only
    float size;               // physical light size
    Vec3 color;
    float energy;
    float specular;
    float shadow_bias;
    float soft_shadow_scale;
    uint32_t shadow_index;    // into shadow atlas or cascade array

    // For point lights (dual-paraboloid)
    Mat4 shadow_matrices[2];
    Rect4 shadow_atlas_rect;

    // For directional lights (CSM)
    Mat4 cascade_matrices[4];
    Vec4 cascade_splits;      // distance thresholds

    // For spot lights
    float spot_angle;
    float spot_attenuation;
    float cone_falloff;
    uint32_t pad;
};

// Unified buffer in shader
layout(binding=0) buffer LightBuffer {
    LightDataGPU lights[];
};

void ApplyLight(uint light_idx, Vec3 pos, Vec3 normal, inout Vec3 diffuse, inout Vec3 specular) {
    LightDataGPU L = lights[light_idx];

    if (L.type == DIRECTIONAL) {
        Vec3 to_light = normalize(-L.direction);
        // No attenuation
    } else if (L.type == POINT) {
        Vec3 to_light = normalize(L.position - pos);
        float dist = length(L.position - pos);
        float atten = pow(max(0, 1 - pow(dist / L.range, 2)), 2);
        // Apply light with atten
    } else if (L.type == SPOT) {
        // Spot logic
    }

    // All lights share same BRDF evaluation
    float NdotL = dot(normal, to_light);
    // ... standard PBR math
}
```

**Benefit**: One buffer, one structure, no per-light draw calls, no branching.

---

## 3. Environment Setup

### Godot: Single Environment Resource

```cpp
// File: /e/godot/servers/rendering/storage/environment_storage.h
struct Environment {
    // In one place:
    RSE::EnvironmentBG background;
    RID sky;
    Basis sky_orientation;

    // Ambient
    Color ambient_light;
    float ambient_light_energy;
    float ambient_sky_contribution;

    // Tonemap (in forward shader)
    RSE::EnvironmentToneMapper tone_mapper;
    float exposure;
    float white;

    // Fog (in forward shader)
    bool fog_enabled;
    Color fog_light_color;
    float fog_density;
    float fog_height_density;

    // Bloom (in post-process)
    bool glow_enabled;
    Vector<float> glow_levels;
    float glow_intensity;

    // SSAO / SSR (in post-process)
    bool ssao_enabled;
    float ssao_radius;

    bool ssr_enabled;
    float ssr_fade_out;
};

// Passed as UBO to all shaders
layout(binding=1) uniform EnvironmentData {
    uint32_t tonemap_mode;
    float exposure;
    float white_point;

    bool fog_enabled;
    vec3 fog_color;
    float fog_density;

    uint32_t glow_enabled;
    vec3 glow_color;
} env;
```

### UzEngine (Current): Scattered Systems

```cpp
// File: e:/UzEngine/src/engine/renderer/core/sky_light.h
class SkyLight {
    Vec3 ambient_color;
    float ambient_intensity;
    RID irradiance_cubemap;
};

// File: e:/UzEngine/src/engine/renderer/core/fog.h
class Fog {
    Vec3 color;
    float density;
    float height_scale;
};

// File: e:/UzEngine/src/engine/renderer/post_process.h
class PostProcessStack {
    Tonemapper tonemap;
    BloomEffect bloom;
    SSAOPass ssao;
};

// Used separately in different systems
```

### UzEngine (Recommended): Unified Environment

```cpp
// File: src/engine/renderer/environment.h
struct RendererEnvironment {
    // Ambient
    struct Ambient {
        Vec3 color;
        float intensity;
        float sky_contribution;  // blend between ground and sky
    } ambient;

    // Sky
    struct Sky {
        RID cubemap;
        RID irradiance;  // SH or LDR
        Basis orientation;
    } sky;

    // Fog
    struct Fog {
        bool enabled;
        Vec3 color;
        float density;
        float height;
        float height_density;
    } fog;

    // Tonemap
    struct Tonemap {
        enum Mode { LINEAR, REINHARDT, ACES, AGX } mode;
        float exposure;
        float white_point;
        float agx_contrast;
    } tonemap;

    // Bloom
    struct Bloom {
        bool enabled;
        float threshold;
        float intensity;
        std::vector<float> pyramid_levels;  // 7 mips
    } bloom;

    // SSAO
    struct SSAO {
        bool enabled;
        float radius;
        float intensity;
        float power;
    } ssao;

    // SSR
    struct SSR {
        bool enabled;
        int max_steps;
        float fade_in, fade_out;
    } ssr;

    uint64_t version;  // for versioning
};

// Single UBO passed to all shaders
layout(binding=7) uniform EnvironmentUBO {
    uint tonemap_mode;
    float exposure;
    float white_point;

    bool fog_enabled;
    vec3 fog_color;
    float fog_density;
    float fog_height_scale;

    vec3 ambient_color;
    float ambient_intensity;

    // ... etc
} environment;
```

**Benefit**: Single ownership, versioning, easier to update all at once.

---

## 4. Shadow Implementation

### Godot: Atlas for Omni/Spot

```cpp
// File: /e/godot/servers/rendering/renderer_rd/storage_rd/light_storage.h, line 391
struct ShadowAtlas {
    struct Quadrant {
        uint32_t subdivision = 0;  // 1, 4, 16, 64 shadows per quadrant
        Vector<Shadow> shadows;
    } quadrants[4];

    int size = 2048;  // single 2K texture
    RID depth;        // 2D texture array
};

// Allocation logic:
void allocate_shadow_for_light(Light* light, ShadowAtlas* atlas) {
    int best_quadrant = -1;
    int best_shadow_idx = -1;

    // Try to find space
    for (int q = 0; q < 4; q++) {
        if (atlas->quadrants[q].shadows.size() < max_shadows_per_quadrant) {
            best_quadrant = q;
            break;
        }
    }

    if (best_quadrant == -1) {
        // No space: shrink smallest quadrant, re-subdivide
        int smallest = find_smallest_quadrant(atlas);
        shrink_quadrant(atlas, smallest);
        // Try again
    }

    // Place shadow in quadrant
    Shadow& shadow = atlas->quadrants[best_quadrant].shadows.emplace_back();
    shadow.owner = light->rid();
}

// Rendering:
void render_shadows_to_atlas(ShadowAtlas* atlas, const std::vector<Light*>& lights) {
    for (int q = 0; q < 4; q++) {
        for (auto& shadow : atlas->quadrants[q].shadows) {
            Light* L = shadow.owner;
            if (L->is_visible()) {
                render_to_quadrant(atlas, q, L);
            }
        }
    }
}
```

### UzEngine (Current): Per-Light Shadow Map

```cpp
// File: e:/UzEngine/src/engine/renderer/rhi/shadow_manager.h
class ShadowManager {
    std::unordered_map<uint32_t, RID> point_light_shadows;  // light_id -> depth texture
    RID directional_shadow;

    RID AllocatePointLightShadow(uint32_t light_id) {
        if (point_light_shadows.size() >= 8) {
            // Out of space!
            return RID_INVALID;
        }
        RID depth = rhi->CreateTexture({1024, 1024, RhiFormat::D32});
        point_light_shadows[light_id] = depth;
        return depth;
    }
};

// Problem: fixed 8-light limit, no dynamic reallocation
```

### UzEngine (Recommended): Adopt Godot's Atlas

```cpp
// File: src/engine/renderer/shadow_atlas.h
class ShadowAtlas {
    struct Quadrant {
        uint32_t subdivision;  // 1, 4, 16, 64
        std::vector<ShadowEntry> shadows;
    } quadrants[4];

    uint32_t size;  // 2048
    RID depth;      // single texture array
    RID fb;

    // Allocation
    ShadowEntry* AllocateShadow(uint32_t light_id) {
        // Find best quadrant
        for (int q = 0; q < 4; q++) {
            if (quadrants[q].shadows.size() < max_per_quadrant(quadrants[q].subdivision)) {
                quadrants[q].shadows.push_back({light_id});
                return &quadrants[q].shadows.back();
            }
        }

        // Realloc: shrink other quadrants
        ReallocateQuadrants();
        return AllocateShadow(light_id);  // Try again
    }

    void ReallocateQuadrants() {
        int smallest_q = find_smallest_populated();
        if (smallest_q >= 0) {
            // Q0 1x1 -> shrink
            // Q1 2x2 -> shrink
            // Q2 4x4 -> keep
            // Q3 8x8 -> keep
            // Cascade down...
            for (int q = 0; q < smallest_q; q++) {
                quadrants[q].subdivision >>= 1;
            }
        }
    }
};
```

**Benefit**: Handles 32+ lights with shadows, automatic reallocation.

---

## 5. Material System

### Godot: Procedural Shader Generation

```cpp
// File: /e/godot/scene/resources/material.h, line 140
class StandardMaterial3D {
public:
    enum TextureParam {
        TEXTURE_ALBEDO,
        TEXTURE_NORMAL,
        TEXTURE_METALLIC,
        TEXTURE_ROUGHNESS,
        TEXTURE_EMISSION,
        TEXTURE_AO,
        TEXTURE_HEIGHTMAP,
        TEXTURE_MAX = 19
    };

    Color albedo_color;
    float metallic = 0.0;
    float roughness = 0.5;
    float specular = 0.5;
    float emission_energy = 1.0;
    float normal_scale = 1.0;
    // ... 20+ properties

    // When properties change, shader is regenerated
    void _on_property_changed() {
        String shader_code = GenerateShaderCode();
        material_storage->shader_set_code(shader_rid, shader_code);
    }
};

// Generated shader looks like:
/*
#ifdef ALBEDO_TEXTURE_USED
uniform sampler2D texture_albedo;
#endif

void fragment() {
    vec3 albedo = #ifdef ALBEDO_TEXTURE_USED ? texture(texture_albedo, UV).rgb : albedo_color;
    float metallic = #ifdef METALLIC_TEXTURE_USED ? texture(texture_metallic, UV).r : metallic_param;
    float roughness = #ifdef ROUGHNESS_TEXTURE_USED ? texture(texture_roughness, UV).r : roughness_param;

    ALBEDO = albedo;
    METALLIC = metallic;
    ROUGHNESS = roughness;

    // BRDF evaluated in light_compute() function
}
*/
```

### UzEngine (Current): Manual Shader Variants

```cpp
// File: e:/UzEngine/src/engine/renderer/materials/material.h
class Material {
    RID shader;  // single hardcoded shader
    std::unordered_map<StringId, Variant> params;

    struct StandardMaterial {
        RID albedo_texture;
        RID normal_texture;
        RID metallic_texture;
        RID roughness_texture;
        RID ao_texture;
        RID displacement_texture;

        Vec3 albedo_color;
        float metallic;
        float roughness;
    };
};

// Shader (hardcoded):
/*
layout(set=3, binding=0) uniform MaterialCB {
    vec4 albedo_color;
    float metallic;
    float roughness;
} material;

layout(set=3, binding=1) uniform sampler2D textures[8];

void main() {
    vec3 albedo = material.albedo_color.rgb;
    if (has_texture_flag(TEXTURE_ALBEDO)) {
        albedo *= texture(textures[0], uv).rgb;
    }

    float metallic = material.metallic;
    if (has_texture_flag(TEXTURE_METALLIC)) {
        metallic = texture(textures[1], uv).r;
    }

    // ...
}
*/
```

**Problem**: All texture sampling code even if not used, wastes instruction budget.

### UzEngine (Recommended): Shader Variant Cache

```cpp
// File: src/engine/renderer/materials/standard_material.h
class StandardMaterial3D {
    enum TextureUsage {
        USE_ALBEDO = 1 << 0,
        USE_NORMAL = 1 << 1,
        USE_METALLIC = 1 << 2,
        USE_ROUGHNESS = 1 << 3,
        USE_AO = 1 << 4,
        USE_DISPLACEMENT = 1 << 5,
    };

    struct Variant {
        uint32_t usage_flags;
        RID shader;
    };

    static std::unordered_map<uint32_t, Variant> variant_cache;

    RID GetOrCreateShader() {
        uint32_t key = CalculateVariantKey();
        if (variant_cache.find(key) != variant_cache.end()) {
            return variant_cache[key].shader;
        }

        // Generate shader for this variant only
        String code = GenerateShaderCode(key);
        RID new_shader = material_storage->CreateShader(code);
        variant_cache[key] = {key, new_shader};
        return new_shader;
    }

private:
    uint32_t CalculateVariantKey() {
        uint32_t key = 0;
        if (albedo_texture.is_valid()) key |= USE_ALBEDO;
        if (normal_texture.is_valid()) key |= USE_NORMAL;
        // ... etc
        return key;
    }
};
```

**Benefit**: Only compile shader paths actually used.

---

## 6. Cluster Rendering

### Godot: GPU-Driven Clustering

```glsl
// File: /e/godot/servers/rendering/renderer_rd/shaders/cluster_store.glsl
#version 450

layout(binding = 0, std430) buffer ClusterData {
    uvec4 cluster_data[];
};

layout(binding = 1, std430) buffer LightIndices {
    uint light_indices[];
};

layout(binding = 2) uniform sampler2D depth_tex;

layout(push_constant, std430) uniform PushConstant {
    uint cluster_render_data_size;
    uint max_render_element_count_div_32;
    uint cluster_screen_size[2];
    uint render_element_count_div_32;
    uint max_cluster_element_count_div_32;
} pc;

shared uint cluster_bits[32];  // Per-cluster light mask

void main() {
    ivec3 cluster_pos = ivec3(gl_GlobalInvocationID);
    uint cluster_idx = cluster_pos.z * pc.cluster_screen_size[0] * pc.cluster_screen_size[1] +
                       cluster_pos.y * pc.cluster_screen_size[0] + cluster_pos.x;

    // Read light mask for this cluster
    uint mask_offset = cluster_idx * pc.cluster_render_data_size;
    for (uint i = 0; i < pc.max_cluster_element_count_div_32; i++) {
        // Read from GPU-computed light mask
        uint mask = cluster_data[mask_offset + i].x;

        // Decompose mask into light indices
        while (mask != 0u) {
            uint bit = findLSB(mask);
            uint light_idx = i * 32 + bit;

            // Write to output
            uint output_offset = (cluster_idx * pc.max_cluster_element_count_div_32) + i;
            light_indices[output_offset] = light_idx;

            mask &= ~(1u << bit);
        }
    }
}

// In fragment shader:
#version 450

layout(binding = 0, std430) buffer LightIndices {
    uint light_indices[];
};

layout(binding = 1, std430) buffer LightData {
    LightDataGPU lights[];
};

ivec2 cluster_pos = ivec2(gl_FragCoord.xy) >> CLUSTER_SHIFT;
uint cluster_idx = cluster_pos.y * CLUSTER_WIDTH + cluster_pos.x;
uint cluster_offset = cluster_idx * MAX_CLUSTER_ELEMENTS_DIV_32;

for (uint i = 0; i < MAX_CLUSTER_ELEMENTS_DIV_32; i++) {
    uint mask = light_indices[cluster_offset + i];
    while (mask != 0u) {
        uint light_idx = findLSB(mask);
        LightDataGPU L = lights[light_idx];
        ApplyLight(L, ...);
        mask &= ~(1u << light_idx);
    }
}
```

### UzEngine (Recommended): Simpler Clustering

```glsl
// File: src/engine/renderer/shaders/cluster.glsl
#version 450

layout(set=0, binding=0, std430) buffer ClusterBuffer {
    uint light_count;
    uint light_indices[];  // flat: [tile0_light0, tile0_light1, ..., tile1_light0, ...]
};

layout(push_constant) uniform PushConstant {
    uint cluster_shift;    // log2(16) = 4
    uint cluster_width;
    uint max_lights_per_cluster;
} pc;

// In fragment
void ApplyLighting() {
    ivec2 cluster = ivec2(gl_FragCoord.xy) >> pc.cluster_shift;
    uint cluster_idx = cluster.y * pc.cluster_width + cluster.x;
    uint cluster_offset = cluster_idx * pc.max_lights_per_cluster;

    // Linear scan (not bitfield decode)
    for (uint i = 0; i < pc.max_lights_per_cluster; i++) {
        uint light_idx = light_indices[cluster_offset + i];
        if (light_idx == 0xFFFFFFFF) break;  // sentinel

        LightDataGPU L = lights[light_idx];
        ApplyLight(L, ...);
    }
}
```

**Godot's approach**: Bitfield encoding (compact, ~1KB per cluster)
**UzEngine approach**: Linear array (simpler, ~4KB per cluster, still small)

---

## 7. Summary: Lessons for UzEngine

| Aspect | Godot Approach | UzEngine Benefit |
|--------|---|---|
| **Pipeline** | Forward clustering | Lower memory, easier transparency |
| **Light data** | Unified struct union | One shader path, no branching |
| **Environment** | Single blob resource | Easier versioning, cache coherent |
| **Shadows (omni/spot)** | Dynamic atlas | Handles 32+ lights, no failures |
| **Material system** | Shader variant cache | Compile only used paths |
| **Clustering** | GPU-driven compute | CPU-free, scales linearly |
| **Tonemap** | In post-process or forward | Consistent with material evaluation |
| **Default scene** | 1 dir light + procedural sky | Minimal startup, works anywhere |

---

**End of Comparison**
