# Point / Spot Light Shadows Spec

## Overview

UzEngine supports shadow-casting point lights and spot lights in addition to the directional light CSM system (see [renderer.md](renderer.md)). Point lights use **dual-paraboloid shadow maps** (2 faces per light, cheaper than cubemaps on DX12). Spot lights use a single **perspective projection** shadow map. Both types are packed into dedicated shadow atlases and sampled with PCF 2x2 filtering.

---

## ECS Components

### CPointLight

```cpp
// src/engine/components/point_light.h

struct CPointLight {
    float color[3];       // linear RGB, default [1, 1, 1]
    float intensity;      // luminous intensity (candela), default 1.0
    float radius;         // attenuation radius (meters), default 10.0
    bool  cast_shadow;    // whether this light casts shadows, default false
};
```

### CSpotLight

```cpp
// src/engine/components/spot_light.h

struct CSpotLight {
    float color[3];       // linear RGB, default [1, 1, 1]
    float intensity;      // luminous intensity (candela), default 1.0
    float range;          // maximum distance (meters), default 20.0
    float inner_angle;    // inner cone half-angle (degrees), default 15.0
    float outer_angle;    // outer cone half-angle (degrees), default 30.0
    bool  cast_shadow;    // whether this light casts shadows, default false
};
```

Both components are attached to scene nodes alongside a `CTransform` that provides world position and (for spot lights) orientation.

---

## .uzscene Format

```ini
[node SpotLamp1]
type = SpotLight
parent = Root
transform.position = [2, 5, -3]
transform.rotation = [45, 0, 0]

color = [1, 0.9, 0.8]
intensity = 2.0
range = 25.0
inner_angle = 20.0
outer_angle = 35.0
cast_shadow = true

[node PointLamp1]
type = PointLight
parent = Root
transform.position = [0, 3, 0]

color = [1, 1, 1]
intensity = 1.5
radius = 12.0
cast_shadow = true
```

---

## Point Light Shadows: Dual-Paraboloid Shadow Maps

### Why Dual-Paraboloid

Cubemap shadow maps require 6 render passes per point light. Dual-paraboloid shadow maps (DPSM) require only **2 passes** (front hemisphere + back hemisphere) while covering the full 360-degree light volume. This is a significant performance win for DX12, where each additional render pass incurs command list overhead.

### Algorithm

1. For each shadow-casting point light, render the scene twice:
   - **Front hemisphere**: Objects in front of the light (dot(L_to_obj, forward) >= 0)
   - **Back hemisphere**: Objects behind the light (dot(L_to_obj, forward) < 0)
2. In the vertex shader, transform vertices into light-space and apply the paraboloid projection:
   ```hlsl
   // Paraboloid projection
   float3 pos_ls = mul(light_view, float4(world_pos, 1.0)).xyz;
   float  L = length(pos_ls);
   pos_ls /= L;  // normalize to unit sphere

   // Front hemisphere: z' = z + 1; Back hemisphere: z' = -z + 1
   float z_offset = (is_front) ? 1.0 : -1.0;
   float2 projected = pos_ls.xy / (pos_ls.z * z_offset + 1.0);

   output.position = float4(projected, L / light_radius, 1.0);
   ```
3. Store depth as `L / light_radius` (normalized distance from light, [0..1]).

### Shadow Map Resolution

- **Per face**: 512 x 512 pixels
- **Per light**: 2 faces = 512 x 1024 pixels (stacked vertically in atlas)

### Point Light Shadow Atlas

- **Atlas size**: 2048 x 2048 pixels
- **Format**: `D32_FLOAT` (DSV) + `R32_FLOAT` (SRV for sampling)
- **Layout**: 4 columns x 4 rows of 512x512 tiles = 16 faces = 8 point lights
- **Max shadow-casting point lights**: 8 (configurable in `project.uzproj`)

```
Point Light Shadow Atlas (2048 x 2048):
+--------+--------+--------+--------+
| PL0 F  | PL1 F  | PL2 F  | PL3 F  |  row 0 (front hemispheres)
+--------+--------+--------+--------+
| PL0 B  | PL1 B  | PL2 B  | PL3 B  |  row 1 (back hemispheres)
+--------+--------+--------+--------+
| PL4 F  | PL5 F  | PL6 F  | PL7 F  |  row 2 (front hemispheres)
+--------+--------+--------+--------+
| PL4 B  | PL5 B  | PL6 B  | PL7 B  |  row 3 (back hemispheres)
+--------+--------+--------+--------+
```

Each tile is 512 x 512 pixels.

---

## Spot Light Shadows

### Projection

Each spot light uses a single perspective projection matrix based on its cone:

```cpp
// Build spot light shadow projection
float fov     = spot.outer_angle * 2.0f;   // full cone angle
float aspect  = 1.0f;                       // square shadow map
float near_z  = 0.1f;
float far_z   = spot.range;

mat4 proj = perspective_fov(fov, aspect, near_z, far_z);
mat4 view = look_at(spot.position, spot.position + spot.direction, up);
mat4 light_vp = proj * view;
```

### Shadow Map Resolution

- **Per light**: 512 x 512 pixels

### Spot Light Shadow Atlas

- **Atlas size**: 2048 x 2048 pixels
- **Format**: `D32_FLOAT` (DSV) + `R32_FLOAT` (SRV for sampling)
- **Layout**: 4 columns x 4 rows of 512x512 tiles = 16 spot lights
- **Max shadow-casting spot lights**: 16 (configurable in `project.uzproj`)

```
Spot Light Shadow Atlas (2048 x 2048):
+--------+--------+--------+--------+
| SL0    | SL1    | SL2    | SL3    |
+--------+--------+--------+--------+
| SL4    | SL5    | SL6    | SL7    |
+--------+--------+--------+--------+
| SL8    | SL9    | SL10   | SL11   |
+--------+--------+--------+--------+
| SL12   | SL13   | SL14   | SL15   |
+--------+--------+--------+--------+
```

Each tile is 512 x 512 pixels.

---

## Light Attenuation

### Point Light

Inverse-square falloff with a smooth window function to avoid hard cutoff at radius:

```hlsl
float attenuation_point(float dist, float radius) {
    float d2     = dist * dist;
    // Smooth window: (1 - (d/r)^2)^2 — reaches exactly 0 at radius
    float ratio  = dist / radius;
    float window = saturate(1.0 - ratio * ratio);
    window = window * window;
    // Physical inverse-square (add 1.0 to denominator to avoid singularity at d=0)
    return window / (d2 + 1.0);
}
```

### Spot Light

Point attenuation multiplied by angular falloff:

```hlsl
float attenuation_spot(float3 L, float3 spot_dir, float inner_cos, float outer_cos) {
    float cos_angle = dot(-L, spot_dir);
    // Smooth falloff between inner and outer cone
    return saturate((cos_angle - outer_cos) / (inner_cos - outer_cos + EPSILON));
}
```

---

## Lighting Pass Integration

### Tile-Based Light Culling (Compute Pre-Pass)

Before the deferred lighting pass, a compute shader culls lights per screen tile:

1. Divide screen into 16x16 pixel tiles.
2. For each tile, compute min/max depth from the depth buffer.
3. Build a frustum for each tile.
4. Test each point/spot light against the tile frustum.
5. Write per-tile light lists to a `RWStructuredBuffer`.

```hlsl
// Per-tile light list (in StructuredBuffer)
struct TileLightList {
    uint point_light_count;
    uint point_light_indices[MAX_LIGHTS_PER_TILE];  // 32
    uint spot_light_count;
    uint spot_light_indices[MAX_LIGHTS_PER_TILE];   // 32
};
```

### Deferred Lighting Evaluation

In the fullscreen deferred lighting shader, for each pixel:

1. Read G-Buffer (albedo, normal, roughness, metallic).
2. Reconstruct world position from depth.
3. Determine tile index from screen position.
4. Read tile light list.
5. For each point light in tile:
   a. Compute PBR lighting (Cook-Torrance BRDF from `brdf.hlsli`).
   b. If `cast_shadow`: sample dual-paraboloid shadow map from atlas.
   c. Multiply by shadow factor and attenuation.
6. For each spot light in tile:
   a. Compute PBR lighting.
   b. If `cast_shadow`: transform to light space, sample shadow atlas.
   c. Multiply by shadow factor, distance attenuation, and angular attenuation.
7. Add directional light contribution (CSM shadow from existing system).

---

## Shadow Sampling

### Point Light Shadow Sampling (Dual-Paraboloid)

```hlsl
float sample_point_shadow(float3 world_pos, PointLightShadowData light_data,
                          Texture2D point_shadow_atlas,
                          SamplerComparisonState shadow_sampler) {
    // Direction from light to surface
    float3 L = world_pos - light_data.position;
    float  dist = length(L);
    float3 dir  = L / dist;

    // Determine hemisphere (front or back)
    bool is_front = (dir.z >= 0.0);

    // Paraboloid projection
    float z_offset = is_front ? 1.0 : -1.0;
    float2 uv = dir.xy / (dir.z * z_offset + 1.0);
    uv = uv * 0.5 + 0.5;  // [-1,1] -> [0,1]

    // Offset into atlas tile
    float2 atlas_uv = light_data.atlas_offset + uv * light_data.atlas_scale;

    // Depth comparison value: normalized distance
    float depth = dist / light_data.radius;

    // PCF 2x2
    return shadow_pcf_2x2(point_shadow_atlas, shadow_sampler,
                          float3(atlas_uv, depth), light_data.texel_size);
}
```

### Spot Light Shadow Sampling

```hlsl
float sample_spot_shadow(float3 world_pos, SpotLightShadowData light_data,
                         Texture2D spot_shadow_atlas,
                         SamplerComparisonState shadow_sampler) {
    // Transform to light clip space
    float4 shadow_clip = mul(light_data.view_proj, float4(world_pos, 1.0));
    float3 shadow_ndc  = shadow_clip.xyz / shadow_clip.w;

    // Check if in light frustum
    if (any(abs(shadow_ndc.xy) > 1.0) || shadow_ndc.z < 0.0 || shadow_ndc.z > 1.0) {
        return 1.0;  // outside shadow map = not in shadow
    }

    // NDC to UV
    float2 uv = shadow_ndc.xy * float2(0.5, -0.5) + 0.5;

    // Offset into atlas tile
    float2 atlas_uv = light_data.atlas_offset + uv * light_data.atlas_scale;

    // PCF 2x2
    return shadow_pcf_2x2(spot_shadow_atlas, shadow_sampler,
                          float3(atlas_uv, shadow_ndc.z), light_data.texel_size);
}
```

---

## Shadow Data Structures

```cpp
// src/renderer/shadow_system.h

struct PointLightShadowData {
    float position[3];
    float radius;
    float atlas_offset[2];   // UV offset in atlas for front face
    float atlas_scale[2];    // UV scale (512/2048 = 0.25)
    float texel_size[2];     // 1.0 / 512.0
    uint  atlas_index;       // which slot in the atlas (0..7)
    float _pad;
};

struct SpotLightShadowData {
    float view_proj[16];     // 4x4 light view-projection matrix
    float atlas_offset[2];   // UV offset in atlas
    float atlas_scale[2];    // UV scale (512/2048 = 0.25)
    float texel_size[2];     // 1.0 / 512.0
    uint  atlas_index;       // which slot in the atlas (0..15)
    float _pad;
};
```

These are uploaded via `StructuredBuffer` and read in the lighting shader.

---

## Render Pass Integration

The shadow passes are registered in the render graph before the lighting pass:

```
PointLightShadowPass  (sort_key=110)  — render point light shadow atlas
SpotLightShadowPass   (sort_key=120)  — render spot light shadow atlas
ShadowPass            (sort_key=100)  — CSM directional (existing)
```

### Per-Frame Shadow Update

Not all shadow-casting lights need to update every frame. The engine prioritizes:

1. **Always update**: Lights whose transform changed this frame.
2. **Distance priority**: Closer lights to the camera update more frequently.
3. **Budget**: Maximum shadow map renders per frame = 8 point faces + 4 spot maps (configurable).
4. **Static lights**: If a light and all objects in its radius are static, cache the shadow map and skip re-rendering.

---

## Configuration (project.uzproj)

```toml
[renderer.shadows]
point_shadow_atlas_size = 2048       # pixels (square)
spot_shadow_atlas_size  = 2048       # pixels (square)
max_shadow_point_lights = 8          # simultaneous shadow-casting point lights
max_shadow_spot_lights  = 16         # simultaneous shadow-casting spot lights
shadow_tile_resolution  = 512        # per-light shadow map resolution
shadow_update_budget    = 12         # max shadow map renders per frame
point_shadow_pcf        = "2x2"      # "none", "2x2"
spot_shadow_pcf         = "2x2"      # "none", "2x2"
```

---

## IPC Handlers

```json
light.setPointLight { "node": "PointLamp1", "intensity": 2.0, "cast_shadow": true }
                --> { "ok": true }

light.setSpotLight  { "node": "SpotLamp1", "outer_angle": 40.0 }
                --> { "ok": true }

renderer.getShadowStats {}
                    --> {
                          "point_shadow_lights": 3,
                          "spot_shadow_lights": 5,
                          "shadow_renders_this_frame": 8,
                          "point_atlas_usage": "3/8",
                          "spot_atlas_usage": "5/16"
                        }
```

---

## Performance Notes

- **Point light shadow**: 2 draw passes per light per frame (when updated). Target < 0.5ms per light on RTX 3070.
- **Spot light shadow**: 1 draw pass per light per frame. Target < 0.3ms per light.
- **Shadow atlas memory**: Point atlas 2048x2048 D32 = 16 MB. Spot atlas 2048x2048 D32 = 16 MB. Total = 32 MB.
- **PCF 2x2 vs 3x3**: PCF 2x2 chosen for point/spot shadows because each light has a smaller shadow map (512x512). PCF 3x3 is reserved for the directional light CSM (2048x2048 tiles).
- **Tile culling**: Prevents evaluating distant lights on pixels they cannot reach. Critical for scenes with many lights.
