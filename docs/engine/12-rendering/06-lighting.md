# Lighting System

> **Status**: Partial (Phase 1 base + Phase 2+ planned)
> **Source**: `engine/lighting/`, `engine/subsystem/lighting_world.h`
> **Implemented**: Deferred shading evaluation (GBuffer + Lighting pass) via [10-deferred-pipeline.md](10-deferred-pipeline.md)
> **References**: UE5 Lumen, Virtual Shadow Maps, Light Channels; Godot VoxelGI, SDFGI, LightmapGI

## Purpose

The Lighting System manages all dynamic and static light sources in the world. It handles:

- **Light component types**: Directional, Point, Spot, Rect, Sky lights with per-type properties
- **Shadow generation**: Cascaded shadow maps (directional), atlased shadow maps (point/spot), ray-traced shadows (future)
- **Light evaluation**: Deferred shading via G-Buffer + Lighting pass (Milestone 1.3), forward shading for transparents (Milestone 1.2)
- **Light culling**: Tiled Forward+ (Phase 2), cluster-based deferred (Phase 3+), virtual shadow maps (Phase 4)
- **Indirect illumination**: Light probes (Phase 3), screen-space GI (Phase 3), ray-traced GI (Phase 4+)

Game code interacts with lights through components and material properties, never through low-level shadow or culling APIs.

## Light Types

All lights inherit from a base `Light` component with common properties (intensity, color, range, shadows enabled). Type-specific behavior is implemented via virtual methods:

```cpp
/// Base light component. Drives a corresponding render proxy with culling + shadow data.
struct Light {
    Color     color;
    float     intensity;
    bool      cast_shadows;
    uint8_t   shadow_priority;  // 0=highest, determines atlas budget
};

/// Directional light — infinite light from a direction (sun, moon).
struct DirectionalLight : Light {
    float     min_roughness;       // tighten PCF on shiny surfaces
    uint8_t   cascade_count;       // 1-4 (Phase 2: 4 fixed)
    float     cascade_blend_width; // transition smoothing
};

/// Point light — omni-directional light from a position.
struct PointLight : Light {
    float     attenuation_radius;  // falloff distance
    float     source_radius;       // for soft shadows
};

/// Spot light — cone of light from a position and direction.
struct SpotLight : Light {
    float     attenuation_radius;
    float     inner_cone_angle;
    float     outer_cone_angle;
    float     source_radius;
};

/// Area/Rect light — approximated as emissive surface.
struct RectLight : Light {
    Vec2      dimensions;           // width, height
    bool      two_sided;
};

/// Sky light — captures environment for indirect lighting.
struct SkyLight : Light {
    ResourceHandle<Texture> cubemap;  // environment map
    float                   intensity;
};
```

## Shadow Techniques

### Cascaded Shadow Maps (CSM) — Phase 2

Directional lights use **4 cascades** with stabilized projection:

```
Camera Frustum
├─ Cascade 0: Near plane → 10m   (finest detail, high res)
├─ Cascade 1: 10m → 50m          (medium detail)
├─ Cascade 2: 50m → 200m         (low detail)
└─ Cascade 3: 200m → Far plane   (far background)

Each cascade = separate 2K shadow map texture
Light renders geometry into each cascade independently
Depth comparison in shader selects cascade by view depth
Smooth transition via blend_width
```

**Why stabilize projection?** Without stabilization, camera movement causes shadow pattern swimming. Solution: snap cascade projection to texel boundaries.

### Shadow Atlas — Phase 2/3

Point and Spot lights share a **dynamic shadow atlas**:
- Atlas size: 2048×2048 tiled into 512×512 slots (16 lights max)
- Allocation: Priority queue — highest-priority lights get atlas space
- Update: Lights render cubemap/2D shadow maps only when dirty (moved, newly created, or marked dynamic)
- Fallback: Unshadowed lights use distance attenuation only

### Virtual Shadow Maps (VSM) — Phase 4

Tile-based, cached shadow system used by AAA engines:
- Single high-res shadow map (sparse, 8K or 16K physical)
- Tiled into virtual pages, pages cached to physical memory
- Fine-grained culling — only draw triangles that affect shadows
- **Much more efficient for many dynamic lights**
- Requires GPU-driven rendering pipeline

## Light Culling

### Tiled Forward+ — Phase 2

Divide screen into 16×16 pixel tiles. For each tile:

1. Build a **light list** — which lights can affect this tile?
2. Every pixel in tile uses same light list (16-light limit per tile)
3. Shade with only relevant lights (no full-screen light loop)

```
Scene with 200 lights
└─ Partition screen into tiles (32×18 for 1920×1080)
   └─ Each tile computes ~5-10 relevant lights
      └─ Pixel shader samples only those lights
         Result: 200 lights → effective 5-10 per pixel
```

**Alternative: Deferred Tile Classification** — Phase 3+
- Render world-space G-buffers (albedo, normal, depth)
- Classify tiles by depth range (what's in front? what's behind?)
- Lights only shade tiles they intersect (even more culling)

## Indirect Lighting

### Light Probes (SH9 Grid) — Phase 3

Bake spherical harmonic (SH9 = 9 coefficients) at grid points:
- SH coefficients encode how much light arrives from every direction
- Place probes 2m apart in level (art tool generates grid)
- Dynamic objects sample nearby probes, blend by distance
- Cheap: SH dot product per probe in pixel shader

```cpp
/// Runtime light probe sample. Blends 8 nearest probes by distance.
struct LightProbeContribution {
    Vec3 diffuse_radiance;
    Vec3 specular_radiance;  // roughness-biased
};
```

### Reflection Probes (Cubemap, Parallax-Corrected) — Phase 3

Bake environment reflection at strategic positions:
- Capture cubemap (6 faces, 128×128)
- Bind roughness-filtered mips (high roughness = blurry reflection)
- Parallax correction: adjust reflection ray to account for probe box shape
- Fallback: Screen-space reflections for real-time updates

### Image-Based Lighting (IBL) — Phase 2

Every material references an **environment map**:
```cpp
struct Material {
    ResourceHandle<Texture> environment_map;      // cubemap
    ResourceHandle<Texture> brdf_lookup_table;    // 2D LUT: (roughness, NdotV) → F0 scale
};
```

In pixel shader:
```glsl
// Indirect specular = sample prefiltered environment + BRDF lookup
vec3 F = texture(brdf_lut, vec2(roughness, dot(N, V))).rgb;
vec3 indirect_spec = textureLod(environment_map, R, roughness * 10.0).rgb * F;
```

### Screen-Space GI (SSGI) — Phase 3

Real-time indirect illumination from on-screen geometry:
- Ray-march in screen space from each pixel
- Sample GBuffer (depth, normal) to find intersections
- Fallback to light probes if ray misses on-screen geometry
- Requires depth pyramid + normal buffer

### Hardware Ray-Traced GI (DXR) — Phase 4+

Use DirectX Raytracing to compute true global illumination:
- Trace GI rays per pixel via `TraceRay()` in ray-generation shader
- Hit shaders sample material at intersection point
- Bounce count: 1-2 bounces (more = expensive)
- Cached + temporal filtering to smooth results

## Volumetric Effects

### Ray-Marched Fog — Phase 3

Sample inscatter + transmittance along camera ray:

```glsl
vec3 volumetric_light = vec3(0);
for (int step = 0; step < 16; step++) {
    vec3 sample_pos = ray_origin + ray_dir * step_distance;
    float density = sample_density(sample_pos);
    vec3 light_contrib = sample_lights(sample_pos);
    volumetric_light += density * light_contrib * step_distance;
}
```

### Volumetric Lighting (God Rays) — Phase 3

Post-process effect over 3D lit fog, adds sun rays through clouds/dust.

## Light Functions & Shaping

### IES Profiles — Phase 3

Photometric light profile from `.ies` file (real-world light manufacture data):
```
IES defines intensity distribution in polar coordinates
Encode as 2D texture (angle vs elevation)
Sample in shader to modulate light intensity per direction
Result: realistic falloff (not perfect sphere)
```

### Projected Textures — Phase 3

Light projects a texture (spotlight with cookie, gobo pattern):
```cpp
struct SpotLight : Light {
    ResourceHandle<Texture> projection_texture;
    float projection_distance;  // how far texture projects
};
```

## Emissive Surfaces as Area Lights

Game artists mark materials with **emissive flag**:
- Phase 2: Emissives contribute to full-screen ambient (simple hack)
- Phase 3: Approximated as dynamic area lights (limited count, update every N frames)
- Phase 4: Traced as analytical area lights (full LTC, accurate soft shadows)

## Exposure & Tone Mapping

### Auto-Exposure — Phase 3

Compute **histogram** of HDR framebuffer:
```cpp
// Histogram: count pixels in luminance ranges [0, 0.1), [0.1, 0.2), ..., [4.0, inf)
// Adapt exposure to keep histogram peak near center (optimal exposure)
struct ExposureSettings {
    float current_exposure;
    float target_exposure;
    float adaptation_speed;  // how fast to reach target (0.1-2.0)
};
```

### Tone Mapping Operators — Phase 2

Multiple tone mapping curves available:
```cpp
enum class ToneMapMode {
    Linear,       // no compression
    ACES,         // film industry standard
    Reinhard,     // simple, effective
    Uncharted2,   // game standard (used in UE5)
    CustomCurve,  // artist-defined spline
};
```

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 1 (Milestone 1.2)** | Forward PBR shading, single directional light + ambient, Cook-Torrance BRDF, Reinhard tonemap. |
| **Phase 1.3 (Milestone 1.3)** | ✅ Deferred G-Buffer + Lighting pass, up to 64 point lights, smooth attenuation, Cook-Torrance evaluation per-light. |
| **Phase 2** | DirectionalLight + CSM (4 cascades), PointLight + SpotLight shadow atlas, tiled forward light culling, IBL (prefiltered env maps), ACES tone mapping. |
| **Phase 3** | Light probes (SH9 grid), reflection probes (cubemap + parallax), SSGI, volumetric fog/rays, auto-exposure, IES profiles, emissive area lights (approximated), light functions (projected textures). |
| **Phase 4** | Virtual Shadow Maps, hardware ray-traced GI (DXR), LTC area light shading, clustered deferred, 100+ dynamic lights with full quality. |

## Engine Comparison

| Feature | Unreal 5 | Godot 4 | **UzEngine** |
|---------|----------|---------|--------------|
| Directional shadows | Cascaded (4 default) | Same | **Cascaded (4 fixed Phase 2)** |
| Dynamic point shadows | Atlas (512×512 slots) | Per-light cubemap | **Dynamic atlas Phase 2/3** |
| Indirect lighting | Lumen (RT) or baked | VoxelGI, SDFGI, baked | **Probes Phase 3 + SSGI Phase 3** |
| Light culling | Deferred (tile + cluster) | Forward + culling | **Tiled Forward+ Phase 2** |
| Reflection | Reflection captures + SSR | Reflection probes + SSR | **Probes Phase 3 + SSGI Phase 3** |
| Ray-traced GI | Lumen (Nanite + GPU RT) | Via Godot RT | **Hardware RT Phase 4+** |

## Rules

1. **NEVER** access shadow atlas directly — only `LightingWorld` manages allocation and updates
2. **ALWAYS** set `cast_shadows = false` for decorative lights (cheap way to reduce shadow cost)
3. **NEVER** create more than 16 high-priority dynamic lights per frame — respect shadow budget
4. **ALWAYS** use `SkyLight` for environment — don't manually set ambient color
5. **NEVER** scale `intensity` to 10+ to compensate for dark lighting — use exposure/tone mapping instead
