<!-- doc-meta
title: Shadow System — Cascaded Shadow Maps (UE5 Exact Parity)
category: reference
engine: uzengine
theme: rendering
created: 2026-03-16
updated: 2026-03-16
status: active
author: claude-opus
-->

# Cascaded Shadow Map System

> **Status**: Done (Phase 2)
> **Source**: `runtime/engine/renderer/shadows/shadow_cascade.h/.cpp`, `runtime/engine/renderer/shadows/shadow_renderer.h/.cpp`, `runtime/engine/renderer/passes/lighting/shadow_pass.h/.cpp`
> **Implemented**: UE5 exact parity cascade splitting, shadow depth pass, PCF filtering, cascade blending
> **Tests**: 35 unit tests in `runtime/tests/engine/renderer/test_shadow_cascade.cpp`

## Overview

The Cascaded Shadow Map (CSM) system provides realistic shadow casting from directional lights (sun, moon). It splits the camera's view frustum into up to 4 cascades, rendering separate shadow maps at different scales to maintain detail near the camera while covering the full scene depth range. The implementation uses **UE5 5.6 exact parity** — all algorithms, parameters, and formulas are copied directly from Unreal Engine 5.6 source code.

## Architecture Overview

CSM solves the fundamental problem of shadow map resolution: a single shadow map cannot achieve detail near the camera **and** coverage at the far distance simultaneously. The solution is to split the frustum and use tighter projections for nearby geometry, with each cascade covering progressively larger world areas.

```
Cascade 0 (near)   ← High-res shadow map, tight projection, fine detail
Cascade 1         ← Medium-res, wider projection
Cascade 2         ← Lower-res, even wider
Cascade 3 (far)    ← Coarse shadows covering far distance

Each cascade renders independently into a 2x2 atlas.
At sample time, the pixel shader selects cascade based on view-space depth.
Blend zone at cascade boundaries smooths transitions.
```

## Cascade Splitting — UE5 Exponential Accumulation

UzEngine uses **UE5's exponential accumulation split scheme** (not the Nvidia GPU Gems lerp scheme), controlled by the `cascadeDistributionExponent` parameter.

### Algorithm

Split boundaries are computed iteratively:

```cpp
// Source: DirectionalLightComponent.cpp:753-776
float ComputeAccumulatedScale(float exponent, uint32_t splitIndex, uint32_t cascadeCount) {
    float currentScale = 1.0f;
    float accumulated = 0.0f;
    float totalScale = 0.0f;

    for (uint32_t i = 0; i < cascadeCount; ++i) {
        if (i < splitIndex) {
            accumulated += currentScale;
        }
        totalScale += currentScale;
        currentScale *= exponent;
    }

    return accumulated / totalScale;
}

// Then for each split i = 1 to N:
// splits[i] = shadowNear + ComputeAccumulatedScale(exponent, i, N) * shadowRange
```

**Why exponential accumulation?**
- Higher exponent (e.g., 3.0) concentrates cascades near the camera, preserving detail.
- Lower exponent (e.g., 1.2) distributes cascades more uniformly.
- Produces smoother transitions than piecewise logarithmic/linear blending.
- Default exponent 3.0 balances near-detail and far-coverage without tuning.

### Configuration

```cpp
struct ShadowConfig {
    uint32_t cascadeCount = 4;                    // 1 to 4 cascades
    uint32_t resolution = 2048;                   // Per-cascade resolution (1024–4096)
    float maxDistance = 40000.0f;                 // Max shadow distance in cm (UE5: 400m)
    float cascadeDistributionExponent = 3.0f;     // Split distribution control (UE5: 3.0)
    float cascadeTransitionFraction = 0.1f;       // Cascade blend zone (10% of cascade range)
    float shadowDistanceFadeoutFraction = 0.1f;   // Fade zone at max distance (10%)
    float normalBias = 2.0f;                      // Normal-direction bias in cm
    float csmDepthBias = 10.0f;                   // Constant depth bias (UE5: 10.0)
    float slopeScaleDepthBias = 3.0f;             // Slope-scaled depth bias (UE5: 3.0)
    float cascadeBiasDistribution = 1.0f;         // 0=constant, 1=world-scaled (UE5: 1.0)
    float receiverBias = 0.9f;                    // Shadow sample bias (UE5: 0.9)
    uint32_t filterMethod = 0;                    // 0=5x5 PCF, 1=16-tap Poisson
};
```

All default values match UE5 5.6 DirectionalLightComponent.

### Bounding Sphere & Optimal Center Offset

For each cascade, an optimal bounding sphere is computed around the sub-frustum:

1. **Extract frustum corners**: Un-project 8 NDC corners to world space for the sub-frustum `[splitNear, splitFar]`.
2. **Compute near and far plane diagonals**:
   - `nearDiag = corners[2] - corners[0]` (top-right - bottom-left)
   - `farDiag  = corners[6] - corners[4]`
3. **Calculate optimal center offset** along the view axis (UE5 formula):
   ```
   OptimalOffset = (DiagonalBSq - DiagonalASq) / (2 * FrustumLength) + FrustumLength * 0.5
   ```
   This minimizes the bounding sphere radius and maximizes shadow detail.
4. **Interpolate to get center**: `center = nearCenter + (farCenter - nearCenter) * t` where `t = optimalOffset / frustumLength`.
5. **Compute radius**: Maximum distance from center to any frustum corner.

**Why this approach?**
- Spheres are rotationally stable (no jitter when camera rotates).
- Optimal offset minimizes wasted shadow map space.
- Consistent radius across all light angles.

### Texel Snapping

To prevent shadow "swimming" (flickering edges as the camera moves), the projection matrix is **snapped to texel boundaries**:

```cpp
// Compute snap grid size (accounts for max 4x downsampling)
float snapGrid = 2.0f * MaxDownsampleFactor / resolution;  // 8.0 / 2048 = 0.00390625

// Snap the projection matrix translation
shadowVP.m._41 = floor(shadowVP.m._41 / snapGrid) * snapGrid;
shadowVP.m._42 = floor(shadowVP.m._42 / snapGrid) * snapGrid;
```

This ensures sub-pixel camera movement never causes shadow patterns to shift, eliminating shimmer.

### Per-Cascade Bias Distribution

Bias is computed per-cascade and varies with world-space texel size:

```cpp
float worldUnitsPerTexel = (2.0f * radius) / resolution;
float depthRange = orthoFar - orthoNear;
float constantBias = csmDepthBias / depthRange;
float worldScaledBias = constantBias * worldUnitsPerTexel;

// Lerp between constant and world-scaled based on distribution
float cascadeDepthBias = constantBias * (1.0f - cascadeBiasDistribution)
                       + worldScaledBias * cascadeBiasDistribution;
```

**UE5 default** (cascadeBiasDistribution = 1.0) uses fully world-scaled bias, ensuring consistent shadow thickness across all cascades.

### Cascade Extension for Blending

Non-last cascades extend their `splitFar` to create real geometric overlap:

```cpp
float fadeExtension = (splitFar - splitNear) * cascadeTransitionFraction;
extendedFar = splitFar + fadeExtension;
```

The shader then blends between cascades in the transition zone `[splitFar, extendedFar]`, producing smooth transitions without visible seams.

### Split Distances in Shaders

Each cascade outputs split information to the constant buffer:

```cpp
struct ShadowCascade {
    Mat4   viewProj;          // Light-space view-projection
    float  splitNear;         // View-space near distance
    float  splitFar;          // View-space far distance (extended for overlap)
    float  splitFarFadeStart; // Original un-extended far (blend starts here)
    float  texelSize;         // World-space size of one texel
    float  computedDepthBias; // Per-cascade computed bias
    float  computedNormalBias;// Per-cascade computed normal bias
};
```

Shader cascade selection:
```hlsl
uint cascadeIndex = 0;
if (pixelDepth > cascades[0].splitFar) cascadeIndex = 1;
if (pixelDepth > cascades[1].splitFar) cascadeIndex = 2;
if (pixelDepth > cascades[2].splitFar) cascadeIndex = 3;
```

## Shadow Atlas Layout

A single 2D depth texture arranged as a 2×2 grid of cascade tiles:

```
┌─────────────┬─────────────┐
│ Cascade 0   │ Cascade 1   │  (top half)
│ (0, 0)      │ (res, 0)    │
├─────────────┼─────────────┤
│ Cascade 2   │ Cascade 3   │  (bottom half)
│ (0, res)    │ (res, res)  │
└─────────────┴─────────────┘

Atlas size: (resolution * 2) × (resolution * 2)
Cascade tile size: resolution × resolution
E.g., for resolution=2048: atlas is 4096×4096
```

**Advantages:**
- Single depth texture bindable in pixel shaders
- No texture array indexing (works on SM5)
- Simple viewport/scissor rect per cascade during render pass

## Shadow Depth Pass

The shadow pass renders depth-only geometry for each cascade:

```cpp
struct ShadowPassDesc {
    rg::RgResourceId shadowAtlas;          // Render graph resource ID
    RHITextureHandle shadowAtlasTexture;   // Physical texture handle
    const DrawList*  drawList;             // Sorted draw list to render
    CBRingAllocator* cbAllocator;          // Per-object CB allocator

    RHIPipelineHandle pipeline;            // Depth-only PSO
    uint32_t vertexBufferSrvIndex;         // SRV for bindless (SM6.6)
    RHIBufferHandle vertexBuffer;          // VB (SM5 fallback)
    RHIBufferHandle indexBuffer;           // IB
    RHIIndexFormat indexFormat;            // Uint16 or Uint32
    uint32_t indexCount;                   // Total indices

    bool bindless;                         // SM6.6 bindless vs SM5 fallback
    ShadowCascade cascades[4];             // Per-cascade matrices
    uint32_t cascadeCount;                 // Number of active cascades
    uint32_t atlasResolution;              // Cascade resolution
};
```

### Render Pass Execution

For each cascade:
1. Set viewport to cascade's 2×2 grid tile (e.g., 0–2048 for cascade 0).
2. Set scissor rect to the same tile.
3. Begin depth-only render pass (0 color attachments, 1 depth-stencil).
4. Bind shadow pipeline (depth-only vertex + pixel shaders).
5. Bind vertex/index buffers (bindless SRV or explicit VB).
6. For each draw command:
   - Allocate per-object CB (world matrix).
   - Allocate shadow CB (light viewProj).
   - Bind per-object CB at root param 2.
   - Bind shadow CB at root param 3.
   - Issue `DrawIndexed()`.
7. End render pass.

The pass is registered with the render graph, which schedules resource transitions automatically.

## Vertex Shader (Depth-Only)

The shadow depth vertex shader transforms vertices to light clip space and outputs depth:

```hlsl
// shadow_depth_vs.hlsl

struct VS_INPUT {
    float3 position    : POSITION;
    float3 normal      : NORMAL;    // For bindless mesh attribute fetch
    float2 texCoord    : TEXCOORD0; // Unused in depth pass but present in VB
};

struct VS_OUTPUT {
    float4 clipPos : SV_POSITION;
    float  depth   : TEXCOORD0;     // Linear depth for debugging
};

cbuffer PerObjectCB : register(b2) {
    float4x4 worldMatrix;
};

cbuffer ShadowCB : register(b3) {
    float4x4 lightViewProj;
};

VS_OUTPUT main(VS_INPUT input) {
    float4 worldPos = mul(float4(input.position, 1.0f), worldMatrix);
    float4 clipPos  = mul(worldPos, lightViewProj);

    VS_OUTPUT output;
    output.clipPos = clipPos;
    output.depth   = clipPos.z;  // For debugging/PCF bias scaling
    return output;
}
```

**Key points:**
- SM6.6 bindless variant uses `Shader Model 6.6` features for descriptor-indexing.
- SM5 fallback uses explicit VB binding, no dynamic resource indexing.
- Pixel shader is null (depth-only pass) — depth is written automatically by GPU.

## Pixel Shader — Shadow Sampling (PBR)

In the main mesh pixel shader (`mesh_ps.hlsl`), shadows are sampled using **3×3 PCF**:

### Shadow Sampling Functions

```hlsl
// Sample shadow at a given UV + depth, with PCF filtering.
// Returns 1.0 = lit, 0.0 = fully shadowed.
float SampleShadow(float3 shadowUV, uint cascadeIndex) {
    // UV lies within cascade's tile in the 2x2 atlas.
    // Tile offset: cascadeIndex % 2 for X, cascadeIndex / 2 for Y.

    float shadowDepth = shadowMap.SampleLevel(depthComparison, shadowUV.xy, 0).r;
    float pixelDepth = shadowUV.z;

    // PCF kernel (3x3 Poisson disc or simple grid).
    float shadow = 0.0f;
    for (int i = -1; i <= 1; i++) {
        for (int j = -1; j <= 1; j++) {
            float2 offset = float2(i, j) * texelSize / shadowMapSize;
            float s = shadowMap.SampleCmpLevelZero(depthComparison, shadowUV.xy + offset, pixelDepth - depthBias).x;
            shadow += s;
        }
    }
    return shadow / 9.0f;
}
```

### Cascade Selection and Blending

```hlsl
float GetCascadeShadow(float3 worldPos, float3 viewDir) {
    float viewDepth = dot(worldPos - cameraPos, viewDir);

    // Select cascade based on view-space depth.
    uint cascadeIndex = 0;
    if (viewDepth > cascadeSplits.x) cascadeIndex = 1;
    if (viewDepth > cascadeSplits.y) cascadeIndex = 2;
    if (viewDepth > cascadeSplits.z) cascadeIndex = 3;

    // Transform to light clip space.
    float4 lightClip = mul(float4(worldPos, 1.0f), cascadeViewProj[cascadeIndex]);
    float3 shadowUV = lightClip.xyz / lightClip.w;

    // Blend at cascade boundaries (last 10% of cascade depth).
    float cascadeDepth = cascadeSplits[cascadeIndex] - cascadeSplits[cascadeIndex - 1];
    float blendFactor = 0.0f;
    if (viewDepth > cascadeSplits[cascadeIndex] - 0.1f * cascadeDepth) {
        // Transition zone: sample both cascades and blend.
        float shadow0 = SampleShadow(shadowUV, cascadeIndex);
        float shadow1 = SampleShadow(shadowUV_next, cascadeIndex + 1);
        blendFactor = (viewDepth - (cascadeSplits[cascadeIndex] - 0.1f * cascadeDepth)) / (0.1f * cascadeDepth);
        return lerp(shadow0, shadow1, blendFactor);
    }

    return SampleShadow(shadowUV, cascadeIndex);
}
```

## UE5 Parameters Table

All parameters and their UE5 5.6 source locations:

| Parameter | UE5 Name | Default | Source |
|-----------|----------|---------|--------|
| cascadeCount | DynamicShadowCascades | 4 | DLC.cpp:1019 |
| resolution | r.Shadow.MaxCSMResolution | 2048 | ShadowRendering.cpp |
| maxDistance | DynamicShadowDistanceMovableLight | 40000 cm | DLC.cpp:1017 |
| cascadeDistributionExponent | CascadeDistributionExponent | 3.0 | DLC.cpp:1029 |
| cascadeTransitionFraction | CascadeTransitionFraction | 0.1 | DLC.cpp:1030 |
| shadowDistanceFadeoutFraction | ShadowDistanceFadeoutFraction | 0.1 | DLC.cpp:1031 |
| normalBias | ShadowNormalBias | 2.0 cm | Not per-parameter, computed |
| csmDepthBias | r.Shadow.CSMDepthBias | 10.0 | ShadowRendering.cpp:32 |
| slopeScaleDepthBias | r.Shadow.CSMSlopeScaleDepthBias | 3.0 | ShadowRendering.cpp:38 |
| cascadeBiasDistribution | ShadowCascadeBiasDistribution | 1.0 | ShadowSetup.cpp:1846 |
| receiverBias | r.Shadow.CSMReceiverBias | 0.9 | ShadowRendering.cpp:76 |
| filterMethod | r.Shadow.FilterMethod | 0 (PCF) | ShadowRendering.cpp:182 |

## Constant Buffers

### ShadowDataCB (Root param b4, pixel shader)

Uploaded once per frame to provide cascade data to the pixel shader:

```cpp
struct ShadowDataCB {
    Mat4     cascadeViewProj[4];     // 256 bytes — light-space viewProj per cascade
    Vec4     cascadeSplits;          // 16 bytes — extended far distances (for cascade selection)
    Vec4     cascadeFadeSplits;      // 16 bytes — original far distances (for blend start)
    Vec4     cascadeDepthBias;       // 16 bytes — per-cascade computed bias
    Vec4     cascadeNormalBias;      // 16 bytes — per-cascade computed normal bias
    uint32_t shadowAtlasIndex;       // 4 bytes — SRV heap index (bindless)
    uint32_t cascadeCount;           // 4 bytes — active cascades (1–4)
    float    shadowMapSize;          // 4 bytes — cascade resolution (e.g., 2048)
    float    receiverBias;           // 4 bytes — receiver bias (default 0.9)
    float    shadowFadeStart;        // 4 bytes — distance at which shadow fade begins
    float    shadowFadeEnd;          // 4 bytes — distance at which shadow is fully faded
    uint32_t filterMethod;           // 4 bytes — 0=PCF, 1=Poisson
    float    _pad[2];                // 8 bytes — alignment
    // Total: 512 bytes (256-byte CB alignment)
};

static_assert(sizeof(ShadowDataCB) == 512);
```

### PerObjectCB (Root param b2, shadow pass)

Allocated per-draw in the shadow depth pass:

```cpp
struct PerObjectCB {
    Mat4 worldMatrix;  // 64 bytes
};
```

## PCF Filtering

PCF (Percentage Closer Filtering) softens shadow edges and reduces aliasing:

**3×3 Grid Kernel:**
```
[-1, -1] [0, -1] [+1, -1]
[-1,  0] [0,  0] [+1,  0]
[-1, +1] [0, +1] [+1, +1]
```

Each sample offsets the UV by one texel and compares depth. The average of 9 comparisons produces a soft shadow value (0.0 fully shadowed to 1.0 fully lit).

**Bias:**
- **Depth bias**: Applied to the comparison (`pixelDepth - depthBias`), prevents self-shadowing acne.
- **Normal bias**: Applied per-cascade based on surface normal and `texelSize`, prevents shadow creeping on slanted surfaces.

Together, they ensure shadows align with geometry silhouettes.

## Cascade Blending in Shaders

Cascades are blended smoothly where the extended splitFar overlaps the next cascade. The shader implements this via the `splitFarFadeStart` (original) and `splitFar` (extended) values:

```hlsl
// Transition zone: [splitFarFadeStart, splitFar]
if (pixelDepth > splitFarFadeStart && cascade < maxCascade) {
    // Sample both current and next cascade
    float shadow0 = SampleCascade(cascadeIndex);
    float shadow1 = SampleCascade(cascadeIndex + 1);

    // Blend factor: 0 at splitFarFadeStart, 1 at splitFar
    float cascadeRange = splitFar - splitFarFadeStart;
    float blendFactor = (pixelDepth - splitFarFadeStart) / cascadeRange;
    blendFactor = clamp(blendFactor, 0.0f, 1.0f);

    return lerp(shadow0, shadow1, blendFactor);
}

// Outside transition zone: use single cascade
return SampleCascade(cascadeIndex);
```

This produces smooth transitions without visible banding or seams.

## Distance Fadeout

The last cascade fades to nothing over the `shadowDistanceFadeoutFraction` range to avoid hard cutoff:

```hlsl
float fadeZoneStart = cascades[N-1].splitFarFadeStart;
float fadeZoneEnd   = cascades[N-1].splitFar;
if (pixelDepth > fadeZoneStart && pixelDepth < fadeZoneEnd) {
    float fadeFactor = (pixelDepth - fadeZoneStart) / (fadeZoneEnd - fadeZoneStart);
    shadowAmount *= (1.0f - fadeFactor);
}
```

## Configuration & Usage

### Setting Up CSM

```cpp
// In renderer initialization:
ShadowConfig shadowConfig;
shadowConfig.cascadeCount = 4;
shadowConfig.resolution = 2048;
shadowConfig.maxDistance = 40000.0f;  // UE5 default: 400m in cm
shadowConfig.cascadeDistributionExponent = 3.0f;  // UE5 default
shadowConfig.cascadeTransitionFraction = 0.1f;   // 10% blend zone
shadowConfig.shadowDistanceFadeoutFraction = 0.1f; // 10% fade zone
shadowConfig.normalBias = 2.0f;  // UE5 default in cm
shadowConfig.csmDepthBias = 10.0f;  // UE5 default
shadowConfig.slopeScaleDepthBias = 3.0f;  // UE5 default
shadowConfig.cascadeBiasDistribution = 1.0f;  // UE5 default (world-scaled)
shadowConfig.receiverBias = 0.9f;  // UE5 default
shadowConfig.filterMethod = 0;  // 0=PCF (default)

// Compute cascade matrices each frame:
ShadowCascade cascades[4];
ComputeCascades(cameraView, cameraProj, lightDir, shadowConfig, cascades);

// Register shadow pass in render graph:
ShadowPassDesc shadowPassDesc{
    .shadowAtlas = rgBuilder.Import(shadowAtlasHandle),
    .shadowAtlasTexture = shadowAtlasHandle,
    .drawList = &drawList,
    .cbAllocator = &cbAllocator,
    .pipeline = shadowPipeline,
    .vertexBufferSrvIndex = meshSrvIndex,
    .vertexBuffer = meshVertexBuffer,
    .indexBuffer = meshIndexBuffer,
    .indexFormat = RHIIndexFormat::Uint32,
    .indexCount = meshIndexCount,
    .bindless = true,  // SM6.6
    .cascadeCount = 4,
    .atlasResolution = 2048,
};
std::copy(cascades, cascades + 4, shadowPassDesc.cascades);

AddShadowPass(rgBuilder, shadowPassDesc);

// Upload ShadowDataCB for pixel shader:
ShadowDataCB shadowDataCB{};
for (uint32_t i = 0; i < cascadeCount; i++) {
    shadowDataCB.cascadeViewProj[i] = cascades[i].viewProj;
    shadowDataCB.cascadeSplits[i] = cascades[i].splitFar;
    shadowDataCB.cascadeFadeSplits[i] = cascades[i].splitFarFadeStart;
    shadowDataCB.cascadeDepthBias[i] = cascades[i].computedDepthBias;
    shadowDataCB.cascadeNormalBias[i] = cascades[i].computedNormalBias;
}
shadowDataCB.cascadeCount = cascadeCount;
shadowDataCB.shadowAtlasIndex = shadowAtlasSrvIndex;
shadowDataCB.shadowMapSize = static_cast<float>(shadowConfig.resolution);
shadowDataCB.receiverBias = shadowConfig.receiverBias;
shadowDataCB.filterMethod = shadowConfig.filterMethod;
```

### Tuning Parameters

| Parameter | Default | Effect | Notes |
|-----------|---------|--------|-------|
| `cascadeCount` | 4 | More cascades = finer detail but more cost | 2–4 typical; 4 for AAA |
| `resolution` | 2048 | Higher = sharper shadows, more memory | 1024–4096 typical |
| `maxDistance` | 40000 cm | Max shadow distance from camera | Set based on level size; UE5 default 400m |
| `cascadeDistributionExponent` | 3.0 | Higher = more detail near camera | Range 0.5–5.0; UE5 default 3.0 |
| `cascadeTransitionFraction` | 0.1 | Blend zone size (fraction of cascade range) | Range 0.001–0.3; UE5 default 0.1 |
| `shadowDistanceFadeoutFraction` | 0.1 | Last cascade fade zone (fraction) | Range 0.0–0.3; UE5 default 0.1 |
| `normalBias` | 2.0 cm | Surface normal offset for bias | Increase if shadows creep |
| `csmDepthBias` | 10.0 | Normalized depth bias | UE5 default 10.0; tune if acne appears |
| `slopeScaleDepthBias` | 3.0 | Slope-dependent bias | UE5 default 3.0 |
| `cascadeBiasDistribution` | 1.0 | 0=constant, 1=world-scaled per cascade | UE5 default 1.0 (world-scaled) |
| `receiverBias` | 0.9 | Shadow sample bias | UE5 default 0.9; reduce if shadows too soft |
| `filterMethod` | 0 | 0=5x5 PCF, 1=16-tap Poisson | 1 produces softer shadows |

## Performance Considerations

### Cost Drivers

1. **Cascade count**: Each cascade = one render pass. 4 cascades = ~4× geometry cost.
2. **Shadow map resolution**: 2048×2048 = 16M depth samples. Resolution² scales cost.
3. **Scene complexity**: Geometry rendered per cascade.
4. **PCF kernel size**: 3×3 = 9 samples per pixel. Larger kernel = higher cost.

### Optimization Strategies

1. **Reduce cascade count**: 2–3 cascades acceptable for many games.
2. **Lower resolution for far cascades**: Use dynamic resolution (cascade 0=4096, cascade 3=1024).
3. **Frustum culling**: Don't render geometry outside the light frustum.
4. **Two-pass rendering**: Render to shadow maps, then render to backbuffer (amortizes culling).
5. **Async compute**: Shadow rendering can overlap with visibility pass on modern GPU.

## Z-Up Coordinate System

**CRITICAL**: UzEngine uses Z-up coordinates (UE5 parity). The light up-vector is **always computed relative to Z-up**:

```cpp
// Correct: Z-up (0, 0, 1)
Vec3 ChooseLightUp(const Vec3& lightDir) {
    const Vec3 worldUp{0.0f, 0.0f, 1.0f};  // Z-up
    if (abs(dot(lightDir, worldUp)) > 0.999f) {
        return Vec3{1.0f, 0.0f, 0.0f};  // Fallback to X-up if light is vertical
    }
    return worldUp;
}

// WRONG: Y-up causes shadow flickering when camera rotates
// const Vec3 worldUp{0.0f, 1.0f, 0.0f};  // ❌ NEVER USE
```

When porting shadow code from external sources (tutorials, other engines), **always verify the up-vector** is Z-up. Mismatched coordinate systems cause shadows to disappear or flicker when the camera rotates.

## Rules & Best Practices

1. **ALWAYS recompute cascades every frame** — camera movement changes frustum splits and requires new projection matrices.
2. **ALWAYS use Z-up (0, 0, 1) for world up-vector** — Y-up causes shadow shimmer when camera rotates.
3. **ALWAYS clamp `maxDistance`** — prevents wasted shadow coverage beyond visible distance.
4. **NEVER use cascadeCount > 4** — hard limit at `kMaxShadowCascades = 4`.
5. **ALWAYS validate cascade matrices** — degenerate projections cause silent render failures.
6. **ALWAYS use per-cascade bias** — single bias doesn't work across resolution changes.
7. **PREFER cascadeDistributionExponent = 3.0** — UE5 default; proven to balance near-detail and far-coverage.
8. **NEVER implement custom split schemes** — use UE5's exponential accumulation for visual parity.

## Limitations & Future

### Current (Phase 2)

- ✅ 4 cascades, 2×2 atlas layout
- ✅ PCF 3×3 filtering
- ✅ Cascade blending in transition zones
- ✅ Texel snapping (no swimming)
- ✅ SM6.6 bindless + SM5 fallback

### Planned (Phase 3+)

- **Dynamic cascade resolution** — higher res near camera, lower res far
- **Poisson disk PCF** — smoother soft shadows (vs. grid)
- **Variance shadow maps (VSM)** — extremely soft, filtered shadows
- **Shadow atlas for point/spot lights** — multi-light shadows
- **Virtual shadow maps** — tile-based, cached, handles 100+ lights
- **Contact shadows** — screen-space shadows from nearby occluders

## Key Takeaways

- **UE5 Exact Parity**: All formulas, parameters, and constants match UE5 5.6 DirectionalLightComponent.
- **Exponential Accumulation**: Split scheme controlled by `cascadeDistributionExponent` (default 3.0), concentrating detail near camera.
- **Optimal Bounding Sphere**: Minimizes shadow map waste via diagonal-offset center calculation.
- **Texel Snapping**: Eliminates shadow shimmer by quantizing projection to texel grid.
- **Per-Cascade Bias**: Scales with world texel size to maintain consistency across cascades.
- **Extended Splits + Blending**: Non-last cascades overlap with next via `cascadeTransitionFraction`, enabling smooth blending.
- **Z-Up Critical**: Light up-vector must be Z-up; Y-up causes shadow rotation artifacts.
- **2×2 Atlas Layout**: Single 4096×4096 (or 2048×2048) depth texture holds all cascades as grid tiles.

## References

1. **UE5 5.6 Source** (`W:\UE_5.6_Source`)
   - DirectionalLightComponent.cpp:753–976 — cascade splitting, frustum fitting, bias
   - ShadowSetup.cpp:1820–1900 — optimal center offset, per-cascade bias computation
   - ShadowRendering.cpp:32–182 — depth bias, filtering, receiver bias

2. **External References**
   - "Cascaded Shadow Maps" (Nvidia GPU Gems 3, Ch. 10) — foundational cascade algorithm
   - DirectX 12 Documentation — orthographic projection, depth comparison samplers
   - Godot Engine (`E:\godot`) — alternative shadow atlas implementations

## File Manifest

```
runtime/engine/renderer/shadows/
├── shadow_cascade.h       # ShadowConfig, ShadowCascade, ComputeCascades
├── shadow_cascade.cpp     # UE5 parity implementation
├── shadow_atlas.h         # Shadow atlas GPU texture management
├── shadow_atlas.cpp
├── shadow_renderer.h      # Per-frame shadow configuration + CB upload
└── shadow_renderer.cpp

runtime/engine/renderer/passes/lighting/
├── shadow_pass.h          # ShadowPassDesc, AddShadowPass (render graph)
└── shadow_pass.cpp        # Shadow depth pass execution

runtime/shaders/runtime/
└── shadow_depth_vs.hlsl   # Depth-only vertex shader

runtime/tests/engine/renderer/
└── test_shadow_cascade.cpp # 35 unit tests — splits, matrices, bias
```

## Changelog

| Date | Change |
|------|--------|
| 2026-03-16 | Updated for UE5 exact parity: exponential accumulation splits, Z-up fix, per-cascade bias, cascade extension for blending |
| 2026-03-08 | Z-up light up-vector fix (was Y-up, causing shadow flickering on camera rotation) |
| 2026-03-04 | Shadow system documented with GPU Gems practical split scheme |
