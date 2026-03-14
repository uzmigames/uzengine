# Cascaded Shadow Map System

> **Status**: Done (Phase 2)
> **Source**: `engine/renderer/shadows/`, `engine/renderer/passes/shadow_pass.h/.cpp`, `shaders/runtime/shadow_depth_vs.hlsl`
> **Implemented**: Cascade frustum splitting, shadow depth pass, PCF filtering, cascade blending
> **Tests**: 35 unit tests in `tests/engine/renderer/test_shadow_cascade.cpp`

## Purpose

The Cascaded Shadow Map (CSM) system provides realistic shadow casting from directional lights (sun, moon). It splits the camera's view frustum into 4 cascades, rendering separate shadow maps at different scales to maintain detail near the camera while covering the full scene depth range.

## Architecture Overview

CSM solves the fundamental problem of shadow map resolution: a single shadow map cannot achieve detail near the camera **and** coverage at the far distance simultaneously. The solution is to split the frustum and use tighter projections for nearby geometry.

```
Cascade 0 (near)   ← High-res shadow map, tight projection, fine detail
Cascade 1         ← Medium-res, wider projection
Cascade 2         ← Lower-res, even wider
Cascade 3 (far)    ← Coarse shadows covering far distance

Each cascade renders independently into a 2x2 atlas.
At sample time, the pixel shader selects cascade based on view-space depth.
Blend zone at cascade boundaries smooths transitions.
```

## Cascade Frustum Splitting

### Practical Split Scheme (GPU Gems 3, Ch. 10)

The split distances use the **practical split scheme**, which blends logarithmic and linear splitting:

```
Logarithmic: near * (far / near)^(i/N)
Linear:      near + (far - near) * (i/N)

Blend:       splits[i] = lambda * log + (1 - lambda) * linear
```

**Configuration:**
```cpp
struct ShadowConfig {
    uint32_t cascadeCount = 4;       // 1 to 4 cascades (hard limit kMaxShadowCascades)
    uint32_t resolution = 2048;      // Per-cascade resolution (e.g., 1024, 2048, 4096)
    float maxDistance = 100.0f;      // Clamp shadow rendering beyond this distance
    float splitLambda = 0.75f;       // 0=linear (uniform), 1=log (detail-biased)
    float normalBias = 0.02f;        // Normal bias (world units, per cascade)
    float depthBias = 0.005f;        // Depth bias for shadow comparison
};
```

**Why this scheme?**
- **Logarithmic splits** (lambda=1) concentrate cascades near the camera, preserving detail.
- **Linear splits** (lambda=0) distribute cascades uniformly, covering far distance.
- **Blend** (lambda=0.75, recommended) balances near-detail and far-coverage.

The `splitLambda` parameter is directly configurable, allowing artists to tune the cascade distribution.

### Texel Snapping

To prevent shadow "swimming" (shadow edges flickering as the camera moves), each cascade's orthographic projection is **snapped to texel boundaries**:

1. Compute world-space texel size: `texelSize = (2 * radius) / resolution`
2. Transform world origin to shadow-map space.
3. Round to nearest texel boundary.
4. Apply rounding offset back to the projection matrix.

This ensures sub-pixel camera movement never causes shadow patterns to shift.

### Split Distances in Shaders

The pixel shader needs to know which cascade to sample. Each cascade stores view-space split distances:

```cpp
struct ShadowCascade {
    Mat4   viewProj;      // Light-space view-projection (for PCF sampling)
    float  splitNear;     // View-space distance to near split
    float  splitFar;      // View-space distance to far split
    float  texelSize;     // World-space texel size (for bias scaling)
};
```

The shader compares pixel view-space depth (`pixelDepth = dot(pixelPos, cameraDir)`) against split distances to determine cascade index:

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

## Constant Buffers

### ShadowDataCB (Root CBV at b4)

Bound once per frame in the pixel shader to provide cascade data:

```cpp
struct ShadowDataCB {
    Mat4     cascadeViewProj[4];  // 256 bytes — light-space viewProj per cascade
    Vec4     cascadeSplits;        // 16 bytes — view-space far distances per cascade
    uint32_t shadowAtlasIndex;     // 4 bytes — SRV heap index (bindless)
    uint32_t cascadeCount;         // 4 bytes — active cascades (1–4)
    float    normalBias;           // 4 bytes — normal bias (world units)
    float    depthBias;            // 4 bytes — depth bias (for comparison)
    float    shadowMapSize;        // 4 bytes — cascade resolution (e.g., 2048)
    float    _pad[3];              // 12 bytes — alignment
    // Padded to 512 bytes (256-byte multiple for constant buffer alignment)
};

static_assert(sizeof(ShadowDataCB) == 512);
```

### PerObjectCB (Root CBV at b2, shadow pass)

In the shadow depth pass, each draw allocates a PerObjectCB:

```cpp
struct PerObjectCB {
    Mat4 worldMatrix;  // 64 bytes
    // ... (may include other per-object data)
};
```

### ShadowCB (Implicit)

The shadow depth pass binds `cascadeViewProj[cascadeIndex]` at root param 3 during the pass execution callback. This is logically a per-cascade, per-frame update (not per-object).

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

## Cascade Blending

Cascades are blended in the **last 10% of each cascade's depth range** to hide visible seams:

1. If pixel is in transition zone (last 10% of cascade i's depth range):
   - Sample both cascade i and cascade i+1.
   - Interpolate based on depth within the transition zone.
   - Blend the two shadow samples smoothly.

2. If outside transition zone:
   - Sample only the current cascade.

This produces a smooth cascade transition without visible banding.

## Configuration & Usage

### Setting Up CSM

```cpp
// In renderer initialization:
ShadowConfig shadowConfig;
shadowConfig.cascadeCount = 4;
shadowConfig.resolution = 2048;
shadowConfig.maxDistance = 100.0f;
shadowConfig.splitLambda = 0.75f;  // Tune near vs. far detail
shadowConfig.normalBias = 0.02f;
shadowConfig.depthBias = 0.005f;

// Compute cascade matrices each frame:
ShadowCascade cascades[4];
ComputeCascades(cameraView, cameraProj, sunDirection, shadowConfig, cascades);

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
for (uint32_t i = 0; i < 4; i++) {
    shadowDataCB.cascadeViewProj[i] = cascades[i].viewProj;
    shadowDataCB.cascadeSplits[i] = cascades[i].splitFar;
}
shadowDataCB.cascadeCount = 4;
shadowDataCB.shadowAtlasIndex = shadowAtlasSrvIndex;
shadowDataCB.normalBias = shadowConfig.normalBias;
shadowDataCB.depthBias = shadowConfig.depthBias;
shadowDataCB.shadowMapSize = 2048.0f;
```

### Tuning Parameters

| Parameter | Default | Effect |
|-----------|---------|--------|
| `cascadeCount` | 4 | More cascades = higher quality but more cost. 2–4 typical. |
| `resolution` | 2048 | Higher = sharper shadows, more memory. 1024–4096 typical. |
| `maxDistance` | 100 | Clamps shadow rendering. Set based on level size. |
| `splitLambda` | 0.75 | 0 = uniform coverage, 1 = detail-biased. 0.5–0.9 typical. |
| `normalBias` | 0.02 | Increase if shadows creep on slanted surfaces. |
| `depthBias` | 0.005 | Increase if shadow acne (sparkles) appears. |

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

## Rules & Best Practices

1. **ALWAYS recompute cascades every frame** — camera movement changes frustum splits.
2. **ALWAYS clamp `maxDistance`** — prevents wasted shadow coverage beyond visible distance.
3. **NEVER use cascadeCount > 4** — root cause: `kMaxShadowCascades = 4` hard limit.
4. **ALWAYS validate cascade matrices** — degenerate projections (e.g., zero radius) cause render failures.
5. **NEVER sample outside the atlas tile** — ensure UV clamping or border color handling.
6. **ALWAYS use per-cascade bias** — single bias doesn't work across resolution changes.
7. **PREFER split lambda = 0.75** — proven to balance detail and coverage.

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

## Implementation Notes

### Frustum Corner Computation

Corners are computed by un-projecting NDC corners to world space:

1. Build NDC corners: `{-1, -1, 0}` to `{1, 1, 1}` (DX clip space).
2. Multiply by `inverse(viewProj)` to recover world positions.
3. Interpolate along near-to-far rays for the sub-frustum `[splitNear, splitFar]`.

This approach is robust and avoids angle/distance calculations.

### Bounding Sphere vs. AABB

CSM uses a **bounding sphere** rather than a tight AABB because:
- Spheres are rotationally stable (no jitter when camera rotates).
- Sphere radius is constant regardless of light angle.
- Ensures cascade remain valid across all rotations.

### Orthographic Projection Snapping

The snapping offset is applied directly to the projection matrix's translation row, avoiding a separate matrix multiplication per frame.

## References

1. **"Cascaded Shadow Maps"** (Nvidia GPU Gems 3, Ch. 10) — foundational algorithm
2. **"Practical Cascaded Splitting"** — practical split scheme derivation
3. **DirectX 12 Documentation** — orthographic projection, depth comparison samplers
4. **UE5 Shadow Maps** (`W:\UE_5.6_Source`) — reference implementation patterns
5. **Godot ShadowAtlas** (`E:\godot`) — alternative atlas layout approaches

## File Manifest

```
engine/renderer/shadows/
├── cascade.h              # ComputeCascades, ShadowConfig, ShadowCascade types
├── cascade.cpp            # Practical split scheme implementation

engine/renderer/passes/
├── shadow_pass.h          # ShadowPassDesc, AddShadowPass
├── shadow_pass.cpp        # Shadow depth pass execution

shaders/runtime/
├── shadow_depth_vs.hlsl   # Depth-only vertex shader (bindless + SM5)

engine/renderer/
├── constant_buffers.h     # ShadowDataCB definition (512 bytes)

tests/engine/renderer/
├── test_shadow_cascade.cpp # 35 unit tests — split distances, matrices, bias
```
