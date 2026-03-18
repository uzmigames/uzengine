# GBuffer Layout Guide

> **Status**: Done (Milestone 1.3.1)
> **Source**: `runtime/engine/renderer/core/gbuffer/gbuffer.h`, `runtime/engine/renderer/passes/gbuffer_pass.h`, `runtime/shaders/runtime/gbuffer_ps.hlsl`, `runtime/shaders/runtime/deferred_lighting_ps.hlsl`
> **Shader includes**: `runtime/shaders/runtime/include/octahedral.hlsli`

## Purpose

The **GBuffer** (geometry buffer) stores per-pixel material data needed for deferred lighting: albedo, normal, metallic, roughness, and ambient occlusion. This guide explains the current memory-optimized layout, channel packing strategy, and how to correctly encode/decode data in your shaders.

## Quick Reference

The optimized GBuffer layout uses **4 render targets total**:

| Target | Format | Width | Height | Content | Bandwidth |
|--------|--------|-------|--------|---------|-----------|
| Albedo | RGBA8Unorm | Full | Full | RGB base color + A AO | 4 B/px |
| Normal | RGBA8Unorm | Full | Full | RG octahedral normal + B metallic + A roughness | 4 B/px |
| Depth | D32Float | Full | Full | Hardware depth (32-bit) | 4 B/px |
| Velocity | RG16Float | Full | Full | Screen-space motion vectors (TAA/motion blur) | 4 B/px |
| **Total** | — | — | — | — | **16 B/px** (no temporal) or **20 B/px** (with velocity) |

## Layout Details

### Albedo Target (RGBA8Unorm)

```
┌─────────────────────────────────────────────────┐
│ RGBA8 Unorm per pixel                           │
├──────────────┬──────────────┬──────────────┬────┤
│ R (8-bit)    │ G (8-bit)    │ B (8-bit)    │ A  │
│ Red channel  │ Green channel│ Blue channel │    │
│ of linear    │ of linear    │ of linear    │ AO │
│ base color   │ base color   │ base color   │    │
└──────────────┴──────────────┴──────────────┴────┘

Content:
- RGB: Linear base color (sRGB input converted to linear in GBuffer PS)
- A: Ambient occlusion [0,1] from texture or default 1.0

Usage in Lighting PS:
  float3 albedo = gbufferAlbedo.Sample(sampler, uv).rgb;
  float  ao     = gbufferAlbedo.Sample(sampler, uv).a;
```

**Why RGBA8?**
- 8 bits per channel = 256 levels, sufficient for sRGB color quantization
- Alpha channel recycled for AO (eliminates separate RT)
- Stores in linear color space (conversion happens at input in GBuffer PS)

### Normal Target (RGBA8Unorm)

```
┌─────────────────────────────────────────────────┐
│ RGBA8 Unorm per pixel                           │
├──────────────┬──────────────┬──────────────┬────┤
│ R (8-bit)    │ G (8-bit)    │ B (8-bit)    │ A  │
│              │              │              │    │
│ Octahedral   │ Octahedral   │ Metallic     │ RMS│
│ Normal X     │ Normal Y     │ [0,1]        │    │
│              │              │              │ ness│
└──────────────┴──────────────┴──────────────┴────┘

Content:
- RG: Octahedral-encoded world-space normal (see section below)
- B: Metallic [0,1] — 0 = fully dielectric, 1 = fully conductive
- A: Roughness [0,1] — clamped to [0.04, 1.0] in lighting (MIN_ROUGHNESS)

Usage in Lighting PS:
  float2 octNormal = gbufferNormal.Sample(sampler, uv).rg;
  float3 N = OctDecode(octNormal);
  N = normalize(N);  // Ensure unit-length

  float metallic = gbufferNormal.Sample(sampler, uv).b;
  float roughness = max(gbufferNormal.Sample(sampler, uv).a, MIN_ROUGHNESS);
```

**Why octahedral normal packing?**
- **Saves 50% bandwidth**: RGBA16F (8 bytes) → RGBA8 (4 bytes)
- **<0.5° angular error**: Imperceptible to human eye
- **Evenly distributed**: Error uniform across normal hemisphere
- **Lookup-free**: No indirection tables; pure math

### Depth Target (D32Float)

```
┌─────────────────────────────────────────────────┐
│ D32 per pixel (hardware depth)                  │
├─────────────────────────────────────────────────┤
│ 32-bit float depth value                        │
│ 0.0 = far plane, 1.0 = near plane              │
│ (standard NDC depth, not reversed-Z yet)        │
└─────────────────────────────────────────────────┘

Usage in Lighting PS:
  float depth = gbufferDepth.Sample(pointSampler, uv).r;
  float3 worldPos = ReconstructWorldPosition(uv, depth);
  // Uses invViewProj matrix to go from (uv, depth) → world position

Usage in Velocity PS:
  float depth = depthBuffer.Sample(pointSampler, uv).r;
  // Reproject via invViewProj (current) and prevViewProj (previous frame)
  // to compute screen-space motion vectors
```

**Why D32?**
- Full 32-bit precision maintains depth accuracy at distance
- Sufficient for world position reconstruction without artifacts
- Used by both lighting and velocity generation

### Velocity Target (RG16Float)

```
┌─────────────────────────────────────────────────┐
│ RG16 Float per pixel                            │
├──────────────────────┬──────────────────────────┤
│ R (16-bit float)     │ G (16-bit float)         │
│ Screen-space X       │ Screen-space Y           │
│ motion (pixels)      │ motion (pixels)          │
└──────────────────────┴──────────────────────────┘

Content:
- R: X-direction motion in screen-space pixels (positive = right)
- G: Y-direction motion in screen-space pixels (positive = down)

Range: Typically [-screen_width/2, +screen_width/2] per axis

Usage in TAA:
  float2 velocity = velocityBuffer.Sample(sampler, uv).rg;
  float2 reprojectedUV = uv - velocity / screenDimensions;
  float3 history = historyBuffer.Sample(sampler, reprojectedUV).rgb;

Usage in Motion Blur:
  float2 velocity = velocityBuffer.Sample(sampler, uv).rg;
  float3 blurred = float3(0, 0, 0);
  for (int i = 0; i < MOTION_BLUR_SAMPLES; ++i) {
      float t = i / (MOTION_BLUR_SAMPLES - 1);
      float2 offsetUV = uv - (velocity / screenDimensions) * t;
      blurred += colorBuffer.Sample(sampler, offsetUV).rgb;
  }
  blurred /= MOTION_BLUR_SAMPLES;
```

**Why RG16Float?**
- 16-bit float precision: ±0.5 pixel accuracy for sub-pixel reprojection
- Only 2 channels needed; B/A unused
- Sky pixels (depth ≈ 0 or ≈ 1) output (0, 0) to prevent ghosting

## Octahedral Normal Encoding

### What is Octahedral Encoding?

Octahedral encoding packs a 3D unit normal into 2D space with minimal angular error. It works by:

1. **Projecting** the normal onto the L1 unit sphere (Manhattan distance)
2. **Wrapping** the lower hemisphere into the [−1,1]² octagon
3. **Remapping** the octagon to [0,1]² for UNORM storage

### Mathematical Details

**Encoding** (normalizing direction to 2D):
```hlsl
float2 OctEncode(float3 n)
{
    // 1. Normalize to L1 sphere: |x| + |y| + |z| = 1
    float3 p = n / (abs(n.x) + abs(n.y) + abs(n.z));

    // 2. Fold lower hemisphere outward (wrap)
    float2 wrap = (1.0 - abs(p.yx)) * float2(
        p.x >= 0.0 ? 1.0 : -1.0,
        p.y >= 0.0 ? 1.0 : -1.0
    );
    float2 oct = (n.z >= 0.0) ? p.xy : wrap;

    // 3. Remap [-1,1] to [0,1] for storage
    return oct * 0.5 + 0.5;
}
```

**Decoding** (reconstructing 3D direction from 2D):
```hlsl
float3 OctDecode(float2 f)
{
    // 1. Remap [0,1] back to [-1,1]
    f = f * 2.0 - 1.0;

    // 2. Reconstruct Z from L1 constraint
    float3 n = float3(f.xy, 1.0 - abs(f.x) - abs(f.y));

    // 3. Unwrap lower hemisphere
    float t = saturate(-n.z);
    n.xy += float2(n.x >= 0.0 ? -t : t, n.y >= 0.0 ? -t : t);

    // 4. Normalize to unit length
    return normalize(n);
}
```

### Error Analysis

**Angular Error Distribution**:
- **Max error**: ±0.5 degrees (imperceptible)
- **Distributed evenly** across the normal hemisphere
- **No clustering** of error at poles or edges like some packing schemes

**Comparison with alternatives**:

| Encoding | Storage | Max Error | Pros | Cons |
|----------|---------|-----------|------|------|
| **Octahedral** | 2 bytes (RG8) | ±0.5° | Uniform error, no lookup table | Slightly complex decode |
| Full precision | 6 bytes (RGB16F) | ~0° | No decode cost, max precision | Expensive bandwidth |
| RGBA10A2 | 4 bytes | ±1.0° | Compact, fast hardware | Uneven error distribution |
| Spherical map | 4 bytes (RG16) | ±0.5° | Fast decode | Degenerate at poles |
| Dual-paraboloid | 4 bytes (RG16) | ±1.0° | Cheap hardware | Two texture lookups |

**Reference**: Meyer, McGuire, Cigolle (2014). *"A Survey of Efficient Representations for Independent Unit Vectors."* Journal of Computer Graphics Techniques, Vol. 3, No. 2.

### Practical Usage

In your **GBuffer pixel shader**:
```hlsl
// Reconstruct world-space normal from texture + tangent frame
float3 tangentNorm = normalize(NormalMapSample * 2.0 - 1.0);  // Convert [0,1] to [-1,1]
float3 worldNorm = mul(tangentNorm, TBN);  // Transform to world space

// Encode for storage
float2 octNorm = OctEncode(worldNorm);  // → [0,1]²

// Pack with metallic/roughness
float4 normalRT = float4(octNorm.xy, metallic, roughness);
output.normalTarget = normalRT;
```

In your **lighting pixel shader**:
```hlsl
// Sample and decode
float2 octNorm = gbufferNormal.Sample(sampler, uv).rg;
float3 N = OctDecode(octNorm);
N = normalize(N);  // Re-normalize after interpolation

float metallic = gbufferNormal.Sample(sampler, uv).b;
float roughness = max(gbufferNormal.Sample(sampler, uv).a, MIN_ROUGHNESS);

// Use N in BRDF evaluation
float3 Lo = EvaluateCookTorrance(N, V, L, albedo, metallic, roughness);
```

## Velocity Buffer Generation

The velocity buffer is computed via **depth reprojection** in a separate fullscreen pass:

### Algorithm

For each pixel:

1. **Sample current depth**: `depth = gbufferDepth.Sample(uv)`
2. **Reconstruct world position**: `worldPos = ReconstructWorldPosition(uv, depth)` using current frame's `invViewProj`
3. **Reproject to previous frame**: `ndcPrev = mul(worldPos, prevViewProj)` using **previous frame's** `viewProj`
4. **Compute screen-space motion**: `velocity = (uv - uvPrev) * screenDimensions`
5. **Zero out sky**: If depth ≈ 0 or ≈ 1, set velocity to (0, 0)

### Constant Buffer (VelocityCB)

```cpp
struct VelocityCB {
    Mat4  invViewProj;    // Current frame: inverse view-projection
    Mat4  prevViewProj;   // Previous frame: view-projection (NOT inverse)
    float _pad[32] = {};  // Padding to 256-byte alignment
};  // Total: 256 bytes
```

**Critical**: Use `prevViewProj`, not `prevInvViewProj`. The projection goes **to** previous frame NDC, not from it.

### Shader Implementation

```hlsl
// velocity_ps.hlsl (fullscreen pixel shader)

cbuffer VelocityCB : register(b2) {
    float4x4 invViewProj;
    float4x4 prevViewProj;
};

Texture2D depthBuffer : register(t0);
SamplerState pointSampler : register(s0);

float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET
{
    // Sample depth at current pixel
    float depth = depthBuffer.Sample(pointSampler, uv).r;

    // Reconstruct current-frame world position
    float4 ndcCurrent = float4(
        uv * 2.0 - 1.0,  // [0,1] → [-1,1]
        depth,            // Z from depth buffer
        1.0               // W = 1 for multiplication
    );
    float4 worldPos = mul(ndcCurrent, invViewProj);
    worldPos.xyz /= worldPos.w;  // Perspective divide

    // Reproject to previous frame
    float4 ndcPrev = mul(float4(worldPos, 1.0), prevViewProj);
    ndcPrev.xyz /= ndcPrev.w;
    float2 uvPrev = ndcPrev.xy * 0.5 + 0.5;

    // Screen-space motion vector (pixels)
    float2 screenSize = float2(1920, 1080);  // or query from CB
    float2 velocity = (uv - uvPrev) * screenSize;

    // Sky pixels should not move
    if (depth < 0.001 || depth > 0.999) {
        velocity = float2(0, 0);
    }

    return float4(velocity, 0.0, 1.0);  // RG = motion, BA = unused
}
```

## Rules & Best Practices

### Do's

✅ **ALWAYS normalize normals after decoding**: `OctDecode()` output may denormalize through interpolation.
```hlsl
float3 N = normalize(OctDecode(octNormal.rg));
```

✅ **ALWAYS clamp roughness to MIN_ROUGHNESS**: Prevents singular BRDF lobes.
```hlsl
roughness = max(roughness, MIN_ROUGHNESS);  // MIN_ROUGHNESS = 0.04
```

✅ **ALWAYS output zero velocity for sky**: Prevents ghosting in TAA.
```hlsl
if (depth < 0.001 || depth > 0.999) velocity = float2(0, 0);
```

✅ **ALWAYS update VelocityCB each frame**: Camera or object movement requires new matrices.

✅ **ALWAYS multiply velocity by screen dimensions**: Velocity should be in screen pixels, not normalized [0,1].
```hlsl
float2 velocityPixels = velocity * screenDimensions;
```

### Don'ts

❌ **NEVER skip normalization after interpolation**: Rasterizer interpolates normals non-linearly.

❌ **NEVER use prevInvViewProj in velocity pass**: Use `prevViewProj` (not inverted).

❌ **NEVER pack world-space normals directly to RGBA8**: Causes banding. Always use octahedral encoding.

❌ **NEVER assume velocity is zero for static objects**: They move due to camera motion. Only sky should be zero.

❌ **NEVER store velocity outside [-screen_width, +screen_width]**: Clamp if needed for extreme motions.

## Troubleshooting

### Problem: Lighting looks flat or desaturated
**Cause**: Normal decoding is failing (normals all pointing up).
**Solution**: Ensure you're calling `OctDecode()` and normalizing the result.

### Problem: Visual artifacts (sparkles, flashing) in normal-mapped areas
**Cause**: ±0.5° error from octahedral encoding becoming visible at grazing angles.
**Solution**: This is expected and imperceptible in motion. If visible, consider using RGBA16F normals (costs 2x bandwidth).

### Problem: TAA ghosting or temporal artifacts
**Cause**: Velocity buffer is incorrect or sky pixels aren't zeroed.
**Solution**:
1. Visualize velocity as colors (velocity.x=red, velocity.y=green)
2. Verify sky regions are black (0, 0)
3. Check that VelocityCB matrices are updated correctly

### Problem: Motion blur looks wrong or blurs the sky
**Cause**: Velocity is non-zero for sky (depth near 0 or 1).
**Solution**: Clamp depth to [0.001, 0.999] before reprojection, zero velocity for out-of-range depths.

## References

1. Meyer, McGuire, Cigolle (2014). *"A Survey of Efficient Representations for Independent Unit Vectors."* Journal of Computer Graphics Techniques, Vol. 3, No. 2.
   - Full paper: [jcgt.org](https://jcgt.org/published/0003/02/01/)

2. Akenine-Möller, Haines, Hoffman (2018). *"Real-time Rendering, 4th Edition."* CRC Press.
   - Chapter 6: Deferred rendering, G-Buffer layouts

3. Tatarchuk, Shopf, DeCoro (2009). *"Advanced Lighting and Shading with DirectX 10."* SIGGRAPH 2009 Course.
   - Deferred shading architecture and bandwidth optimization

## See Also

- [Deferred Rendering Pipeline](../../engine/12-rendering/10-deferred-pipeline.md) — Full architecture overview
- [Post-Processing System](../../engine/12-rendering/07-post-processing.md) — TAA and motion blur usage
- [Shader Pipeline](./shader-pipeline.md) — Compilation and shader management
