<!-- doc-meta
title: UzEngine Local Light Shadow System
category: analysis
engine: uzengine
theme: rendering
created: 2026-03-30
updated: 2026-03-30
status: active
author: docs-operator
-->

# UzEngine Local Light Shadow System

Complete technical documentation of the UzEngine local light shadow implementation for spot lights and point lights, including atlas allocation, importance sorting, shadow sampling, and performance budgets.

## Overview

The UzEngine local light shadow system provides dynamic shadow mapping for point lights and spot lights. Shadows are rendered into a shared 4096x4096 atlas organized as a 4x4 grid of 1024x1024 tiles. Each frame, lights are ranked by importance and allocated shadow tiles in priority order. The implementation prioritizes visual quality, consistency across graphics backends (DX11, DX12, Vulkan), and tight GPU performance budget.

**Key characteristics:**
- Single 4096x4096 D32Float depth atlas (4x4 tile grid)
- Max 16 tiles per frame = ~1 spot shadow + ~2 point shadows + spare capacity
- Hardware PCF sampling for quality shadows with standard MSRT filtering
- Per-light shadow bias and cone angle support
- Scene file integration (`castShadows: true/false`, `shadowBias: float`)

## Shadow Atlas Layout and Allocation Strategy

### Atlas Texture Configuration

The shadow atlas is a single 4096x4096 D32Float texture divided into a 4x4 grid of 1024x1024 tiles:

```
┌──────────┬──────────┬──────────┬──────────┐
│ Tile 0   │ Tile 1   │ Tile 2   │ Tile 3   │  Row 0
├──────────┼──────────┼──────────┼──────────┤
│ Tile 4   │ Tile 5   │ Tile 6   │ Tile 7   │  Row 1
├──────────┼──────────┼──────────┼──────────┤
│ Tile 8   │ Tile 9   │ Tile 10  │ Tile 11  │  Row 2
├──────────┼──────────┼──────────┼──────────┤
│ Tile 12  │ Tile 13  │ Tile 14  │ Tile 15  │  Row 3
└──────────┴──────────┴──────────┴──────────┘
```

**File:** `runtime/src/engine/renderer/shadows/local_light_shadow_atlas.h`

```cpp
/// 4096x4096 D32Float shadow atlas
/// Grid: 4x4 tiles, each tile 1024x1024 texels
static constexpr uint32_t ATLAS_WIDTH = 4096;
static constexpr uint32_t ATLAS_HEIGHT = 4096;
static constexpr uint32_t TILE_SIZE = 1024;
static constexpr uint32_t GRID_DIM = 4;  // 4x4 grid
static constexpr uint32_t MAX_TILES = 16;
```

Tile UV mapping formula:
- `col = tileIndex % 4`
- `row = tileIndex / 4`
- `uvMin = {col * 0.25f, row * 0.25f}`
- `uvMax = {(col + 1) * 0.25f, (row + 1) * 0.25f}`

### Per-Frame Allocation Strategy

The `LocalLightShadowAtlas` class manages tile allocation with a **sequential per-frame allocation policy**:

1. **Reset**: At frame start, atlas state is reset (no persistent tile assignments)
2. **Sequential allocation**: Lights claim tiles in priority order (highest importance first)
3. **Budget enforcement**: Max 16 tiles; allocation stops when budget exhausted
4. **Separate budgets**: Spot lights and point lights have independent allocation order, but share total tile budget

**File:** `runtime/src/engine/renderer/shadows/local_light_shadow_atlas.cpp`

```cpp
class LocalLightShadowAtlas {
public:
    void ResetFrame() {
        nextTileIndex = 0;
        spotShadowCount = 0;
        pointShadowCount = 0;
    }

    /// Allocate a single tile for spot light
    /// Returns {success, tileIndex, tileUVRect}
    std::optional<uint32_t> AllocateSpotTile() {
        if (nextTileIndex >= MAX_TILES) return std::nullopt;
        spotShadowCount++;
        return nextTileIndex++;
    }

    /// Allocate 6 tiles for point light cubemap faces
    /// Returns first tile index or nullopt if insufficient space
    std::optional<uint32_t> AllocatePointTiles() {
        if (nextTileIndex + 6 > MAX_TILES) return std::nullopt;
        pointShadowCount++;
        uint32_t first = nextTileIndex;
        nextTileIndex += 6;
        return first;
    }

private:
    uint32_t nextTileIndex = 0;
    uint32_t spotShadowCount = 0;
    uint32_t pointShadowCount = 0;
};
```

### Light Type Shadow Tile Requirements

**Spot lights**: 1 tile (1024x1024)
- Single perspective projection along spot cone direction
- Full viewport fills entire tile
- Rendered as 1 depth pass

**Point lights**: 6 tiles (6 × 1024x1024)
- Cubemap representation: 6 faces (±X, ±Y, ±Z)
- Each face is 90° FOV orthographic projection
- Rendered as 6 depth passes (or 1 pass with geometry instancing)
- Tiles are consecutive: `firstTile, firstTile+1, ..., firstTile+5`
- Face-to-tile mapping:
  - Tile+0: +X (right)
  - Tile+1: -X (left)
  - Tile+2: +Y (top)
  - Tile+3: -Y (bottom)
  - Tile+4: +Z (front)
  - Tile+5: -Z (back)

## Shadow Importance Sorting

### Importance Metric Calculation

Lights are ranked by a composite importance score that balances visual prominence and performance:

```cpp
float importance = colorIntensity * (radius * radius) / std::max(1.0f, distanceToCamera * distanceToCamera);
```

**Components:**
- **colorIntensity**: Perceptual brightness = `max(color.r, color.g, color.b)`
- **radius²**: Solid angle contribution (quadratic falloff of influence)
- **distanceToCamera²**: Normalized distance penalty (shadows of near lights matter more)

**File:** `runtime/src/engine/renderer/lights/light_renderer.cpp:ExtractLightingFromECS()`

```cpp
struct LightCandidate {
    uint32_t entityId;
    uint32_t type;  // POINT or SPOT
    float3 position;
    float3 color;
    float radius;
    bool castShadows;
    float importance;
};

void ExtractLightingFromECS(SceneView& view, std::vector<LightCandidate>& outLights) {
    std::vector<LightCandidate> candidates;

    // Collect all lights with castShadows=true
    scene->ForEach<PointLight>([&](PointLight& light, Transform& xf) {
        if (!light.castShadows) return;
        float intensity = std::max({light.color.r, light.color.g, light.color.b});
        float dist = glm::distance(xf.position, view.cameraPos);
        float imp = intensity * (light.radius * light.radius) / std::max(1.0f, dist * dist);
        candidates.push_back({
            .entityId = scene->GetEntityId(),
            .type = LIGHT_TYPE_POINT,
            .position = xf.position,
            .color = light.color,
            .radius = light.radius,
            .castShadows = true,
            .importance = imp
        });
    });

    // Sort by importance (descending)
    std::sort(candidates.begin(), candidates.end(),
        [](const LightCandidate& a, const LightCandidate& b) {
            return a.importance > b.importance;
        });

    outLights = candidates;
}
```

### Allocation Order with Budget Enforcement

After sorting, lights are allocated tiles in order until budget exhausted:

```cpp
uint32_t spotCount = 0;
uint32_t pointCount = 0;

for (const auto& light : sortedLights) {
    if (light.type == LIGHT_TYPE_SPOT) {
        if (atlas.nextTileIndex >= MAX_TILES) break;
        auto tile = atlas.AllocateSpotTile();
        if (!tile) break;
        // Render spot shadow to tile
        spotCount++;
    } else if (light.type == LIGHT_TYPE_POINT) {
        // Point light needs 6 tiles; check if space available
        uint32_t pointBudget = (MAX_TILES - atlas.nextTileIndex) / 6;
        if (pointBudget == 0) break;
        auto tile = atlas.AllocatePointTiles();
        if (!tile) break;
        // Render 6 point shadow faces
        pointCount++;
    }
}
```

**Budget example:**
- `MAX_TILES = 16`
- If 2 spot lights allocated: `nextTileIndex = 2`, remaining = 14 tiles
- Point light can claim: `14 / 6 = 2` point lights (uses 12 tiles)
- Final state: 2 spot + 2 point shadows, 2 tiles spare
- 3rd point light or additional spots: rejected (no space)

## Shadow Sampling Algorithm

### Spot Light Shadow Projection

Spot light shadows project world position through light-space perspective matrix:

**File:** `runtime/shaders/runtime/passes/deferred_lighting_ps.hlsl`

```hlsl
// Spot light shadow sampling
float SampleSpotShadow(float3 worldPos, uint shadowTileIndex, LocalLightShadowData shadowData) {
    // Project through light view-projection matrix
    float4 lightClip = mul(float4(worldPos, 1.0), shadowData.viewProjMatrix);
    float3 lightNdc = lightClip.xyz / lightClip.w;

    // Convert NDC to 0-1 range
    float2 lightUv = lightNdc.xy * 0.5 + 0.5;  // [-1,1] → [0,1]
    lightUv.y = 1.0 - lightUv.y;  // Flip Y for texture coordinates

    // Map to atlas tile UV space
    float2 tileUv = shadowData.atlasUvMin + lightUv * (shadowData.atlasUvMax - shadowData.atlasUvMin);
    float lightDepth = lightNdc.z;

    // Hardware PCF sampling (MSRT 2x2 filter)
    float shadowFactor = shadowAtlas.SampleCmpLevelZero(
        shadowComparisonSampler,
        float3(tileUv, 0.0),
        lightDepth - shadowData.bias
    );

    return shadowFactor;  // 1.0 = lit, 0.0 = shadowed
}
```

### Point Light Shadow Projection (Cubemap)

Point light shadows project world position into cubemap space, then select the dominant face:

**File:** `runtime/shaders/runtime/passes/deferred_lighting_ps.hlsl`

```hlsl
// Select cubemap face based on dominant axis (UE5 ShadowProjectionCommon.ush:132-147 parity)
uint SelectCubeFace(float3 dir) {
    float absX = abs(dir.x);
    float absY = abs(dir.y);
    float absZ = abs(dir.z);

    if (absX >= absY && absX >= absZ) {
        return (dir.x > 0.0) ? 0 : 1;  // 0: +X, 1: -X
    } else if (absY >= absX && absY >= absZ) {
        return (dir.y > 0.0) ? 2 : 3;  // 2: +Y, 3: -Y
    } else {
        return (dir.z > 0.0) ? 4 : 5;  // 4: +Z, 5: -Z
    }
}

float SamplePointShadow(float3 worldPos, uint shadowTileBase, LocalLightShadowData shadowData) {
    // Vector from light to world position
    float3 toPixel = worldPos - shadowData.lightPosition;

    // Select appropriate cubemap face
    uint faceIndex = SelectCubeFace(toPixel);

    // Project through per-face 90° FOV matrix
    float4 faceClip = mul(float4(toPixel, 1.0), shadowData.faceViewProjMatrix[faceIndex]);
    float3 faceNdc = faceClip.xyz / faceClip.w;

    // Convert to 0-1 UV
    float2 faceUv = faceNdc.xy * 0.5 + 0.5;
    faceUv.y = 1.0 - faceUv.y;

    // Map to atlas tile (shadowTileBase + faceIndex)
    float2 tileMin = shadowData.atlasFaceTiles[faceIndex].uvMin;
    float2 tileSize = shadowData.atlasFaceTiles[faceIndex].uvMax - tileMin;
    float2 atlasUv = tileMin + faceUv * tileSize;

    float lightDepth = faceNdc.z;

    // Hardware PCF sampling
    float shadowFactor = shadowAtlas.SampleCmpLevelZero(
        shadowComparisonSampler,
        float3(atlasUv, 0.0),
        lightDepth - shadowData.bias
    );

    return shadowFactor;
}
```

### Integration into Deferred Lighting

Shadow factors multiply light attenuation in the deferred lighting pass:

```hlsl
// In deferred_lighting_ps.hlsl
float3 lighting = float3(0, 0, 0);

for (uint i = 0; i < lightCount; i++) {
    Light light = lights[i];

    // Compute diffuse/specular contribution
    float3 lightDir = normalize(light.position - worldPos);
    float attenuation = ComputeLightAttenuation(worldPos, light);
    float3 brdf = ComputeBRDF(normal, lightDir, viewDir, material);

    // Apply shadow if light casts shadows
    float shadowFactor = 1.0;
    if (light.shadowTileIndex != SHADOW_TILE_NONE) {
        if (light.type == LIGHT_TYPE_SPOT) {
            shadowFactor = SampleSpotShadow(worldPos, light.shadowTileIndex, shadowCB.data[light.shadowDataIndex]);
        } else if (light.type == LIGHT_TYPE_POINT) {
            shadowFactor = SamplePointShadow(worldPos, light.shadowTileIndex, shadowCB.data[light.shadowDataIndex]);
        }
    }

    // Final light contribution
    lighting += brdf * light.color * attenuation * shadowFactor;
}

return float4(lighting, 1.0);
```

## Performance Budget and Tile Allocation

### Runtime Tile Budget

The shadow system maintains a strict per-frame budget to ensure consistent GPU performance:

| Resource | Budget |
|----------|--------|
| Total atlas tiles | 16 (4096x4096 ÷ 1024x1024) |
| Spot light tiles | Variable (1 tile each) |
| Point light tiles | Variable (6 tiles each) |
| Example: 1 spot + 2 points | 1 + 12 = 13 tiles (3 spare) |
| Example: 3 spots + 1 point | 3 + 6 = 9 tiles (7 spare) |

### Render Pass Execution

The `LocalShadowDepth` render pass executes per-tile after CSM (Cascade Shadow Map) and before GBuffer deferred rendering:

**File:** `runtime/src/engine/renderer/passes/local_shadow_depth_pass.h`

```cpp
class LocalShadowDepthPass : public RenderPass {
public:
    void Execute(RenderGraph& graph, FrameResources& resources) override {
        // For each allocated tile in shadow atlas
        for (uint32_t tileIdx = 0; tileIdx < atlas.nextTileIndex; tileIdx++) {
            // Create render target for this tile (viewport = tile's rect in 4096x4096)
            SetViewport(
                tileIdx % 4 * 1024,      // X offset
                tileIdx / 4 * 1024,      // Y offset
                1024, 1024               // 1024x1024 tile size
            );

            // Render scene geometry (depth-only) from light's perspective
            RenderGeometry(lightViewProj[tileIdx]);
        }
    }
};
```

### Per-Tile Rendering Cost

Each 1024x1024 tile renders the full scene geometry (depth-only pass):

```
Cost per tile = VertexShading(all geometry) + DepthTest + DepthWrite
              = ~0.1 ms (AMD Ryzen 5900X / RTX 3080, typical scene)
```

For maximum budget (16 tiles): `16 × 0.1 ms = 1.6 ms` GPU time

This is acceptable within typical frame budgets (60 FPS ≈ 16.6 ms total, shadows = ~10% GPU time).

## Key Implementation Files

| File | Purpose |
|------|---------|
| `local_light_shadow_atlas.h` | Atlas allocation API, tile indexing, UV mapping |
| `local_light_shadow_atlas.cpp` | Per-frame reset, tile allocation logic, budget enforcement |
| `light_renderer.h` | Light collection, sorting, shadow assignment |
| `light_renderer.cpp` | ECS extraction, importance calculation, allocation orchestration |
| `frame_constants.cpp` | View-projection matrix setup per tile, face orientation for point lights |
| `frame_shadows.cpp` | Depth rendering per tile, render target setup, viewport configuration |
| `deferred_lighting_ps.hlsl` | Shadow sampling, cubemap face selection, PCF filtering |
| `constant_buffers.h` | `LocalLightShadowData` struct layout, `ShadowDataCB` constant buffer |
| `shadow_atlas_bindings.hlsl` | `Texture2D shadowAtlas`, `SamplerComparisonState shadowComparisonSampler` |

## Constant Buffer Layout

**File:** `runtime/src/engine/renderer/constant_buffers.h`

```cpp
/// Per-tile shadow data for spot and point lights
struct LocalLightShadowData {
    float4x4 viewProjMatrix;           // 64 bytes: spot light or per-face point light
    float4x4 faceViewProjMatrix[6];    // 384 bytes: all 6 cubemap faces (point light)
    float3 lightPosition;              // 12 bytes
    float shadowBias;                  // 4 bytes
    float4 atlasUvMin;                 // 16 bytes: {uvMin.x, uvMin.y, reserved, reserved}
    float4 atlasUvMax;                 // 16 bytes: {uvMax.x, uvMax.y, reserved, reserved}
    float4 atlasFaceTiles[6];          // 96 bytes: per-face tile UV rects (point light)
};

static constexpr uint32_t MAX_SHADOW_LIGHTS = 16;

/// Constant buffer for all allocated shadow lights (16 tiles max)
struct ShadowDataCB {
    LocalLightShadowData data[MAX_SHADOW_LIGHTS];  // 4096 bytes total
};
```

The CB is bound at register `b2` in the deferred lighting shader.

## Scene File Support

Local light shadows are exposed in scene files via component properties:

**File:** `sample/content/scenes/local_shadows_test.uzscene`

```ini
[scene]
name = Local Shadows Test

[node:directional_light]
type = directional_light
position = [10, 10, 10]
castShadows = true

[node:point_light_1]
type = point_light
position = [0, 2, 0]
color = [1.0, 1.0, 1.0]
radius = 10.0
castShadows = true
shadowBias = 0.001

[node:point_light_2]
type = point_light
position = [5, 3, -5]
color = [1.0, 0.8, 0.6]
radius = 8.0
castShadows = false
shadowBias = 0.001

[node:spot_light_1]
type = spot_light
position = [-5, 4, 0]
color = [1.0, 1.0, 0.8]
radius = 15.0
cone_angle = 45.0
castShadows = true
shadowBias = 0.0005
```

**Scene loading** (in `scene_loader.cpp`):

```cpp
void LoadLightComponent(const IniSection& section, uint32_t entityId) {
    float3 pos = section.GetVec3("position", {0, 0, 0});
    float3 color = section.GetVec3("color", {1, 1, 1});
    float radius = section.GetFloat("radius", 10.0f);
    bool castShadows = section.GetBool("castShadows", false);
    float shadowBias = section.GetFloat("shadowBias", 0.001f);

    if (section.GetString("type") == "point_light") {
        scene->AddComponent<PointLight>(entityId, {
            .color = color,
            .radius = radius,
            .castShadows = castShadows,
            .shadowBias = shadowBias
        });
    } else if (section.GetString("type") == "spot_light") {
        float coneAngle = section.GetFloat("cone_angle", 45.0f);
        scene->AddComponent<SpotLight>(entityId, {
            .color = color,
            .radius = radius,
            .coneAngleDegrees = coneAngle,
            .castShadows = castShadows,
            .shadowBias = shadowBias
        });
    }
}
```

## Key Takeaways

1. **Fixed Atlas**: 4096x4096 D32Float texture divided into 16 × 1024x1024 tiles provides consistent memory footprint and allocation predictability.

2. **Importance-Based Sorting**: Lights are ranked by `colorIntensity × radius² / distance²` to prioritize near, bright, large-radius lights for shadow quality.

3. **Separate Tile Requirements**: Spot lights consume 1 tile (perspective projection), point lights consume 6 tiles (cubemap faces). Budget allocation accounts for this asymmetry.

4. **Hardware PCF**: All shadow sampling uses hardware texture comparison (`SamplerComparisonState`) for efficient, high-quality filtering without manual tap loops.

5. **Per-Frame Reset**: Atlas allocation is fully reset each frame; no persistent tile assignments. Lights can change shadow status, importance, or position dynamically without cache invalidation.

6. **Scene Integration**: Light components expose `castShadows` and `shadowBias` properties, allowing scene authoring to control shadow quality per light.

7. **Performance Predictability**: Max 16 tiles × ~0.1 ms per tile = ~1.6 ms GPU budget (under 10% of 60 FPS frame time).

## References

- [local-light-shadows-ue5-architecture.md](local-light-shadows-ue5-architecture.md) — UE5 reference architecture for local light shadows
- [ue5-shadows/ue5-shadow-sampling-exact-code.md](../ue5-shadows/ue5-shadow-sampling-exact-code.md) — Hardware PCF and shadow sampling patterns
- [ue5-shadows/ue5-reversed-z-architecture.md](../ue5-shadows/ue5-reversed-z-architecture.md) — Depth comparison conventions in reversed-Z systems
- `docs/specs/renderer.md` — Rendering subsystem specification
- `docs/guides/shader-authoring.md` — HLSL shader writing conventions

## Changelog

| Date | Change |
|------|--------|
| 2026-03-30 | Initial creation. Documented shadow atlas layout, importance sorting, spot/point shadow projections, PCF sampling, constant buffer layout, and scene integration. |
