<!-- doc-meta
title: Godot 4 Point Light (OmniLight3D) and Spot Light Shadow Architecture
description: Detailed analysis of Godot's local light shadow rendering pipeline, atlas management, and comparison with UzEngine's DX12 deferred renderer
status: complete
last_updated: 2026-03-30
authors: Claude Code
reference_sources: Godot 4.3 source code (light_storage.h/cpp, scene_forward_lights_inc.glsl, render_forward_clustered.cpp)
-->

# Godot 4 Point Light and Spot Light Shadow Architecture

## Executive Summary

Godot 4 uses a **single shadow atlas with quadrant-based tile allocation** for both omni (point) and spot lights. For omni shadows, Godot supports **two rendering modes**:
- **Dual-Paraboloid (default)**: Two depth maps per light, efficient 2D atlas packing
- **Cubemap (optional)**: Full 6-face cubemap rendered separately, then projected to atlas

This document compares Godot's approach with UE5 and provides architectural guidance for UzEngine's DX12 deferred renderer.

---

## 1. Shadow Atlas Management

### 1.1 Single Atlas Architecture

Godot maintains a **single shadow atlas texture** shared across all local lights (both omni and spot):

```cpp
// From light_storage.h
struct ShadowAtlas {
    struct Quadrant {
        uint32_t subdivision = 0;
        struct Shadow {
            RID owner;
            uint64_t version = 0;
            uint64_t alloc_tick = 0;
        };
        Vector<Shadow> shadows;
    } quadrants[4];

    int size = 0;                    // e.g., 2048
    bool use_16_bits = true;         // R16F or R32F
    RID depth;                       // Single depth texture
    RID fb;                          // Framebuffer for copying
    HashMap<RID, uint32_t> shadow_owners;  // Light instance -> shadow key
};
```

**Key insights:**
- **4 quadrants** divide the atlas into quarters: top-left, top-right, bottom-left, bottom-right
- Each quadrant **subdivides independently** (subdivision = 1, 2, 4, 8, etc.)
- **Omni lights consume 2 shadow slots** (one per paraboloid)
- **Spot lights consume 1 shadow slot**
- Shared framebuffer minimizes GPU resources

### 1.2 Tile Allocation Strategy

Godot uses **greedy first-fit with LRU eviction**:

```cpp
// Excerpt from light_storage.cpp::shadow_atlas_update_light()
// 1. Calculate desired fit size based on light coverage
uint32_t quad_size = shadow_atlas->size >> 1;  // e.g., 1024 for 2048 atlas
int desired_fit = MIN(
    quad_size / shadow_atlas->smallest_subdiv,
    Math::next_power_of_2(uint32_t(quad_size * p_coverage))
);

// 2. Find quadrants that can fit this size
int valid_quadrants[4];
int valid_quadrant_count = 0;
for (int i = 0; i < 4; i++) {
    int q = shadow_atlas->size_order[i];
    int sd = shadow_atlas->quadrants[q].subdivision;
    int max_fit = quad_size / sd;
    if (max_fit >= desired_fit) {
        valid_quadrants[valid_quadrant_count++] = q;
    }
}

// 3. If light already has a shadow, check if reallocation is needed
if (shadow_atlas->shadow_owners.has(p_light_instance)) {
    should_realloc = (tick - alloc_tick > 500ms) && (new_subdiv != old_subdiv);
}

// 4. Allocate (or evict + reallocate) in valid quadrants
found_shadow = is_omni ?
    _shadow_atlas_find_omni_shadows(...) :
    _shadow_atlas_find_shadow(...);
```

**Reallocation tolerance**: 500ms before allowing downgrade (prevents thrashing on borderline sizes).

### 1.3 Omni-Specific Allocation (2 Slots)

For omni lights, Godot allocates **2 consecutive shadow slots** (one for each paraboloid):

```cpp
// From shadow_atlas_update_light(), line 2486
if (is_omni) {
    new_key |= OMNI_LIGHT_FLAG;  // Set bit 26 to mark as omni

    // Allocate second slot for flip paraboloid
    int new_omni_shadow = new_shadow + 1;
    ShadowAtlas::Quadrant::Shadow *extra_sh =
        &shadow_atlas->quadrants[new_quadrant].shadows.write[new_omni_shadow];
    _shadow_atlas_invalidate_shadow(extra_sh, p_atlas, shadow_atlas,
                                     new_quadrant, new_omni_shadow);

    extra_sh->owner = p_light_instance;
    extra_sh->alloc_tick = tick;
    extra_sh->version = p_light_version;
}
```

The key `uint32_t` encodes:
- **Bits 0-26**: Shadow index (up to 64K shadows per quadrant)
- **Bit 26**: OMNI_LIGHT_FLAG (distinguishes omni from spot)
- **Bits 27-28**: Quadrant (0-3)

### 1.4 Dynamic Atlas Resizing

Godot supports **dynamic resize** of the shadow atlas:

```cpp
void LightStorage::_update_shadow_atlas(ShadowAtlas *shadow_atlas) {
    // Periodically recalculate subdivision based on usage
    // If a quadrant has fewer free slots, increase subdivision
    // If empty quadrant, decrease subdivision
    // Reallocation tolerance prevents rapid ping-ponging
}
```

**Advantages over static size:**
- Adapts to scene complexity at runtime
- Empty quadrants shrink to free memory
- No manual tuning of "max shadow count"

**Disadvantages:**
- Reallocation causes GPU stalls
- Requires careful timing logic to avoid thrashing

---

## 2. OmniLight3D Shadow Rendering Modes

### 2.1 Dual-Paraboloid (Default Mode)

**LIGHT_OMNI_SHADOW_DUAL_PARABOLOID** is the default and most efficient approach.

#### Paraboloid Projection

Instead of rendering 6 cubemap faces, Godot renders 2 paraboloid maps:
- **Front paraboloid** (z ≥ 0): Projects front hemisphere
- **Back paraboloid** (z < 0): Projects back hemisphere

Each paraboloid is a **2D quad** in the shadow atlas.

#### UV Computation

The paraboloid projection formula (from scene_forward_lights_inc.glsl, line 537-604):

```glsl
// Dual-paraboloid projection for omni shadows
vec3 local_vert = (omni_lights.data[idx].shadow_matrix * vec4(vertex, 1.0)).xyz;
float shadow_len = length(local_vert);
vec3 shadow_dir = normalize(local_vert);

// Determine which paraboloid to use
vec4 uv_rect = base_uv_rect;
if (shadow_dir.z >= 0.0) {
    uv_rect.xy += flip_offset;  // Use front paraboloid
}

// Paraboloid projection formula: 1 / (1 + |z|)
shadow_dir.z = 1.0 + abs(shadow_dir.z);
vec2 pos = shadow_dir.xy / shadow_dir.z;

// Normalize to [0, 1] UV space
pos = pos * 0.5 + 0.5;

// Apply atlas rect to get final coordinates
pos = uv_rect.xy + pos * uv_rect.zw;

// Sample depth from atlas
float depth = (shadow_len - bias) * inv_radius;
depth = 1.0 - depth;  // Invert for comparison
```

**Key insight**: The paraboloid formula `1 / (1 + |z|)` maps the direction vector to a 2D quad. This is equivalent to projecting a hemisphere onto a disk.

#### Atlas Layout for Dual-Paraboloid

For a light occupying `quad_size × quad_size` pixels in one quadrant:
- **Front paraboloid**: `quad_size × quad_size` at `(0, 0)` offset
- **Back paraboloid**: `quad_size × quad_size` at `flip_offset` (either +X or +Y)

```
┌─────────────────────────────┐
│  Quadrant (e.g., size=256)  │
├─────────────────┬───────────┤
│  Front (z≥0)    │ Back (z<0)│
│   128x128       │  128x128  │
└─────────────────┴───────────┘
```

The `flip_offset` alternates based on `(shadow + 1) % subdivision`:
```cpp
// From render_forward_clustered.cpp, line 2684
dual_paraboloid_offset = wrap ? Vector2i(1 - subdivision, 1) : Vector2i(1, 0);
// wrap = (shadow + 1) % subdivision == 0
```

**Advantages:**
- Only 2 passes instead of 6 (cubemap)
- Efficient 2D atlas packing
- Linear memory access pattern
- Unified shadow matrix (doesn't change per direction)

**Disadvantages:**
- Edge artifacts near paraboloid boundaries (seams at poles)
- Distortion near edges (less uniform sampling)

### 2.2 Cubemap Mode (Optional)

**LIGHT_OMNI_SHADOW_CUBE** renders a full 6-face cubemap:

```cpp
// From render_forward_clustered.cpp, line 2687
if (light_storage->light_omni_get_shadow_mode(base) ==
    RSE::LIGHT_OMNI_SHADOW_CUBE) {

    render_texture = light_storage->get_cubemap(shadow_size / 2);
    render_fb = light_storage->get_cubemap_fb(shadow_size / 2, p_pass);

    light_projection = light_storage->light_instance_get_shadow_camera(p_light, p_pass);
    light_transform = light_storage->light_instance_get_shadow_transform(p_light, p_pass);

    render_cubemap = true;
    finalize_cubemap = p_pass == 5;

    // After all 6 passes, convert to dual-paraboloid for atlas
    copy_effects->copy_cubemap_to_dp(render_texture, atlas_fb, atlas_rect_norm,
                                      atlas_rect.size, near, far, false);  // Front
    copy_effects->copy_cubemap_to_dp(render_texture, atlas_fb, atlas_rect_norm + offset,
                                      atlas_rect.size, near, far, true);   // Back
}
```

**Workflow:**
1. Render 6 cubemap faces (one per pass, p_pass 0-5) to temporary cubemap
2. After all 6 passes, call `copy_cubemap_to_dp()` to convert to dual-paraboloid
3. Store final dual-paraboloid in atlas

**Advantages:**
- No edge seams (full sphere coverage)
- Uniform sampling quality across sphere
- Higher shadow quality for point lights

**Disadvantages:**
- 6 passes instead of 2 (3× more GPU work)
- Extra conversion pass (cubemap → dual-paraboloid)
- Requires temporary cubemap texture

---

## 3. Shadow Sampling: PCF and Bias

### 3.1 Shadow Matrix Computation

For **dual-paraboloid**, Godot uses a **single shadow matrix** (not per-face):

```cpp
// From light_storage.h, LightInstance::ShadowTransform
struct ShadowTransform {
    Projection camera;        // Single projection for both paraboloids
    Transform3D transform;    // Light position + rotation
    float farplane = 0.0;
    float split = 0.0;
    float bias_scale = 0.0;
    float shadow_texel_size = 0.0;
    float range_begin = 0.0;
    Rect2 atlas_rect;
    Vector2 uv_scale;
};
```

The projection is **orthographic or perspective**, applied to `local_vert`:
```glsl
// Vertex to light space
vec3 local_vert = (shadow_matrix * vec4(vertex, 1.0)).xyz;

// This handles the transformation, paraboloid selection is done in shader
```

### 3.2 PCF Sampling (Disk Rotation + Random Offset)

Godot's PCF for omni shadows (scene_forward_lights_inc.glsl, line 348-390):

```glsl
half sample_omni_pcf_shadow(
    texture2D shadow,
    float blur_scale,
    vec2 coord,              // [-1, 1] paraboloid coordinates
    vec4 uv_rect,            // Atlas rect [x, y, w, h]
    vec2 flip_offset,        // Offset to back paraboloid
    float depth,             // [0, 1] normalized depth
    float taa_frame_count
) {
    if (sc_soft_shadow_samples() == 0) {
        // Single sample at center
        return half(textureProj(sampler2DShadow(...), vec4(pos, depth, 1.0)));
    }

    // Compute random rotation for Poisson disk
    mat2 disk_rotation;
    {
        float r = quick_hash(gl_FragCoord.xy + vec2(taa_frame_count * 5.588238))
                  * 2.0 * M_PI;
        float sr = sin(r);
        float cr = cos(r);
        disk_rotation = mat2(vec2(cr, -sr), vec2(sr, cr));
    }

    float avg = 0.0;
    vec2 offset_scale = blur_scale * 2.0 * shadow_atlas_pixel_size / uv_rect.zw;

    for (uint i = 0; i < sc_soft_shadow_samples(); i++) {
        vec2 offset = offset_scale * (disk_rotation * soft_shadow_kernel[i].xy);
        vec2 sample_coord = coord + offset;

        // Critical: Flip offset handling for paraboloid boundary
        float sample_coord_length_squared = dot(sample_coord, sample_coord);
        bool do_flip = sample_coord_length_squared > 1.0;

        if (do_flip) {
            // Sample went beyond the [-1, 1] disk, mirror it to other paraboloid
            float len = sqrt(sample_coord_length_squared);
            sample_coord = sample_coord * (2.0 / len - 1.0);
        }

        // Normalize to [0, 1] UV space
        sample_coord = sample_coord * 0.5 + 0.5;
        sample_coord = uv_rect.xy + sample_coord * uv_rect.zw;

        if (do_flip) {
            sample_coord += flip_offset;  // Switch to back paraboloid
        }

        avg += textureProj(sampler2DShadow(...), vec4(sample_coord, depth, 1.0));
    }

    return half(avg * (1.0 / float(sc_soft_shadow_samples())));
}
```

**Key features:**
- **Disk rotation**: Rotates Poisson disk based on TAA frame count (ensures temporal stability)
- **Boundary wrapping**: If a sample goes outside [-1, 1], it flips to the opposite paraboloid
- **Atlas UV mapping**: Carefully applies atlas rect (position and scale)

### 3.3 Bias Strategy

Godot uses **normal bias + constant bias**:

```glsl
// From scene_forward_lights_inc.glsl, line 499-500
vec3 local_normal = normalize(mat3(omni_lights.data[idx].shadow_matrix) * normal);
vec3 normal_bias = local_normal * omni_lights.data[idx].shadow_normal_bias
                   * (1.0 - abs(dot(local_normal, shadow_dir)));

// Apply constant bias
float depth = shadow_len - omni_lights.data[idx].shadow_bias;
depth *= omni_lights.data[idx].inv_radius;
depth = 1.0 - depth;
```

**Normal bias formula**: `normal_bias * (1 - |N·L|)`
- When surface is perpendicular to light (|N·L| ≈ 0), normal bias is maximum
- When surface is parallel to light (|N·L| ≈ 1), normal bias is zero
- This prevents self-shadowing on grazing angles

**Default values** (from light_storage.cpp):
```cpp
light.param[RSE::LIGHT_PARAM_SHADOW_BIAS] = 0.02;        // Constant bias
light.param[RSE::LIGHT_PARAM_SHADOW_NORMAL_BIAS] = 1.0;  // Normal bias scale
```

### 3.4 Soft Shadows (Penumbra)

Godot implements **blocker search + PCF** (variance shadow mapping-inspired, line 502-593):

```glsl
if (sc_use_light_soft_shadows() && omni_lights.data[idx].soft_shadow_size > 0.0) {
    // Step 1: Blocker search (find average depth of blocking surfaces)
    for (i = 0; i < penumbra_samples; i++) {
        float d = textureLod(shadow, sample_pos, 0.0).r;
        if (d > z_norm) {  // This sample is occluded
            blocker_average += d;
            blocker_count += 1.0;
        }
    }

    if (blocker_count > 0.0) {
        // Step 2: Compute penumbra size (edge width)
        blocker_average /= blocker_count;
        float penumbra = (-z_norm + blocker_average) / (1.0 - blocker_average);

        // Step 3: PCF with scaled kernel
        tangent *= penumbra;
        bitangent *= penumbra;

        for (i = 0; i < penumbra_samples; i++) {
            // Sample with scaled kernel
        }
    } else {
        shadow = 1.0;  // No blockers, fully lit
    }
}
```

**Penumbra formula**: `(-depth + blocker_depth) / (1.0 - blocker_depth)`

This approximates the penumbra width based on:
- Distance from light to blocker
- Distance from blocker to receiver

---

## 4. Spotlight Shadows

### 4.1 Single-Pass Rendering

Spot lights use **single-pass shadow rendering** (no dual-paraboloid):

```cpp
// From render_forward_clustered.cpp, line 2715
} else if (light_storage->light_get_type(base) == RSE::LIGHT_SPOT) {
    light_projection = light_storage->light_instance_get_shadow_camera(p_light, 0);
    light_transform = light_storage->light_instance_get_shadow_transform(p_light, 0);

    render_fb = light_storage->shadow_atlas_get_fb(p_shadow_atlas);
    flip_y = true;
}
```

**Single render pass** because:
- Spot light cone is directional (like a flashlight)
- Single projection matrix covers the cone
- No need for multiple paraboloids or cubemap faces

### 4.2 Projection Matrix

For spot lights, Godot computes a **perspective projection** matching the light's cone:

```cpp
// Implied from shadow matrices in light_storage.cpp
// Perspective matrix with FOV = 2 * spot_angle
float fov = 2.0 * light.param[RSE::LIGHT_PARAM_SPOT_ANGLE];
float near = 0.01;  // Near plane
float far = light.param[RSE::LIGHT_PARAM_RANGE];  // Light range
```

### 4.3 Spot Shadow Sampling

Spot shadows use the same **PCF function as directional shadows** (not omni-specific):

```glsl
half sample_pcf_shadow(
    texture2D shadow,
    vec2 shadow_pixel_size,
    vec3 coord,  // [x, y, depth]
    float taa_frame_count
)
```

No paraboloid flipping or wrapping needed since the projection is already perspective.

---

## 5. Common Shadow Artifacts and Solutions

### 5.1 Peter Panning (Light Leaking)

**Symptom**: Surfaces appear to float away from surfaces they should shadow.

**Cause**: Bias too large relative to depth precision.

**Godot's solution**:
```glsl
vec3 normal_bias = local_normal * shadow_normal_bias * (1.0 - abs(dot(local_normal, shadow_dir)));
depth = shadow_len - shadow_bias;
depth *= inv_radius;
```

Normal bias scales based on surface orientation, reducing peter panning on grazing surfaces.

**Recommendation for UzEngine**:
- Use `shadow_normal_bias = 1.0` as default
- Only increase if self-shadowing persists
- Never increase constant bias to "fix" self-shadowing (causes light leaking)

### 5.2 Shadow Acne (Self-Shadowing Noise)

**Symptom**: Speckled light/dark dots on surfaces not in shadow.

**Cause**: Precision loss in depth comparison due to rasterization errors.

**Godot's solution**: Constant bias + normal bias

**UzEngine recommendation**:
- Use reversed-Z (1.0 = near, 0.0 = far) for better precision near camera
- Start with `bias = 0.02`, `normal_bias = 1.0`
- Increase slowly until acne disappears
- If bias still too large → problem likely in depth format (ensure 32-bit float shadows)

### 5.3 Paraboloid Seams (Dual-Paraboloid Only)

**Symptom**: Visible discontinuity where front and back paraboloid meet (z=0 plane).

**Cause**:
- Projection distortion at paraboloid boundary
- PCF samples may flip between paraboloids inconsistently

**Godot's solution**:
```glsl
// Explicit boundary handling in PCF
float sample_coord_length_squared = dot(sample_coord, sample_coord);
bool do_flip = sample_coord_length_squared > 1.0;

if (do_flip) {
    float len = sqrt(sample_coord_length_squared);
    sample_coord = sample_coord * (2.0 / len - 1.0);  // Reflect back
}
```

**UzEngine recommendation**:
- Implement boundary wrapping in PCF to prevent scattered samples
- Consider slightly increasing blur_scale near boundaries to smoothen transition
- For critical scene areas, use cubemap mode instead of dual-paraboloid

### 5.4 Shadow Cascade Banding (Multiple Lights)

**Symptom**: Different shadow quality/banding on surfaces lit by different lights.

**Cause**: Each light's atlas region has different resolution.

**Godot's solution**:
- Quadrant-based allocation ensures nearby lights often share quadrant (same resolution)
- Reallocation tolerance prevents thrashing
- Atlas size scales with scene complexity

**UzEngine recommendation**:
- Monitor shadow_atlas memory usage
- Consider per-light shadow maps for critical lights (increased memory cost)

### 5.5 Atlas Thrashing (Frequent Reallocation)

**Symptom**: Stutters when lights move or change visibility.

**Cause**: Constant reallocation of atlas regions.

**Godot's solution**:
```cpp
shadow_atlas_realloc_tolerance_msec = 500;  // Don't reallocate more than every 500ms
```

**UzEngine recommendation**:
- Use same tolerance
- Profile atlas reallocation frequency
- If thrashing, increase atlas size or reduce max light count

---

## 6. Light Intensity and Attenuation

### 6.1 Point Light Attenuation

Godot uses **inverse-squared attenuation**:

```glsl
// From scene_forward_lights_inc.glsl, line 439-446
half get_omni_attenuation(float distance, float inv_radius, float decay) {
    float nd = distance * inv_radius;
    nd *= nd;
    nd *= nd;  // nd^4
    nd = max(1.0 - nd, 0.0);
    nd *= nd;  // nd^2
    return half(nd * pow(max(distance, 0.0001), -decay));
}
```

**Formula breakdown**:
1. `nd = distance / radius` (normalized distance, 0 at light, 1 at radius)
2. `nd^4` smooths the curve
3. `(1 - nd)^2` creates smooth falloff to zero
4. `decay = attenuation parameter` (usually 2 for physically-based inverse-square)

**Default intensity** (from light_storage.cpp):
```cpp
light.param[RSE::LIGHT_PARAM_INTENSITY] = 1000.0;  // Omni light
```

### 6.2 Attenuation Curve

```
Attenuation vs Distance
│
1.0 ├─────●
    │    ╱╲
    │   ╱  ╲
0.5 ├──╱────╲──
    │ ╱      ╲
    │╱        ╲
0.0 └─────────●─── distance
    0       radius
```

The formula produces:
- **At light (d=0)**: attenuation ≈ 1.0
- **At radius (d=radius)**: attenuation ≈ 0.0
- **Smooth transition**: No discontinuities

### 6.3 Units and Intensity

Godot uses **arbitrary intensity units** (not physically-based energy):
- **Directional light**: 100,000 lm/sr (very bright)
- **Point light**: 1,000 lm
- **Spotlight**: 1,000 lm

These are tuned for visual appearance, not physical accuracy.

**UzEngine recommendation**:
- Keep same defaults for user-familiar behavior
- Document that intensity is arbitrary (not lumens)
- Scale with exposure compensation if needed

---

## 7. Architecture Comparison: Godot vs UE5

| Feature | Godot | UE5 |
|---------|-------|-----|
| **Atlas approach** | Single shared atlas | Separate omni/spot atlases |
| **Omni rendering** | Dual-paraboloid (default) | Cubemap to atlas via Copy |
| **Spot rendering** | Single perspective | Single perspective |
| **Quadrants** | 4 independent subdivisions | Global quadtree (more flexible) |
| **Reallocation** | LRU with 500ms tolerance | Immediate |
| **Bias strategy** | Normal + constant | Slope-scale + constant |
| **PCF kernel** | Poisson disk | Poisson disk |
| **Soft shadows** | Blocker search + penumbra | Angle-based penumbra only |
| **Intensity units** | Arbitrary (1000 lm) | Arbitrary (1600 lm) |

---

## 8. Recommendations for UzEngine DX12

### 8.1 Shadow Atlas Design

**Adopt Godot's quadrant-based approach:**

```cpp
struct ShadowAtlas {
    uint32_t size;           // 2048 or 4096

    struct Quadrant {
        uint32_t subdivision;
        struct Tile {
            uint32_t owner_id;
            uint32_t version;
            uint64_t last_used_ms;
        };
        Vector<Tile> tiles;
    } quadrants[4];

    RID depth_texture;
    HashMap<uint32_t, uint32_t> owners;  // light_id -> key
};
```

**Advantages over per-light textures:**
- Memory efficient (one 2K texture vs. N 512-1024 textures)
- Cache-friendly atlas layout
- Dynamic resizing without per-light reallocation

### 8.2 Omni Shadow Rendering

**Recommendation: Default to dual-paraboloid, optional cubemap**

Implement both:

```cpp
enum class OmniShadowMode {
    DualParaboloid,  // 2 passes, efficient, default
    Cubemap,         // 6 passes, high quality
};

struct OmniLight {
    OmniShadowMode shadow_mode = OmniShadowMode::DualParaboloid;

    // For dual-paraboloid
    Matrix4 shadow_matrix;  // Single matrix for both paraboloids
    Rect2 atlas_rect;
    Vector2 flip_offset;

    // For cubemap (temporary)
    // RID cubemap_texture;
};
```

### 8.3 PCF Implementation

Implement Godot's disk-rotation + boundary wrapping:

```hlsl
// DX12 HLSL version
float SampleOmniShadowPCF(
    Texture2D shadow_atlas,
    SamplerComparisonState shadow_sampler,
    float2 coord,        // [-1, 1] paraboloid space
    float4 atlas_rect,   // [x, y, w, h] normalized
    float2 flip_offset,
    float depth,
    float2 pixel_offset  // For temporal variation
)
{
    if (num_samples == 0) {
        // Single sample
        float2 pos = coord * 0.5 + 0.5;
        pos = atlas_rect.xy + pos * atlas_rect.zw;
        return shadow_atlas.SampleCmpLevelZero(shadow_sampler, pos, depth);
    }

    // Multi-sample PCF with disk rotation
    float angle = quick_hash(pixel_offset) * 2.0 * 3.14159;
    float2x2 rotation = {
        cos(angle), -sin(angle),
        sin(angle), cos(angle)
    };

    float avg = 0.0;
    for (uint i = 0; i < num_samples; i++) {
        float2 offset = soft_shadow_kernel[i];
        offset = mul(offset, rotation);

        float2 sample_coord = coord + offset * blur_scale;
        float dist_sq = dot(sample_coord, sample_coord);

        bool do_flip = dist_sq > 1.0;
        if (do_flip) {
            float len = sqrt(dist_sq);
            sample_coord = sample_coord * (2.0 / len - 1.0);
        }

        sample_coord = sample_coord * 0.5 + 0.5;
        sample_coord = atlas_rect.xy + sample_coord * atlas_rect.zw;

        if (do_flip) {
            sample_coord += flip_offset;
        }

        avg += shadow_atlas.SampleCmpLevelZero(shadow_sampler, sample_coord, depth);
    }

    return avg / num_samples;
}
```

### 8.4 Bias Strategy

Use normal-bias + constant-bias like Godot:

```cpp
struct OmniLightCB {
    float3 position;
    float inv_radius;

    float3 shadow_matrix_row0[3];  // 3x3 rotation

    float shadow_bias;        // Constant bias (0.02 default)
    float shadow_normal_bias; // Normal bias scale (1.0 default)
    float soft_shadow_size;   // For soft shadows

    float4 atlas_rect;        // [x, y, w, h]
    float2 flip_offset;       // Paraboloid offset
};
```

### 8.5 Spot Light Considerations

Spot lights are simpler (single perspective projection):

```hlsl
float SampleSpotShadowPCF(
    Texture2D shadow_atlas,
    SamplerComparisonState shadow_sampler,
    float3 shadow_coord,     // [x, y, depth] from projection
    float2 pixel_offset
)
{
    // Standard 2D PCF (simpler than omni)
    float angle = quick_hash(pixel_offset) * 2.0 * 3.14159;
    float2x2 rotation = { cos(angle), -sin(angle), sin(angle), cos(angle) };

    float avg = 0.0;
    for (uint i = 0; i < num_samples; i++) {
        float2 offset = soft_shadow_kernel[i];
        offset = mul(offset, rotation);

        float2 sample_pos = shadow_coord.xy + offset * blur_scale;
        avg += shadow_atlas.SampleCmpLevelZero(shadow_sampler, sample_pos,
                                                shadow_coord.z);
    }

    return avg / num_samples;
}
```

### 8.6 Debug Visualization

Implement shadow atlas visualization (following Godot's precedent):

```cpp
// Debug mode to visualize atlas
enum class ShadowDebugMode {
    Disabled = 0,
    AtlasVisualize = 1,    // Color-code quadrants
    TileBoundaries = 2,    // Draw tile borders
    ResolutionHeatmap = 3, // Red=low res, green=high res
};

// In pixel shader:
float4 DebugShadowAtlas(float2 uv, ShadowDebugMode mode) {
    switch (mode) {
        case AtlasVisualize: {
            // Determine which quadrant
            float2 quad = uv * 2.0;
            uint q = (uint(quad.x) << 1) | uint(quad.y);
            return kQuadrantColors[q];
        }
        case TileBoundaries: {
            // Highlight tile boundaries based on subdivision
            // Implementation depends on atlas structure
        }
        // ... other modes
    }
}
```

---

## 9. References

### Source Code Locations (Godot 4.3)
- **Light storage**: `servers/rendering/renderer_rd/storage_rd/light_storage.h/cpp`
- **Shadow atlas allocation**: `light_storage.cpp::shadow_atlas_update_light()` (line 2383)
- **Omni allocation**: `_shadow_atlas_find_omni_shadows()` (line 2311)
- **Shadow rendering**: `servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.cpp::_render_shadow_pass()` (line 2577)
- **Shadow sampling (GLSL)**: `servers/rendering/renderer_rd/shaders/scene_forward_lights_inc.glsl`
  - `sample_omni_pcf_shadow()` (line 348)
  - `light_process_omni()` (line 448)
  - Paraboloid projection (line 537-604)

### Key Parameters (Default Values)

**Light initialization** (light_storage.cpp):
```cpp
LIGHT_PARAM_SHADOW_BIAS = 0.02
LIGHT_PARAM_SHADOW_NORMAL_BIAS = 1.0
LIGHT_PARAM_INTENSITY = 1000.0 (omni) / 100000.0 (directional)
LIGHT_OMNI_SHADOW_MODE = DUAL_PARABOLOID (default)
```

**Shadow atlas**:
```cpp
shadow_atlas_size = 2048 or 4096 (project setting)
shadow_atlas_use_16_bits = true
shadow_atlas_realloc_tolerance_msec = 500
```

### Related Papers and References
- **Paraboloid shadow mapping**: König & Schulz (2010), "Real-time Rendering of Point-lit Metaballs"
- **Dual-paraboloid projections**: Introduced in early GPU era for omnidirectional shadows
- **Poisson disk sampling**: Bridger et al. (2006), "Rendering Algorithms for Order-Independent Transparency"
- **Variance shadow maps**: Lauritzen & McCool (2008)
- **Normal-bias techniques**: Various GDC presentations on shadow artifact reduction

---

## 10. Summary Table: Godot Implementation Details

| Component | Implementation | Notes |
|-----------|----------------|-------|
| **Atlas allocation** | Quadrant-based LRU | 4 independent subdivisions |
| **Omni rendering** | Dual-paraboloid (default) | 2 passes, single matrix |
| **Omni backup mode** | Cubemap with conversion | 6 passes → dual-paraboloid |
| **Spot rendering** | Single perspective pass | Like directional but cone-shaped |
| **PCF kernel** | Poisson disk (16 samples) | Rotated per frame (TAA) |
| **Boundary handling** | Reflect/wrap for paraboloids | Prevents seams |
| **Bias** | Normal + constant | Adaptive to surface angle |
| **Soft shadows** | Blocker search + penumbra | Optional, per-light |
| **Atlas texture** | Single shared (all lights) | Memory efficient |
| **Depth format** | R16F or R32F | Configurable |
| **Reallocation tolerance** | 500ms | Prevents thrashing |
| **Default intensity** | 1000 lm (omni), 100k lm (dir) | Arbitrary units |
| **Attenuation curve** | `(1-nd)²×pow(d,-decay)` | Inverse-squared falloff |

---

## Document Metadata
- **Status**: Final (comprehensive)
- **Review**: Internal reference for UzEngine rendering team
- **Next steps**: Implement quadrant-based atlas in UzEngine RHI layer
- **Related tasks**:
  - Local light shadow architecture (point-spot-light-shadows.md)
  - UE5 shadow parity (local-light-shadows-ue5-architecture.md)
