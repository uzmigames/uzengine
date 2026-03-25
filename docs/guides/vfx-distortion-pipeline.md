<!-- doc-meta
title: VFX Distortion Rendering Pipeline
category: guide
engine: uzengine
theme: rendering
created: 2026-03-25
updated: 2026-03-25
status: active
author: docs-operator
-->

# VFX Distortion Rendering Pipeline

## Overview

Screen-space distortion (refraction) is a real-time rendering technique that warps the visual appearance of objects and the environment to simulate effects like heat shimmer, glass refraction, and energy shields. UzEngine implements distortion through a two-pass GPU-driven pipeline that accumulates UV offsets from distortion-emitting particles, then applies those offsets to warp the HDR color buffer.

This guide explains the architecture, data flow, and implementation details of the distortion system.

## Context

The distortion pipeline is part of the UzEngine VFX system and sits in the transparent rendering phase, after opaque geometry is drawn but before post-processing (tonemapping, SMAA, etc.). It works in conjunction with the particle simulation system to render visual effects that require transparent, view-dependent distortion.

See also: [VFX System Overview](../architecture/vfx-overview.md), [GPU Particle System](../architecture/gpu-particles.md)

## Architecture Overview

The distortion pipeline uses a two-pass approach:

### Pass 1: Accumulation
Draw distortion-emitting particles into an RGBA16F target. Each pixel outputs a screen-space UV offset (RG channels) and distortion strength (A channel). Particles are rendered with additive blending so overlapping distortion sources combine their offsets.

### Pass 2: Resolve
Full-screen warp pass that reads the accumulated UV offsets and applies them to the HDR color buffer. Each pixel is sampled at a warped location: `warpedUV = saturate(uv + offset.rg * offset.a)`.

```
┌─────────────────────────────────────────────────────────────────┐
│                    Distortion Pipeline Data Flow                │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  Emitter Particles (GPU Buffers)                               │
│       ↓                                                          │
│  Emitter Simulation Pass                                       │
│       ↓                                                          │
│  [Sort emitters back-to-front by distance to camera]          │
│       ↓                                                          │
│  ┌─ Accumulation Pass ────────────────────────────────────────┐ │
│  │  For each emitter (back-to-front):                         │ │
│  │  • Bind distortion PSO (billboard VS + distortion PS)      │ │
│  │  • Set per-emitter constants (strength, normal map)        │ │
│  │  • DrawIndexedIndirect with particle count                 │ │
│  │  Output: RGBA16F distortion target                         │ │
│  │    RG = accumulated UV offset                              │ │
│  │    A = accumulated distortion strength                      │ │
│  └────────────────────────────────────────────────────────────┘ │
│       ↓                                                          │
│  [Copy HDR color buffer to pre-distortion texture]            │
│       ↓                                                          │
│  ┌─ Resolve Pass ─────────────────────────────────────────────┐ │
│  │  Fullscreen triangle (no vertex buffer):                   │ │
│  │  For each pixel:                                           │ │
│  │  1. Sample distortion texture at original UV               │ │
│  │  2. Compute warpedUV = uv + offset.rg * offset.a           │ │
│  │  3. Sample pre-distortion color at warpedUV                │ │
│  │  4. Write result to color target                           │ │
│  │  Output: Warped HDR color in color target                  │ │
│  └────────────────────────────────────────────────────────────┘ │
│       ↓                                                          │
│  [Continue to post-processing: tonemapping, SMAA, etc.]       │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

## Accumulation Pass

### Purpose
Render distortion-emitting particles and accumulate their UV offset contributions into a single RGBA16F texture.

### Configuration: DistortionPassDesc

```cpp
struct DistortionPassDesc {
    // Render targets
    rg::RgResourceId distortionTarget;  // RGBA16F accumulation texture
    rg::RgResourceId depthTarget;       // Scene depth (read-only)

    // Per-emitter render data
    std::vector<DistortionEmitterData> emitters;
};
```

### Per-Emitter Data: DistortionEmitterData

Each emitter provides:

| Field | Type | Purpose |
|-------|------|---------|
| `pipeline` | RHIPipelineHandle | Graphics PSO (billboard VS + distortion PS) |
| `billboardCB` | CBAllocation | Per-emitter billboard constants (view/proj) |
| `distortionCB` | CBAllocation | Distortion strength + normal map SRV index |
| `readSetSrvIndex` | uint32_t | SRV descriptor heap index for particle attributes |
| `buffer` | GPUParticleBuffer* | GPU particle storage (for indirect draw args) |
| `distanceToCamera` | float | Distance for back-to-front sorting |
| `rgReadPositions`, `rgReadVelocities`, etc. | RgResourceId | Render graph resource IDs for barrier tracking |
| `rgIndirectDrawArgs` | RgResourceId | Indirect draw arguments buffer |

### Root Signature Layout

| Slot | Register | Type | Purpose |
|------|----------|------|---------|
| 0 | b0 | CBV | DistortionConstants (strength, normal map SRV) |
| 1 | b1 | CBV | Billboard constants (view/proj, viewport) |
| 5 | t0..tN | SRV Table | Particle attributes + normal map |

### Rendering Flow

1. **Sort emitters back-to-front** by `distanceToCamera` to ensure correct alpha blending of overlapping distortion sources.

2. **For each emitter**:
   - Set the graphics pipeline (billboard VS + distortion PS)
   - Bind per-emitter constants:
     - b0: distortion strength multiplier + normal map SRV index
     - b1: view/proj matrices + viewport dimensions
   - Bind SRV descriptor table for particle attributes
   - Issue `DrawIndexedIndirect` using the emitter's indirect draw args buffer

3. **Blend state**: Additive blending (SrcAlpha / One) so overlapping distortion sources combine their UV offsets.

4. **Depth state**: Read-only depth testing (no depth writes) to prevent distortion particles from occluding subsequent geometry.

### DistortionConstants

```cpp
struct DistortionConstants {
    float    distortionStrength = 1.0f;       // UV offset magnitude multiplier
    uint32_t normalMapSrvIndex  = UINT32_MAX; // SRV index for normal map texture
    float    _pad0              = 0.0f;       // Padding to 16-byte alignment
    float    _pad1              = 0.0f;       // Padding to 16-byte alignment
};
```

## Distortion Pixel Shader

The distortion PS (`distortion_ps.hlsl`) generates UV offset vectors from a normal map:

### Input
- `texcoord` — billboard UV in [0, 1] to sample the normal map
- `color.a` — particle alpha, controls per-particle distortion intensity

### Algorithm

1. **Sample normal map** at the billboard UV (tangent-space, [0,1] encoded)
2. **Decode normal** from [0,1] to [-1,1]
3. **Compute UV offset**: `uvOffset = normal.xy * strength * DISTORTION_UV_SCALE`
4. **Modulate intensity**: `intensity = particleAlpha * saturate(strength)`
5. **Output**: `float4(uvOffset.x, uvOffset.y, 0.0, intensity)`

### Constants

| Name | Value | Purpose |
|------|-------|---------|
| `DISTORTION_UV_SCALE` | 0.01 | UV offset scale per unit normal displacement |
| `DISTORTION_MIN_ALPHA` | 0.001 | Discard threshold for invisible particles |

**Note**: DISTORTION_UV_SCALE of 0.01 produces a ~10-pixel warp at 1080p resolution per unit strength. This scale empirically matches real-time VFX pipelines for heat shimmer and energy shield effects. @ref Vlachos 2015 "Advanced VR Rendering"

### Output Format

| Channel | Value | Purpose |
|---------|-------|---------|
| R | Horizontal offset | Warp direction (positive = right) |
| G | Vertical offset | Warp direction (positive = down) |
| B | 0.0 | Unused |
| A | Intensity | Particle alpha × distortion strength (warp magnitude) |

## Resolve Pass

### Purpose
Read the accumulated distortion offsets and apply them to the HDR color buffer via a fullscreen warp pass.

### Configuration: DistortionResolveDesc

```cpp
struct DistortionResolveDesc {
    // Pipeline
    RHIPipelineHandle resolvePipeline;  // Fullscreen triangle PSO

    // Render targets
    rg::RgResourceId colorTarget;       // HDR color (output)
    rg::RgResourceId distortionSource;  // RGBA16F distortion accumulation
    rg::RgResourceId preDistortionCopy; // Copy of color before distortion

    // Bindless descriptor indices
    uint32_t colorTextureSrvIndex;
    uint32_t distortionTextureSrvIndex;
};
```

### Root Signature Layout

| Slot | Register | Type | Purpose |
|------|----------|------|---------|
| 0 | b0 | Root Constants | Color + distortion SRV indices (8 bytes) |

### Rendering Flow

1. **Bind the fullscreen triangle pipeline** (no vertex buffer, no vertex binding)
2. **Set root constants** with SRV descriptor indices:
   ```cpp
   uint32_t rootConstants[4] = {
       colorTextureSrvIndex,
       distortionTextureSrvIndex,
       0, // padding
       0  // padding
   };
   ```
3. **Draw 3 vertices** — the vertex shader generates clip-space positions and UVs from SV_VertexID

### DistortionResolveConstants

```cpp
struct DistortionResolveConstants {
    uint32_t colorTextureSrvIndex      = UINT32_MAX; // Pre-distortion HDR color
    uint32_t distortionTextureSrvIndex = UINT32_MAX; // RGBA16F distortion target
    uint32_t _pad0                     = 0;          // Padding to 16-byte alignment
    uint32_t _pad1                     = 0;          // Padding to 16-byte alignment
};
```

## Distortion Resolve Pixel Shader

The resolve PS (`distortion_resolve_ps.hlsl`) warps the pre-distortion HDR color:

### Algorithm

1. **Sample distortion texture** at the original screen UV
2. **Extract UV offset** (RG) and **warp magnitude** (A)
3. **Compute warped UV**: `warpedUV = saturate(uv + offset.rg * offset.a)`
4. **Sample pre-distortion color** at the warped UV
5. **Output warped color** to the color target

### Key Properties

- **No branching for zero distortion**: When `offset.a == 0`, `warpedUV == originalUV`, automatically producing an identity transform.
- **Clamped UV wrapping**: `saturate()` clamps the warped UV to [0,1] to prevent sampling outside texture boundaries at screen edges.
- **Pre-distortion copy required**: The pass reads from a copy of the HDR color buffer taken BEFORE the accumulation pass to avoid read-write hazards on the live color target.

### Warp Formula

```hlsl
float2 offset    = distortion.Sample(uv).rg;    // UV offset direction
float  magnitude = distortion.Sample(uv).a;     // Warp strength
float2 warpedUV  = saturate(uv + offset * magnitude);
float4 color     = preDistortionColor.Sample(warpedUV);
```

## Render Graph Integration

### Pass Order

The distortion pipeline sits in the transparent phase:

1. **Opaque rendering** (gbuffer, lighting, shadows)
2. **HDR color copy** (copy color buffer before distortion)
3. **Accumulation pass** (render distortion particles)
4. **Resolve pass** (warp HDR color by accumulated offsets)
5. **Post-processing** (tonemapping, SMAA, bloom, etc.)

### Resource Dependencies

**Accumulation Pass**:
- Writes: `distortionTarget` as `RenderTarget`
- Reads: `depthTarget` as `DepthStencilRead`
- Reads: Particle attribute buffers as `ShaderResource`
- Reads: `indirectDrawArgs` as `IndirectArgument`

**Resolve Pass**:
- Writes: `colorTarget` as `RenderTarget`
- Reads: `distortionSource` as `ShaderResource`
- Reads: `preDistortionCopy` as `ShaderResource`

The render graph automatically inserts resource transitions and synchronization barriers based on these declarations.

## Implementation Example

### Setting Up the Accumulation Pass

```cpp
// Prepare emitter data (from VFX system)
std::vector<DistortionEmitterData> emitters;
for (auto& emitterInstance : activeDistortionEmitters) {
    DistortionEmitterData data;
    data.pipeline = distortionPipeline;
    data.billboardCB = AllocateBillboardConstants(emitterInstance);
    data.distortionCB = AllocateDistortionConstants(
        emitterInstance.strength,
        emitterInstance.normalMapSrvIndex
    );
    data.buffer = emitterInstance.gpuParticleBuffer;
    data.distanceToCamera = Length(emitterInstance.position - cameraPos);
    data.readSetSrvIndex = emitterInstance.readSetDescriptorIndex;
    // ... populate render graph resource IDs
    emitters.push_back(data);
}

// Register accumulation pass
DistortionPassDesc accumulateDesc;
accumulateDesc.distortionTarget = distortionTargetResourceId;
accumulateDesc.depthTarget = depthBufferResourceId;
accumulateDesc.emitters = std::move(emitters);
AddDistortionAccumulationPass(rgBuilder, accumulateDesc);
```

### Setting Up the Resolve Pass

```cpp
// Copy HDR color before distortion
rg::RgResourceId colorCopyId = builder.AddPass("CopyPreDistortionColor", [...]);

// Register resolve pass
DistortionResolveDesc resolveDesc;
resolveDesc.resolvePipeline = distortionResolvePipeline;
resolveDesc.colorTarget = colorBufferResourceId;
resolveDesc.distortionSource = distortionTargetResourceId;
resolveDesc.preDistortionCopy = colorCopyId;
resolveDesc.colorTextureSrvIndex = colorCopySrvIndex;
resolveDesc.distortionTextureSrvIndex = distortionTargetSrvIndex;
AddDistortionResolvePass(builder, resolveDesc);
```

## Performance Considerations

### Bandwidth
- **Accumulation pass**: Bounded by particle rasterization and normal map sampling.
- **Resolve pass**: Full-screen UV warp is essentially a memory-bound fullscreen pass (reads distortion texture, reads pre-distortion color, writes color target).

### Optimization Techniques
1. **Back-to-front sorting** ensures correct blending without requiring depth/transparency sorting algorithms.
2. **Additive blending** naturally combines overlapping distortion sources; no manual composition needed.
3. **Normal map caching**: Use a single shared normal map texture across multiple emitters to reduce VRAM bandwidth.
4. **Lower resolution distortion**: Render the accumulation pass at half or quarter resolution, then upsample in the resolve pass (requires special handling of UV warping at boundaries).

### GPU Profiling
Use the buffer visualization grid (`--shadow-debug 1`) to inspect the distortion accumulation texture in real-time:

```bash
# View distortion target as a raw texture
demo.exe --shadow-debug 1 --screenshot-stdout --screenshot-frame 10 --frames 20
```

The distortion target will appear as a 16F floating-point texture; positive values appear bright, negative values appear dark. The alpha channel encodes the accumulated warp magnitude.

## Common Issues and Solutions

### Issue: No Visible Distortion
- **Check normal map**: Verify the normal map texture is loaded and the SRV index is correct.
- **Check strength**: Ensure `distortionStrength` is > 0 and not NaN.
- **Check emitter distance**: Verify emitters are visible in the camera frustum and `distanceToCamera` is computed correctly.
- **Visual debug**: Use `--shadow-debug 1` to inspect the raw distortion accumulation texture.

### Issue: Black Screen After Distortion
- **Check color copy**: Verify the pre-distortion color copy pass is registered BEFORE the resolve pass.
- **Check SRV indices**: Ensure `colorTextureSrvIndex` and `distortionTextureSrvIndex` are valid heap indices.
- **Check render target binding**: Verify the color target is correctly bound as a render target in the resolve pass.

### Issue: Distortion Wraps at Screen Edges
- The resolve pass uses `saturate(warpedUV)` to clamp warped UVs to [0,1], preventing out-of-bounds sampling. This is correct behavior — distortion at screen edges will sample from the clamped border color (usually opaque).

### Issue: Overlapping Particles Cause Banding
- This indicates either a precision issue with the RGBA16F format (unlikely) or uneven alpha distribution across particles. Verify particle alpha values are smooth and not quantized.

## Key Takeaways

- **Two-pass architecture**: Accumulation builds up UV offsets, resolve applies them to color.
- **Additive blending**: Distortion sources naturally combine via additive blending with no manual composition.
- **Back-to-front sorting**: Ensures correct blending order for overlapping distortion volumes.
- **Screen-space efficiency**: The resolve pass is a simple fullscreen warp; GPU cost is proportional to pixel count, not scene complexity.
- **Pre-distortion copy required**: The resolve pass must read from a snapshot of the color buffer taken before distortion to avoid read-write conflicts.
- **Normal map sampling**: Distortion direction is derived from tangent-space normal map; UV scale is empirically tuned (DISTORTION_UV_SCALE = 0.01).
- **Bindless descriptors**: Both passes use SRV descriptor heap indices for flexible texture binding without PSO recompilation.

## References

- @ref Vlachos 2015 "Advanced VR Rendering" — screen-space distortion and refraction techniques
- [GPU Particle System](../architecture/gpu-particles.md)
- [VFX System Overview](../architecture/vfx-overview.md)
- [Render Graph Documentation](../architecture/render-graph.md)
- [Bindless Rendering](../architecture/bindless-rendering.md)

## Changelog

| Date | Change |
|------|--------|
| 2026-03-25 | Initial creation — documented accumulation and resolve passes, shader details, and integration guide. |
