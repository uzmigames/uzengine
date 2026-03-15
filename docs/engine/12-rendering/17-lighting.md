# Lighting System

> **Status**: Partial (Phase 1 base + Phase 2+ planned)
> **Source**: `engine/lighting/`, `engine/subsystem/lighting_world.h`
> **Implemented**: Deferred shading evaluation (GBuffer + Lighting pass) via [11-deferred-pipeline.md](11-deferred-pipeline.md)
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

## Multi-Light System (Phase 1.3+)

### Overview

The **Multi-Light System** extends forward PBR shading to support up to **8 dynamic lights per frame** (directional + point + spot). This is a CPU-efficient culling system where:

1. **CPU side**: Selects the nearest N lights by distance to the camera and filters out lights whose centers are outside their attenuation radius.
2. **GPU side**: Evaluates all selected lights in a single forward pass without branching per-light-type — the shader dispatches to the appropriate lighting code based on the light type discriminant.

This system is **independent of shadow atlasing** (Phase 2+). Lights may cast shadows or not; the multi-light system is purely about which lights contribute to shading in the current frame.

### LightGPU Struct Layout (64 bytes)

Every light is packed into exactly **64 bytes** (four float4 rows) for optimal HLSL cbuffer alignment:

```cpp
struct alignas(16) LightGPU {
    // Row 0  [bytes 0..15]
    Vec3  positionOrDir;  ///< World position (point/spot) or direction TOWARD light (directional)
    float range;          ///< Attenuation radius in world units (0 = directional)

    // Row 1  [bytes 16..31]
    Vec3  color;          ///< Linear HDR color multiplied by intensity
    float lightType;      ///< 0=directional, 1=point, 2=spot (stored as float for HLSL)

    // Row 2  [bytes 32..47]
    Vec3  spotDirection;  ///< Normalized cone axis (spot lights only)
    float innerConeCos;   ///< cos(innerAngle) — full intensity inside this threshold

    // Row 3  [bytes 48..63]
    float outerConeCos;   ///< cos(outerAngle) — zero intensity outside this threshold
    float _pad[3];        ///< Explicit padding to 64 bytes
};
```

**Why this layout?**
- **HLSL compatibility**: Each row is 16 bytes (float4). Shaders access `lights[i].color` as a single float4 read.
- **Memory efficiency**: 8 lights × 64 bytes = 512 bytes of light data fits in 2 × 256-byte constant buffer slots.
- **Alignment safety**: 16-byte alignment (alignas(16)) matches D3D12 requirements for cbuffer arrays.

### PerFrameMultiLightCB (768 bytes)

Per-frame constant buffer containing camera state, timing, and the light array:

```cpp
struct PerFrameMultiLightCB {
    Mat4     viewProj         = {};          // [  0.. 63] (64 bytes)
    Vec4     cameraPos        = {};          // [ 64.. 79] (16 bytes)
    Vec4     timeParams       = {};          // [ 80.. 95] (16 bytes) x=totalTime, y=deltaTime, z=frameCount
    Vec4     ambientColor     = {};          // [ 96..111] (16 bytes)
    uint32_t lightCount       = 0;           // [112..115] (4 bytes)
    uint32_t _pad0[3]         = {};          // [116..127] (12 bytes, align array to 16)
    LightGPU lightArray[8]    = {};          // [128..639] (512 bytes)
    float    _pad1[32]        = {};          // [640..767] (128 bytes, pad to 768)
};

static_assert(sizeof(PerFrameMultiLightCB) % 256 == 0);
```

**Total size: 768 bytes = 3 × 256-byte slots** (satisfies D3D12 constant buffer alignment).

The `lightCount` field tells the shader how many entries in `lightArray[8]` are valid. Typically:
- `lightArray[0]` = primary directional light (sun)
- `lightArray[1..N]` = nearest point/spot lights selected by distance

### Light Attenuation Math

#### Point Light Distance Attenuation

**Formula (Frostbite / UE4-compatible):**

```
distRatio   = clamp(dist / range, 0, 1)
window      = (1 - distRatio²)²
attenuation = window / (dist² + 1)
```

**Properties:**
- Reaches exactly **0.0 at range boundary** (no popping when lights enter/exit culling range)
- Smooth falloff due to squared window function (second-order polynomial)
- Inverse-square with +1 bias avoids singularity at dist=0 and caps intensity at light center

**HLSL implementation:**
```hlsl
float ComputePointAttenuation(float dist, float range) {
    if (range <= 0.0f) return 0.0f;
    float ratio  = dist / range;
    float window = saturate(1.0f - ratio * ratio);
    window *= window; // quadratic window
    return window / (dist * dist + 1.0f);
}
```

#### Spot Light Cone Falloff

**Formula:**

```
cosAngle    = dot(-L, spotDir)  // angle between reversed ray and cone axis
t           = clamp((cosAngle - outerCos) / (innerCos - outerCos), 0, 1)
spotFactor  = t²                 // smooth quadratic penumbra
```

**Properties:**
- Returns **1.0 inside inner cone** (full intensity)
- Returns **0.0 outside outer cone** (no light)
- **Smooth penumbra between cones** via quadratic interpolation
- No sharp edges → no lighting artifacts at cone boundaries

**HLSL implementation:**
```hlsl
float ComputeSpotFactor(float3 L, float3 spotDir, float innerCos, float outerCos) {
    float cosAngle = dot(-L, spotDir);
    if (cosAngle >= innerCos) return 1.0f;
    if (cosAngle <= outerCos) return 0.0f;
    float t = saturate((cosAngle - outerCos) / (innerCos - outerCos));
    return t * t;
}
```

### CPU Light Culling (SelectNearestLights)

**Purpose:** Select the nearest N lights by distance to camera, filtering out lights far outside their range.

**Algorithm:**

```cpp
uint32_t SelectNearestLights(const LightCPU* candidates,
                             uint32_t count,
                             const Vec3& cameraPos,
                             LightGPU* outLights,
                             uint32_t outCapacity) {
    // 1. For each light:
    for (each light in candidates) {
        if (light.type == Directional) {
            // Directional lights always included (infinite range)
            sortBuffer[validCount++] = {lightIndex, 0.0f};  // sqDist = 0 for sorting
        } else {
            // Point/Spot: compute squared distance to camera
            float sqDist = (light.position - cameraPos).LengthSquared();
            if (sqDist <= light.range * light.range) {
                // Light center is within range of camera
                sortBuffer[validCount++] = {lightIndex, sqDist};
            }
            // else: skip light (camera is outside its attenuation radius)
        }
    }

    // 2. Sort by squared distance (ascending = nearest first)
    std::sort(sortBuffer.begin(), sortBuffer.end(),
              [](const SortEntry& a, const SortEntry& b) {
                  return a.sqDist < b.sqDist;
              });

    // 3. Convert top N sorted lights to LightGPU format and write output
    for (uint32_t i = 0; i < min(validCount, outCapacity); ++i) {
        const LightCPU& src = candidates[sortBuffer[i].index];
        LightGPU& dst = outLights[i];

        dst.positionOrDir = src.position;
        dst.range = src.range;
        dst.color = src.color;
        dst.lightType = (float)src.type;
        dst.spotDirection = src.direction;
        dst.innerConeCos = cos(src.innerAngle);
        dst.outerConeCos = cos(src.outerAngle);
    }

    return numWritten;
}
```

**Filtering rules:**
- **Directional lights**: Always included (range = 0, infinite distance).
- **Point/Spot lights**: Included only if camera's squared distance is ≤ `range²`.
- **Overflow**: If more than `kMaxMultiLights` (8) lights pass filtering, the nearest 8 are selected.

**Why sort by squared distance?**
- Avoids expensive sqrt during culling (one multiply is cheaper than one sqrt).
- Preserves ordering: if `dist_a < dist_b` then `sqDist_a < sqDist_b`.

### Per-Frame CPU Setup (BuildPerFrameMultiLightCB)

**Typical frame setup:**

```cpp
// 1. Collect CPU-side light data (e.g., from LightComponent nodes)
std::vector<LightCPU> lights = GatherSceneLights();

// 2. Build the multi-light CB (culls + converts to GPU format)
PerFrameMultiLightCB cb = BuildPerFrameMultiLightCB(
    camera,                    // Camera position + view/proj matrices
    totalTime,
    deltaTime,
    frameIndex,
    lighting,                  // SceneLighting with primary directional light
    lights.data(),
    lights.size()
);

// 3. Upload to GPU
memcpy(constantBufferGPUPtr, &cb, sizeof(cb));
device->BindConstantBuffer(ShaderStage::Pixel, 1, constantBufferGPU);
```

**Key behavior:**
- The **primary directional light** (sun) from `SceneLighting` is **always placed at `lightArray[0]`**, even if it was not in the `candidates` list.
- If the candidates array produces 8 lights, the sun shifts the last candidate out.
- This ensures the shader can always assume `lightArray[0]` is the main directional light for shadow-map lookups.

### HLSL Integration

#### Include Headers

**`light_types.hlsli`** — Struct definition only:
```hlsl
static const uint MAX_LIGHTS = 8;

struct LightData {
    float3 positionOrDir;
    float  range;
    float3 color;
    float  lightType;       // 0=directional, 1=point, 2=spot
    float3 spotDirection;
    float  innerConeCos;
    float  outerConeCos;
    float  _pad0, _pad1, _pad2;
};
```

**`lighting.hlsli`** — Attenuation and accumulation functions:
```hlsl
float ComputePointAttenuation(float dist, float range) { ... }
float ComputeSpotFactor(float3 L, float3 spotDir, float innerCos, float outerCos) { ... }

float3 AccumulateDirectLighting(
    float3 positionWS, float3 N, float3 V, float NdotV,
    float3 albedo, float metallic, float alpha, float3 F0,
    uint lightCount, LightData lights[MAX_LIGHTS],
    float3 cameraPos, uint cascadeCount) { ... }
```

#### Typical Pixel Shader Usage

```hlsl
cbuffer PerFrameMultiLightCB : register(b1) {
    float4x4 viewProj;
    float4   cameraPos;
    float4   timeParams;
    float4   ambientColor;
    uint     lightCount;
    uint     _pad0[3];
    LightData lights[8];
    float    _pad1[32];
};

float4 PSMain(PixelInput input) : SV_TARGET {
    float3 positionWS = input.positionWS;
    float3 N = normalize(input.normalWS);
    float3 V = normalize(cameraPos.xyz - positionWS);
    float NdotV = max(dot(N, V), 0.001f);

    // Fetch material
    float3 albedo = SampleDiffuseMap(input.uv).rgb;
    float metallic = SampleMetallicMap(input.uv).r;
    float roughness = SampleRoughnessMap(input.uv).r;
    float alpha = roughness * roughness;

    // Accumulate all lights
    float3 Lo = AccumulateDirectLighting(
        positionWS, N, V, NdotV,
        albedo, metallic, alpha, F0,
        lightCount, lights,
        cameraPos.xyz, cascadeCount);

    // Add ambient
    float3 ambient = ambientColor.rgb * albedo;

    return float4(Lo + ambient, 1.0f);
}
```

#### Shadow Mapping Integration

- **Shadow mapping only applies to `lights[0]` (primary directional light)** — evaluated via `SampleShadowMap(positionWS, N, viewDepth)`.
- Other lights (point/spot) do **not** cast shadows in Phase 1.3 (shadow atlasing is Phase 2+).
- The `cascadeCount` parameter tells the shader whether cascaded shadow maps are active; if 0, shadows are skipped.

### File Locations

| Component | File | Purpose |
|-----------|------|---------|
| **C++ Structs** | `runtime/engine/renderer/core/multi_light.h` | `LightGPU`, `PerFrameMultiLightCB`, `LightCPU`, culling + attenuation math |
| **C++ Helpers** | `runtime/engine/renderer/core/scene_lighting.h` | `SceneLighting`, `BuildPerFrameMultiLightCB()` |
| **HLSL Types** | `runtime/shaders/runtime/include/light_types.hlsli` | `LightData` struct for GPU |
| **HLSL Lighting** | `runtime/shaders/runtime/include/lighting.hlsli` | `ComputePointAttenuation()`, `ComputeSpotFactor()`, `AccumulateDirectLighting()` |
| **Pixel Shader** | `runtime/shaders/runtime/mesh_ps.hlsl` | Example usage in forward PBR pass |
| **Tests** | `runtime/tests/engine/renderer/test_multi_light.cpp` | 47 tests: struct layout, attenuation, culling, edge cases |

### Testing Strategy

All multi-light math functions are **pure CPU** (no RHI, no GPU):

```cpp
TEST(MultiLightTest, ComputePointAttenuation_ZeroAtRange) {
    float atten = ComputePointAttenuation(10.0f, 10.0f);
    EXPECT_EQ(atten, 0.0f) << "Attenuation must be exactly 0 at range boundary";
}

TEST(MultiLightTest, SelectNearestLights_CullsOutOfRange) {
    LightCPU lights[3] = { ... };
    LightGPU out[8];
    uint32_t count = SelectNearestLights(lights, 3, cameraPos, out, 8);
    EXPECT_EQ(count, 1) << "Only in-range light selected";
}

TEST(MultiLightTest, PerFrameMultiLightCB_SizeIs768) {
    EXPECT_EQ(sizeof(PerFrameMultiLightCB), 768u)
        << "CB must be 3x256 bytes for D3D12 alignment";
}
```

**Coverage**: 47 tests covering struct layout, memory alignment, attenuation math, cone falloff, light sorting, culling edge cases, and constant buffer population.

## Rules

1. **NEVER** access shadow atlas directly — only `LightingWorld` manages allocation and updates
2. **ALWAYS** set `cast_shadows = false` for decorative lights (cheap way to reduce shadow cost)
3. **NEVER** create more than 16 high-priority dynamic lights per frame — respect shadow budget
4. **ALWAYS** use `SkyLight` for environment — don't manually set ambient color
5. **NEVER** scale `intensity` to 10+ to compensate for dark lighting — use exposure/tone mapping instead
6. **ALWAYS** ensure light colors are in **linear HDR space** (not gamma-corrected) before passing to GPU
7. **NEVER** create lights outside the culling range of the camera and expect them to shade — use `range` to control visibility distance
8. **ALWAYS** normalize `spotDirection` on CPU before uploading — the shader assumes unit length
9. **NEVER** set `innerAngle > outerAngle` — this produces undefined cone falloff behavior
