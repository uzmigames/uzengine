# Renderer Passes — Complete Reference

> **Status**: Implemented (Milestone 1.2–Phase 2)
> **Source**: `runtime/engine/renderer/passes/`
> **Depends on**: Render Graph (§04), RHI (§03), Constant Buffers, Draw List

## Purpose

Renderer passes are the building blocks of the rendering pipeline, each implementing a specific stage in the deferred or forward rendering workflow. This document comprehensively documents all 9 implemented passes:

1. **PBR Pass** — Forward rendering with Cook-Torrance BRDF
2. **GBuffer Pass** — Deferred rendering geometry stage
3. **Lighting Pass** — Deferred rendering composition stage with split-sum IBL
4. **Shadow Pass** — Depth-only rendering for shadow maps
5. **Sky Atmosphere Pass** — Bruneton atmospheric scattering with precomputed transmittance LUT
6. **Transmittance LUT Pass** — Precomputes 256×64 atmosphere transmittance table
7. **Sky Cubemap Capture Pass** — Captures sky atmosphere into 64×64×6 cubemap for IBL
8. **Height Fog Pass** — Exponential height-based atmospheric fog
9. **Debug View Pass** — G-Buffer channel visualization with 13 diagnostic modes

Each pass is registered in the render graph, manages resource transitions automatically, and supports both SM6.6 bindless and SM5 fallback binding models.

## Quick Reference

| Pass | Purpose | Input | Output | Type | Location |
|------|---------|-------|--------|------|----------|
| PBR | Forward Cook-Torrance | DrawList, Materials, Camera | Lit backbuffer + depth | Full render pass | `geometry/pbr_pass.h` |
| GBuffer | Deferred geometry stage | DrawList, Materials, Camera | 2x MRT (Albedo, Normal) + depth + velocity | Full render pass | `geometry/gbuffer_pass.h` |
| Velocity | Motion vector generation | Depth, prev/cur view-proj | Velocity buffer (RG16F) | Fullscreen triangle | `postprocess/velocity_pass.h` |
| Lighting | Deferred composition with IBL | G-Buffers + IBL cubemaps + lights | Lit backbuffer | Fullscreen triangle | `lighting/lighting_pass.h` |
| Shadow | Shadow depth rendering | DrawList per cascade | 2×2 cascade atlas | Depth-only pass | `lighting/shadow_pass.h` |
| Sky Atmosphere | Bruneton atmospheric scattering | SkyAtmosphereComponent, transmittance LUT | Lit fullscreen | Fullscreen triangle | `atmosphere/sky_atmosphere_pass.h` |
| Transmittance LUT | Precompute atmosphere table | Bruneton parameters | 256×64 RGBA16F texture | Compute dispatch | `atmosphere/transmittance_lut_pass.h` |
| Sky Cubemap | IBL environment capture | Sky Atmosphere output | 64×64×6 cubemap | Compute dispatch | `atmosphere/sky_cubemap_capture_pass.h` |
| Height Fog | Exponential fog blending | Scene color, depth, fog params | Fogged scene | Fullscreen triangle | `atmosphere/height_fog_pass.h` |
| Debug View | G-Buffer visualization | G-Buffers, Lighting buffer | Visualization output | Fullscreen triangle | `debug/debug_view_pass.h` |

> **ℹ️ Note**: The **Debug View pass** is inserted after Lighting, before Tonemapping, for real-time channel visualization without shader recompilation. See **[Debug View Modes Spec](../../specs/rendering/debug-view-modes.md)** for complete details (13 modes, F1–F11 shortcuts, zero overhead when disabled).
>
> **ℹ️ Note**: **Atmosphere passes** (Sky, Transmittance LUT, Sky Cubemap, Height Fog) are executed after Lighting. The transmittance LUT is precomputed once at initialization. The sky cubemap is captured each frame and feeds into the IBL baker to drive ambient lighting.

## PBR Pass — Forward Rendering

**Header**: `runtime/engine/renderer/passes/pbr_pass.h`

### Purpose

Executes a single-pass forward PBR rendering with Cook-Torrance BRDF. Iterates a sorted DrawList and issues one indexed draw call per command with per-object constant buffer binding.

### Configuration (PBRPassDesc)

```cpp
struct PBRPassDesc {
    rg::RgResourceId backbuffer;       ///< Render target (write, RenderTarget state)
    rg::RgResourceId depthBuffer;      ///< Depth buffer (write, DepthStencilWrite state)
    RHITextureHandle  depthTexture;    ///< Physical depth texture for render pass setup
    const DrawList*   drawList;        ///< Sorted draw list to iterate
    CBRingAllocator*  cbAllocator;     ///< Per-object CB ring allocator
    CBAllocation      perFrameAlloc;   ///< Pre-allocated per-frame CB (filled by caller)
    RHIPipelineHandle pipeline;        ///< PSO handle for the PBR shader
    uint32_t          vertexBufferSrvIndex; ///< SRV index for bindless vertex fetch (SM6.6)
    RHIBufferHandle   vertexBuffer;    ///< Vertex buffer handle (SM5 fallback)
    RHIBufferHandle   indexBuffer;     ///< Index buffer handle
    RHIIndexFormat    indexFormat;     ///< Uint16 or Uint32
    uint32_t          indexCount;      ///< Total indices per mesh
    bool              bindless;        ///< True if SM6.6 bindless path is active
    float             viewportWidth;   ///< Viewport width in pixels
    float             viewportHeight;  ///< Viewport height in pixels
};
```

### Render Graph Integration

The pass declares:
- **Backbuffer**: Write (RenderTarget state)
- **Depth buffer**: Write (DepthStencilWrite state)

Barriers are scheduled automatically by the render graph compiler.

### Constant Buffer Bindings

- **Root Parameter 1**: Per-frame CB (PerFrameCB) — camera, lighting, time
- **Root Parameter 2**: Per-object CB (PerObjectCB) — model matrix, normal matrix, material index
- **Root Constant 0** (SM6.6 only): SRV descriptor indices for bindless vertex fetch

### Binding Model

**SM6.6 Bindless**:
- Vertex buffer bound as SRV (descriptor index pushed via root constant)
- No Input Assembler vertex format binding
- Vertex fetch in shader via `StructuredBuffer<Vertex>`

**SM5 Fallback**:
- Explicit SetVertexBuffer with Input Assembler state
- RHI translates PSO input layout to D3D12 input element descriptors
- Stride must match configured vertex format

### Pass Execution

During execution the pass:

1. Clears the backbuffer (typically to mid-gray) and depth buffer (to 1.0)
2. Begins a render pass with one color attachment and depth
3. Binds the pipeline, vertex/index buffers, and per-frame CB
4. For each draw command:
   - Allocates a PerObjectCB from the ring allocator (avoid stalls during triple buffering)
   - Fills model matrix, normal matrix (inverse transpose), and material index
   - Binds the PerObjectCB at root parameter 2
   - Issues DrawIndexed with the command's index count and offset
5. Ends the render pass

### Usage Example

```cpp
PBRPassDesc desc{};
desc.backbuffer      = builder.ImportRenderTarget(swapchainRT);
desc.depthBuffer     = builder.CreateDepthBuffer(...);
desc.depthTexture    = physicalDepthHandle;
desc.drawList        = &frame.drawList;  // Pre-sorted by material/mesh
desc.cbAllocator     = &cbRingAllocator;
desc.perFrameAlloc   = perFrameCB;  // Pre-filled with camera, lighting
desc.pipeline        = pbrPipeline;
desc.vertexBuffer    = vertexBufferHandle;
desc.indexBuffer     = indexBufferHandle;
desc.indexFormat     = RHIIndexFormat::Uint32;
desc.indexCount      = totalIndexCount;
desc.bindless        = isBindlessSupported;
desc.viewportWidth   = renderWidth;
desc.viewportHeight  = renderHeight;

AddPBRPass(builder, desc);
```

---

## GBuffer Pass — Deferred Geometry Stage

**Header**: `runtime/engine/renderer/passes/gbuffer_pass.h`

### Purpose

Renders opaque geometry to a deferred G-Buffer, outputting surface attributes (albedo, normals, metallic/roughness) into three render targets plus depth. Decouples geometry shading from lighting calculations.

### Configuration (GBufferPassDesc)

```cpp
struct GBufferPassDesc {
    rg::RgResourceId albedoTarget;      ///< Albedo RT (write, RGBA8Unorm, RGB color + A AO)
    rg::RgResourceId normalTarget;      ///< Normal RT (write, RGBA8Unorm, RG normal + B metallic + A roughness)
    rg::RgResourceId depthBuffer;       ///< Depth buffer (write, D32Float)
    RHITextureHandle  depthTexture;     ///< Physical depth texture
    const DrawList*   drawList;         ///< Sorted draw list
    CBRingAllocator*  cbAllocator;      ///< Per-object CB allocator
    CBAllocation      perFrameAlloc;    ///< Pre-allocated per-frame CB
    RHIPipelineHandle pipeline;         ///< PSO for G-Buffer shader
    uint32_t          vertexBufferSrvIndex; ///< SRV for SM6.6 bindless
    RHIBufferHandle   vertexBuffer;     ///< VB for SM5 fallback
    RHIBufferHandle   indexBuffer;      ///< Index buffer
    RHIIndexFormat    indexFormat;      ///< Uint16 or Uint32
    uint32_t          indexCount;       ///< Indices per mesh
    bool              bindless;         ///< SM6.6 or SM5
    float             viewportWidth;    ///< Viewport dimensions
    float             viewportHeight;
};
```

**Note** (Milestone 1.3.1): Removed `metalRoughTarget` — metallic/roughness now packed with normal in second MRT. Velocity buffer generated separately via depth reprojection.

### G-Buffer Layout

| Target | Format | Semantics | Bits | Notes |
|--------|--------|-----------|------|-------|
| Albedo | RGBA8Unorm | RGB base color + A ambient occlusion | 32 | Linear color (sRGB→linear conversion in PS) |
| Normal | RGBA8Unorm | RG octahedral-encoded normal + B metallic + A roughness | 32 | Normal encoding via `OctEncode()`; <0.5° max error |
| Depth | D32Float | Linear depth | 32 | Written by rasterizer, used for position reconstruction |
| Velocity | RG16Float | Screen-space motion vectors (pixels) | 32 | Generated via separate depth reprojection pass |

**Total bandwidth**: 16 bytes/pixel (12 B for GBuffer + 4 B for velocity pass output)

### Render Graph Integration

The pass declares:
- **Albedo target**: Write (RenderTarget state, RGBA8Unorm)
- **Normal target**: Write (RenderTarget state, RGBA8Unorm)
- **Depth buffer**: Write (DepthStencilWrite state, D32Float)

After the GBuffer pass, a separate **Velocity Pass** reads the depth and writes velocity vectors.

Barriers are scheduled automatically by the render graph compiler.

### Constant Buffer Bindings

- **Root Parameter 1**: Per-frame CB (PerFrameCB)
- **Root Parameter 2**: Per-object CB (PerObjectCB) — model matrix, normal matrix
- **Root Constant 0** (SM6.6 only): SRV descriptor index for bindless vertex fetch

### Binding Model

Identical to PBR Pass:
- **SM6.6**: Bindless vertex buffer via SRV
- **SM5**: Input Assembler with explicit vertex format

### Pass Execution

During execution the pass:

1. Clears both G-Buffer render targets (albedo, normal) and the depth buffer
2. Begins a render pass with two color attachments and depth
3. Binds the pipeline, vertex/index buffers, and per-frame CB
4. For each draw command:
   - Allocates a PerObjectCB from the ring allocator
   - Fills model/normal matrices and material texture indices
   - Binds at root parameter 2
   - Issues DrawIndexed
5. Ends the render pass

**Key differences from older 3-RT layout**:
- Only 2 color attachments (not 3)
- Metallic/roughness packed into Normal RT alpha channel (requires `OctEncode()` in shader)
- Ambient occlusion moved to Albedo RT alpha channel
- Velocity generated separately (see velocity_pass.h)

### Usage Example

```cpp
GBufferPassDesc desc{};
desc.albedoTarget     = builder.CreateRenderTarget(w, h, RHIFormat::RGBA8Unorm);
desc.normalTarget     = builder.CreateRenderTarget(w, h, RHIFormat::RGBA8Unorm);  // Packed: Oct(normal) + metallic + roughness
desc.depthBuffer      = builder.CreateDepthBuffer(w, h, RHIFormat::D32Float);
desc.depthTexture     = physicalDepthHandle;
desc.drawList         = &frame.drawList;
desc.cbAllocator      = &cbRingAllocator;
desc.perFrameAlloc    = perFrameCB;
desc.pipeline         = gBufferPipeline;
desc.vertexBuffer     = vertexBufferHandle;
desc.indexBuffer      = indexBufferHandle;
desc.indexFormat      = RHIIndexFormat::Uint32;
desc.indexCount       = totalIndexCount;
desc.bindless         = isBindlessSupported;
desc.viewportWidth    = renderWidth;
desc.viewportHeight   = renderHeight;

AddGBufferPass(builder, desc);

// Velocity pass (Phase 1.3.1+) — separate, after GBuffer
VelocityPassDesc velocityDesc{};
velocityDesc.velocityTarget = builder.CreateRenderTarget(w, h, RHIFormat::RG16Float);
velocityDesc.depthBuffer    = depthResourceId;  // Read from GBuffer pass output
velocityDesc.depthTexture   = physicalDepthHandle;
velocityDesc.depthSrvIndex  = depthSrvIndex;
velocityDesc.velocityCBAlloc = velocityCB;  // Contains invViewProj + prevViewProj
velocityDesc.pipeline       = velocityPipeline;
velocityDesc.viewportWidth  = renderWidth;
velocityDesc.viewportHeight = renderHeight;
velocityDesc.bindless       = isBindlessSupported;

AddVelocityPass(builder, velocityDesc);
```

---

## Lighting Pass — Deferred Composition

**Header**: `runtime/engine/renderer/passes/lighting_pass.h`

### Purpose

Reads the G-Buffer textures and computes Cook-Torrance BRDF lighting per pixel using a fullscreen triangle (generated via SV_VertexID, no vertex/index buffer). Composites the final lit result into the backbuffer.

### Configuration (LightingPassDesc)

```cpp
struct LightingPassDesc {
    rg::RgResourceId backbuffer;        ///< Final lit output (write, RenderTarget)
    rg::RgResourceId albedoGBuffer;     ///< Albedo input (read, ShaderResource)
    rg::RgResourceId normalGBuffer;     ///< Normal input (read, ShaderResource)
    rg::RgResourceId metalRoughGBuffer; ///< MetalRough input (read, ShaderResource)
    rg::RgResourceId depthGBuffer;      ///< Depth input (read as SRV, not DSV)
    CBAllocation      perFrameAlloc;    ///< Per-frame CB with invViewProj (bound at b1)
    CBAllocation      lightingAlloc;    ///< LightingCB with light array (bound at b3)
    RHIPipelineHandle pipeline;         ///< Lighting PSO (fullscreen, no depth)
    uint32_t          albedoSrvIndex;   ///< SRV descriptor index (SM6.6)
    uint32_t          normalSrvIndex;   ///< SRV descriptor index (SM6.6)
    uint32_t          metalRoughSrvIndex; ///< SRV descriptor index (SM6.6)
    uint32_t          depthSrvIndex;    ///< SRV descriptor index (SM6.6)
    RHITextureHandle  albedoTexture;    ///< Physical texture (SM5)
    RHITextureHandle  normalTexture;    ///< Physical texture (SM5)
    RHITextureHandle  metalRoughTexture;///< Physical texture (SM5)
    RHITextureHandle  depthTextureForSrv;///< Physical depth texture (SM5)
    CBAllocation      shadowAlloc;      ///< ShadowDataCB (bound at b4, optional)
    bool              bindless;         ///< SM6.6 or SM5
    float             viewportWidth;    ///< Viewport dimensions
    float             viewportHeight;
};
```

### Render Graph Integration

The pass declares:
- **Backbuffer**: Write (RenderTarget state)
- **Albedo G-Buffer**: Read (ShaderResource state)
- **Normal G-Buffer**: Read (ShaderResource state)
- **MetalRough G-Buffer**: Read (ShaderResource state)
- **Depth G-Buffer**: Read (ShaderResource state, read as SRV not DSV)

Barriers are scheduled automatically by the render graph compiler.

### Constant Buffer Bindings

- **Root Parameter 1**: Per-frame CB (PerFrameCB) — contains `invViewProj` for ray reconstruction
- **Root Parameter 3**: Lighting CB (LightingCB) — point light array + count
- **Root Parameter 4**: Shadow CB (ShadowDataCB) — cascade matrices and split distances (optional)
- **Root Constant 0** (SM6.6 only): G-Buffer SRV descriptor indices pushed as constants

### Binding Model

**SM6.6 Bindless**:
- G-Buffer SRV indices pushed via root constants at slot 0
- Descriptor indices used to fetch from unbounded descriptor heap

**SM5 Fallback**:
- G-Buffer textures bound via SetFragmentTexture at t0–t3
- RHI translates descriptor indices to explicit register assignments

### Pass Execution

During execution the pass:

1. Clears the backbuffer
2. Begins a render pass with one color attachment (no depth)
3. Binds the lighting pipeline, per-frame CB, and lighting CB
4. For SM6.6: Pushes G-Buffer SRV indices via root constants
5. For SM5: Binds G-Buffer textures at t0–t3
6. Issues Draw(3) for the fullscreen triangle (generated from SV_VertexID)
7. Ends the render pass

The fullscreen triangle eliminates vertex/index buffer binding overhead and guarantees pixel-perfect coverage.

### Usage Example

```cpp
LightingPassDesc desc{};
desc.backbuffer          = builder.ImportRenderTarget(swapchainRT);
desc.albedoGBuffer       = gBufferAlbedo;  // From previous GBuffer pass
desc.normalGBuffer       = gBufferNormal;
desc.metalRoughGBuffer   = gBufferMetalRough;
desc.depthGBuffer        = gBufferDepth;
desc.perFrameAlloc       = perFrameCB;  // Pre-filled with invViewProj
desc.lightingAlloc       = lightingCB;  // Pre-filled with lights
desc.pipeline            = lightingPipeline;
desc.albedoSrvIndex      = albedoSrvIdx;
desc.normalSrvIndex      = normalSrvIdx;
desc.metalRoughSrvIndex  = metalRoughSrvIdx;
desc.depthSrvIndex       = depthSrvIdx;
desc.shadowAlloc         = shadowCB;  // Optional
desc.bindless            = isBindlessSupported;
desc.viewportWidth       = renderWidth;
desc.viewportHeight      = renderHeight;

AddLightingPass(builder, desc);
```

---

## Shadow Pass — Depth-Only Rendering

**Header**: `runtime/engine/renderer/passes/shadow_pass.h`

### Purpose

Renders depth-only geometry for cascaded shadow maps (CSM). The pass writes to a single shadow atlas texture laid out as a 2×2 grid of cascade tiles. For each cascade, the pass sets viewport and scissor to the cascade's tile and issues draw calls with shadow-space light view-projection matrices.

### Configuration (ShadowPassDesc)

```cpp
struct ShadowPassDesc {
    rg::RgResourceId  shadowAtlas;
    RHITextureHandle  shadowAtlasTexture;
    const DrawList*   drawList;
    CBRingAllocator*  cbAllocator;
    RHIPipelineHandle pipeline;
    uint32_t          vertexBufferSrvIndex;
    RHIBufferHandle   vertexBuffer;
    RHIBufferHandle   indexBuffer;
    RHIIndexFormat    indexFormat;
    uint32_t          indexCount;
    bool              bindless;
    ShadowCascade     cascades[kMaxShadowCascades];
    uint32_t          cascadeCount;
    uint32_t          atlasResolution;
};
```

### Shadow Atlas Layout

A single 2D depth texture of size `(atlasResolution * 2) × (atlasResolution * 2)`. Four cascades arranged in a 2×2 grid:

```
┌─────────────────────────────┐
│  Cascade 0 (0,0)  │ Cascade 1 (res,0)  │
├──────────────────┼────────────────────┤
│ Cascade 2 (0,res)│ Cascade 3(res,res) │
└─────────────────────────────┘
```

Each cascade is a square tile at resolution × resolution pixels.

### Render Graph Integration

The pass declares:
- **Shadow atlas**: Write (DepthStencilWrite state)

Barriers are scheduled automatically by the render graph compiler.

### Constant Buffer Bindings

- **Root Parameter 2**: Per-object CB (PerObjectCB) — world matrix (identity for most objects)
- **Root Parameter 3**: Shadow CB (ShadowDataCB) — light view-projection matrix for current cascade
- **Root Constant 0** (SM6.6 only): SRV descriptor index for bindless vertex fetch

**Note**: No per-frame CB (root parameter 1) is bound since the shadow pass does not need camera data.

### Binding Model

Identical to PBR/GBuffer passes:
- **SM6.6**: Bindless vertex buffer via SRV
- **SM5**: Input Assembler with explicit vertex format

### Cascade Data (ShadowCascade)

```cpp
struct ShadowCascade {
    Mat4   lightViewProj;      ///< View-projection matrix from light's perspective
    float  nearPlane;          ///< Near clip plane (for bias computation)
    float  farPlane;           ///< Far clip plane
    float  splitDistance;      ///< Camera-space distance of cascade split
    float  _padding;           ///< Alignment
};
```

### Pass Execution

During execution the pass:

1. Clears the shadow atlas depth texture to 1.0
2. For each cascade (0 to cascadeCount-1):
   a. Computes viewport and scissor rectangle for the cascade's tile in the atlas
   b. Begins a depth-only render pass (0 color attachments, depth only)
   c. Binds the shadow pipeline, vertex/index buffers
   d. For each draw command:
      - Allocates PerObjectCB (world matrix for transformations)
      - Allocates ShadowCB with the cascade's light viewProj matrix
      - Binds at root parameters 2 and 3 respectively
      - Issues DrawIndexed
   e. Ends the render pass
3. Logs trace-level information with cascade count and total draw call count

### Viewport & Scissor Calculation

For cascade `i` in an atlas of `resolution×resolution` per cascade:

```cpp
// Cascade 0 (top-left)
viewport.x = 0, viewport.y = 0, viewport.w = resolution, viewport.h = resolution

// Cascade 1 (top-right)
viewport.x = resolution, viewport.y = 0, viewport.w = resolution, viewport.h = resolution

// Cascade 2 (bottom-left)
viewport.x = 0, viewport.y = resolution, viewport.w = resolution, viewport.h = resolution

// Cascade 3 (bottom-right)
viewport.x = resolution, viewport.y = resolution, viewport.w = resolution, viewport.h = resolution
```

Scissor rectangle matches the viewport.

### Usage Example

```cpp
ShadowPassDesc desc{};
desc.shadowAtlas         = builder.CreateDepthBuffer(atlasRes*2, atlasRes*2, RHIFormat::D32Float);
desc.shadowAtlasTexture  = physicalShadowAtlasHandle;
desc.drawList            = &frame.drawList;
desc.cbAllocator         = &cbRingAllocator;
desc.pipeline            = shadowPipeline;
desc.vertexBuffer        = vertexBufferHandle;
desc.indexBuffer         = indexBufferHandle;
desc.indexFormat         = RHIIndexFormat::Uint32;
desc.indexCount          = totalIndexCount;
desc.bindless            = isBindlessSupported;

// Fill cascades from lighting system
for (uint32_t i = 0; i < shadowCascadeCount; ++i) {
    desc.cascades[i] = shadowCascadeData[i];
}
desc.cascadeCount        = shadowCascadeCount;
desc.atlasResolution     = 1024;  // Per-cascade resolution

AddShadowPass(builder, desc);
```

---

## Atmosphere Passes — Sky & Fog

### Overview

The atmosphere rendering system consists of four passes that work together to compute realistic Bruneton-style atmospheric scattering and capture the sky environment for IBL:

1. **Transmittance LUT Pass** (precomputed at init) — 256×64 lookup table for transmittance
2. **Sky Atmosphere Pass** (per-frame) — Renders sky with precomputed transmittance, adds ground reflection
3. **Sky Cubemap Capture Pass** (per-frame) — Captures sky into 64×64×6 cubemap for IBL
4. **Height Fog Pass** (per-frame) — Blends exponential height fog over scene

All atmosphere passes use Z-up coordinates (UzEngine convention).

---

## Transmittance LUT Pass — Precomputed Lookup Table

**Header**: `runtime/engine/renderer/passes/atmosphere/transmittance_lut_pass.h`

### Purpose

Precomputes a 256×64 RGBA16F transmittance lookup table using Bruneton 2017 parameterization. This table encodes how light attenuates through the atmosphere as a function of:
- **θ (theta)**: View angle from zenith (0° = looking straight up, 90° = horizon)
- **Altitude**: Height above the planet surface (0 km to top of atmosphere)

The LUT is computed once at initialization and reused by `SkyAtmospherePass` and `SkyCubemapCapturePass` to avoid per-pixel ray marching.

### Configuration (TransmittanceLutPassDesc)

```cpp
struct TransmittanceLutPassDesc {
    rg::RgResourceId transmittanceLut;  ///< Output 256×64 RGBA16F texture
    RHITextureHandle lutTexture;        ///< Physical texture handle
    CBAllocation     atmosphereCB;      ///< Bruneton atmosphere parameters
};
```

### Computation Details

For each pixel at `(u, v)` in the 256×64 grid:
1. Decode height from `v`: lerp from ground (0 km) to top of atmosphere (~100 km)
2. Decode zenith angle from `u`: lerp from 0° (up) to 180° (nadir direction)
3. Cast a ray from that position and angle upward through the atmosphere
4. Integrate Rayleigh and Mie scattering coefficients along the ray path
5. Store the result as transmittance (RGB for three wavelengths, A unused)

### Render Graph Integration

The pass declares:
- **Transmittance LUT**: Write (UnorderedAccess for compute shader)

### Pass Execution

During execution:
1. Dispatches compute shader with grid size `ceil(256/8) × ceil(64/8) = 32×8` (8×8 thread groups)
2. Each thread computes one pixel of the LUT
3. Result stored in texture, ready for `SkyAtmospherePass` to read

### Shader

**File**: `runtime/shaders/runtime/atmosphere/transmittance_lut_cs.hlsl`

- **Root Constants**: Bruneton atmosphere parameters (planet radius, atm density, etc.)
- **Output**: `RWTexture2D<float4>` at u0
- **Sampler**: `linearSampler` (s0)

### Usage Example

```cpp
TransmittanceLutPassDesc desc{};
desc.transmittanceLut = builder.CreateRenderTarget(256, 64, RHIFormat::RGBA16Float);
desc.lutTexture       = physicalLUTHandle;
desc.atmosphereCB     = atmosphereCB;  // Pre-filled with Bruneton params

AddTransmittanceLutPass(builder, desc);
```

### Performance

- **Compute time**: 0.1–0.2 ms on modern GPUs
- **Texture size**: 256×64×8 bytes (RGBA16F) = 128 KB
- **Frequency**: Once per initialization (or per SkyAtmosphereComponent parameter change)

---

## Sky Atmosphere Pass — Rendering & IBL Capture

**Header**: `runtime/engine/renderer/passes/atmosphere/sky_atmosphere_pass.h`

### Purpose

Renders the sky atmosphere using Bruneton single-scattering, with:
- **Precomputed transmittance** from TransmittanceLUT (no per-pixel ray march)
- **Rayleigh and Mie scattering** for realistic color gradients
- **Sun disk** with configurable intensity and angular size
- **Ground reflection** using Lambertian model (sky color bounced from ground)
- **Captures result into 64×64×6 cubemap** for split-sum IBL

### Configuration (SkyAtmospherePassDesc)

```cpp
struct SkyAtmospherePassDesc {
    rg::RgResourceId      backbuffer;         ///< Output scene color
    const class SkyAtmosphereComponent* skyComp; ///< Parameters (sun dir, intensity, radius, etc.)
    RHITextureHandle      transmittanceLUT;   ///< 256×64 precomputed LUT
    CBAllocation          atmosphereCB;       ///< Bruneton parameters + sun direction
    RHIPipelineHandle     pipeline;           ///< PSO for sky shader
    RHIDescriptorHandle   transmittanceSrvIndex; ///< SRV index for LUT (bindless)
    RHITextureHandle      transmittanceLUTPhysical; ///< Physical LUT handle (SM5)
    bool                  bindless;           ///< SM6.6 or SM5
    float                 viewportWidth;      ///< Viewport dimensions
    float                 viewportHeight;
};
```

### Bruneton Atmosphere Parameters (SkyAtmosphereCB)

```cpp
struct SkyAtmosphereCB {  // 256 bytes (D3D12 aligned)
    float3 sunDirection;              ///< Light direction (normalized)
    float  sunIntensity;              ///< Luminance multiplier
    float  sunAngularRadius;          ///< Sun disk angle (degrees)
    float  planetRadius;              ///< Planet radius (km, typically 6371)
    float  atmosphereHeight;          ///< Atmosphere thickness (km, typically 100)
    float  rayleighScaleHeight;       ///< Rayleigh density scale (km, typically 8.5)
    float3 rayleighCoefficient;       ///< Wavelength absorption at sea level
    float  mieScaleHeight;            ///< Mie density scale (km, typically 1.2)
    float3 mieCoefficient;            ///< Mie scattering coefficient
    float  miePreferredScatteringDirection; ///< Phase function asymmetry (g)
    // ... padding to 64 bytes
};
```

### Sky Atmosphere Component

The `SkyAtmosphereComponent` (ECS) stores 50+ parameters:
- **Sun**: direction, intensity, angular radius
- **Atmosphere**: planet radius, top height, density scales
- **Rayleigh**: coefficient RGB, scale height
- **Mie**: coefficient RGB, scale height, phase asymmetry
- **Ground**: ground albedo RGB (for reflection)
- **Flags**: enable sky, enable ground, ground contribute to IBL

### Render Graph Integration

The pass declares:
- **Backbuffer**: Write (RenderTarget state)

### Pass Execution

During execution:
1. Clears the backbuffer (or blends over existing)
2. Begins a fullscreen render pass (no depth)
3. Binds the sky atmosphere PSO and transmittance LUT
4. Issues Draw(3) for fullscreen triangle (generated from SV_VertexID)
5. For each pixel:
   - Reconstructs world-space ray direction from screen position
   - Traces ray through atmosphere using precomputed transmittance LUT
   - Integrates Rayleigh and Mie scattering
   - Adds ground reflection (Lambertian) if view ray hits planet
   - Composites sun disk
   - Outputs final sky color

### Shader

**File**: `runtime/shaders/runtime/atmosphere/sky_atmosphere_ps.hlsl`

- **Root Parameter 0**: Atmosphere CB (b0)
- **Root Constant 0** (SM6.6): Transmittance LUT SRV index
- **Sampler s0**: `linearSampler`
- **Output**: Scene color in log-space for deferred composition

### Z-up Correction

All sky shaders are corrected for Z-up coordinates:
- View ray direction reconstructed with Z as up axis
- Sun direction expected in Z-up frame
- Zenith angle computed as `acos(rayDir.z)`
- Horizon detection uses `rayDir.z <= -sin(planetRadius/horizonDistance)`

### Usage Example

```cpp
SkyAtmospherePassDesc desc{};
desc.backbuffer               = builder.ImportRenderTarget(sceneColor);
desc.skyComp                  = skyComponent;  // From scene
desc.transmittanceLUT         = transmittanceLutResource;
desc.atmosphereCB             = atmosphereCB;  // Pre-filled from SkyAtmosphereComponent
desc.pipeline                 = skyAtmospherePipeline;
desc.transmittanceSrvIndex    = lutSrvIdx;
desc.transmittanceLUTPhysical = physicalLUTHandle;
desc.bindless                 = isBindlessSupported;
desc.viewportWidth            = renderWidth;
desc.viewportHeight           = renderHeight;

AddSkyAtmospherePass(builder, desc);
```

### Performance

- **Pixel cost**: 0.2–0.4 ms full 1080p (transmittance LUT lookup amortized)
- **Bindless path**: 1 root constant + 1 SRV index
- **SM5 path**: SetFragmentTexture for LUT

### IBL Capture

After rendering the sky, a separate `SkyCubemapCapturePass` captures the output into a 64×64×6 cubemap, which is then processed by the IBL baker to produce irradiance and specular cubemaps. This ensures the sky environment drives all ambient lighting in the scene.

---

## Sky Cubemap Capture Pass — IBL Environment Extraction

**Header**: `runtime/engine/renderer/passes/atmosphere/sky_cubemap_capture_pass.h`

### Purpose

Captures the rendered sky atmosphere into a 64×64×6 cubemap that serves as the IBL environment. This cubemap is then processed by the IBL baker to produce:
- **Irradiance cubemap** (32×32×6): Diffuse ambient lighting
- **Prefiltered specular** (128×128×6, 5 mips): Specular reflection

### Configuration (SkyCubemapCapturePassDesc)

```cpp
struct SkyCubemapCapturePassDesc {
    rg::RgResourceId      skyCubemap;     ///< Output 64×64×6 cubemap
    rg::RgResourceId      skyScene;       ///< Sky rendered image (fullscreen)
    const SkyAtmosphereComponent* skyComp;
    CBAllocation          atmosphereCB;   ///< Bruneton parameters
    RHIPipelineHandle     pipeline;       ///< Cubemap capture compute
};
```

### Capture Process

1. **Per-face dispatch**: For each of 6 cube faces (±X, ±Y, ±Z):
   - Dispatches compute shader grid `ceil(64/8) × ceil(64/8) = 8×8` threads per face
   - 64 threads per face, totaling 6 × 64 = 384 thread groups

2. **Ray direction computation**: Each thread at pixel `(px, py)` on face `f`:
   - Normalizes pixel coordinates to [-1, 1]
   - Converts face + UV to world-space ray direction
   - Traces ray through atmosphere using transmittance LUT
   - Stores result in cubemap at `(face, px, py)`

3. **Output**: 64×64×6 RGBA16F cubemap ready for IBL baker

### Render Graph Integration

The pass declares:
- **Sky Cubemap**: Write (UnorderedAccess)

### Pass Execution

During execution (compute shader):
1. Iterates 6 cube faces in parallel
2. For each face, dispatches 8×8 thread groups (8 threads per dimension, 64 per face)
3. Each thread computes atmosphere contribution for one cubemap pixel
4. Result written directly to cubemap UAV

### Shader

**File**: `runtime/shaders/runtime/atmosphere/sky_cubemap_capture_cs.hlsl`

- **Compute grid**: `(ceil(64/8), ceil(64/8), 6)` = (8, 8, 6) groups
- **Thread group size**: (8, 8, 1)
- **Output**: `RWTexture2DArray<float4>` cubemap

### Usage Example

```cpp
SkyCubemapCapturePassDesc desc{};
desc.skyCubemap  = builder.CreateTexture(64, 64, 6, RHIFormat::RGBA16Float);
desc.skyScene    = skySceneResource;  // From SkyAtmospherePass output
desc.skyComp     = skyComponent;
desc.atmosphereCB = atmosphereCB;
desc.pipeline    = skyCubemapPipeline;

AddSkyCubemapCapturePass(builder, desc);

// After this pass, feed cubemap to IBL baker:
iblBaker.CaptureCubemap(skyCubemapResource);
iblBaker.BakeIrradiance();
iblBaker.BakePrefiltered();
```

### Performance

- **Compute time**: 0.05–0.1 ms for 6×64×64 = 24K pixels
- **Texture size**: 64×64×6×8 bytes = 192 KB
- **Frequency**: Every frame (or when sky changes)

---

## Height Fog Pass — Atmospheric Fog Blending

**Header**: `runtime/engine/renderer/passes/atmosphere/height_fog_pass.h`

### Purpose

Applies exponential height-based fog to the rendered scene. Fog density decreases exponentially with altitude, creating a realistic atmospheric haze layer. Typical uses:
- Ground-level mist that obscures distant geometry
- Atmosphere haze that fades to sky color
- Volumetric effect without compute overhead

### Configuration (HeightFogPassDesc)

```cpp
struct HeightFogPassDesc {
    rg::RgResourceId    sceneColor;      ///< Input scene color (fullscreen)
    rg::RgResourceId    output;          ///< Output fogged color
    const class HeightFogComponent* fogComp; ///< Fog parameters
    CBAllocation        fogCB;           ///< Fog constants
    RHIPipelineHandle   pipeline;        ///< PSO for fog shader
    float               viewportWidth;   ///< Viewport dimensions
    float               viewportHeight;
};
```

### Height Fog Component Parameters (HeightFogCB)

```cpp
struct HeightFogCB {  // 256 bytes (D3D12 aligned)
    float3 fogColor;              ///< Fog tint color (RGB)
    float  fogDensity;            ///< Exponential fog density (0.0–1.0)
    float  baseHeight;            ///< World Z height where fog starts
    float  heightFalloff;         ///< Exponential decay scale (1/km)
    float  startDistance;         ///< Minimum camera distance before fog applies
    float  maxOpacity;            ///< Clamp fog opacity (0.0–1.0)
    // ... padding to 64 bytes
};
```

### HeightFogComponent

The `HeightFogComponent` (ECS) stores:
- **Color**: Fog tint RGB (typically sky color)
- **Density**: 0.0 (no fog) to 1.0 (opaque)
- **Base height**: Z coordinate where fog starts (e.g., sea level = 0)
- **Height falloff**: How quickly fog density increases with descent
- **Start distance**: Min camera distance before fog activates
- **Max opacity**: Clamp fog strength to avoid complete black
- **Enabled**: Toggle fog on/off

### Render Graph Integration

The pass declares:
- **Scene Color**: Read (ShaderResource)
- **Output**: Write (RenderTarget)

### Pass Execution

During execution:
1. Clears output (or blends over backbuffer)
2. Binds the fog PSO and scene color texture
3. Issues Draw(3) for fullscreen triangle
4. For each pixel:
   - Reconstructs world-space position from depth (uses invViewProj)
   - Computes fog density: `exp(-heightFalloff × (worldZ - baseHeight))`
   - Clamps to `[0, maxOpacity]`
   - Lerps scene color toward fog color: `lerp(sceneColor, fogColor, fogDensity)`
   - Outputs fogged result

### Shader

**File**: `runtime/shaders/runtime/atmosphere/height_fog_ps.hlsl`

- **Root Parameter 0**: Fog CB (b0)
- **Root Constant 0** (SM6.6): Scene color SRV index
- **Sampler s0**: `linearSampler`
- **Input**: Scene color texture
- **Output**: Fogged color

### Z-up Note

Fog density is computed as function of world Z coordinate (vertical axis in UzEngine). Base height and start distance are in world units.

### Usage Example

```cpp
HeightFogPassDesc desc{};
desc.sceneColor      = builder.ImportRenderTarget(sceneColorResource);
desc.output          = builder.CreateRenderTarget(w, h, RHIFormat::RGBA16Float);
desc.fogComp         = heightFogComponent;  // From scene
desc.fogCB           = fogCB;  // Pre-filled from HeightFogComponent
desc.pipeline        = heightFogPipeline;
desc.viewportWidth   = renderWidth;
desc.viewportHeight  = renderHeight;

AddHeightFogPass(builder, desc);
```

### Performance

- **Pixel cost**: 0.05–0.1 ms full 1080p (simple exponential)
- **Texture read**: Single fullscreen scene color texture
- **Memory**: 256-byte constant buffer per frame

### Integration with Sky Atmosphere

Height fog is typically applied after sky atmosphere, so the fog color blends ground fog with distant sky. For best results:
1. Render opaque geometry (GBuffer or forward)
2. Apply lighting (Deferred lighting or forward shading)
3. Render sky atmosphere (captures background + sky reflection)
4. Apply height fog (blends ground mist)
5. Apply post-processing (tone-mapping, color grading, etc.)

---

## Shader Model Compatibility

### SM6.6 Bindless Path

All passes support SM6.6 bindless descriptor indexing:
- Vertex buffer fetched via SRV with descriptor index in root constant
- No Input Assembler state
- Enables descriptor heap consolidation

### SM5 Fallback Path

All passes automatically fall back to SM5-compatible binding:
- Explicit Input Assembler vertex format (RHI translates to D3D12)
- Textures bound via SetVertexBuffer/SetFragmentTexture
- Root constants used for index offsets rather than descriptor indices

The RHI backend handles the translation transparently based on `bindless` flag at pass registration.

---

## Performance Considerations

### PBR Pass

- **Best for**: Few lights, few materials, forward transparency compositing
- **Cost**: Single geometry pass + single lit output
- **Optimization**: Sort keys minimize PSO switches; per-object CB ring buffer avoids GPU stalls

### Deferred (GBuffer + Lighting)

- **Best for**: Many lights, many pixels, opaque-dominant scenes
- **Cost**: Two geometry passes + one composition pass (overhead if few lights)
- **Optimization**: G-Buffer writes are inexpensive (simple pixel shaders); lighting pass is fullscreen (no vertex overhead)

### Shadow Pass

- **Best for**: Directional lights with cascades
- **Cost**: One depth-only pass per cascade (typically 4 cascades = 4× geometry iteration)
- **Optimization**: Depth-only shaders are fast; atlas packing minimizes memory bandwidth

### Triple-Buffered Ring Allocators

All passes use `CBRingAllocator` for per-object constant buffers:
- Each frame triple-buffers the CB ring to avoid GPU stalls
- Per-draw allocation is O(1) with no synchronization overhead
- Large enough rings prevent buffer overruns

---

## Best Practices

### Register a Pass

1. Create the descriptor struct (PBRPassDesc, etc.)
2. Fill all required fields from your scene/renderer state
3. Call AddPass(builder, desc) before RgBuilder::Execute()
4. The render graph schedules transitions and executes the pass

### Constant Buffers

- **Per-frame**: Filled once per frame (camera, lighting, time) before any pass registration
- **Per-object**: Allocated fresh for each draw command to avoid write-after-read hazards
- **Shadow**: Filled per cascade with light view-projection matrices

### DrawList Sorting

- PBR Pass: Sort by pipeline ID, material, mesh (opaque front-to-back)
- GBuffer Pass: Same sorting as PBR
- Shadow Pass: Sort similarly within each cascade
- Lighting Pass: No sorting needed (fullscreen triangle)

### Thread Safety

All passes are **NOT thread-safe**. Register passes from the render thread only. Do not modify descriptors or allocators during pass execution.

---

## Related Documentation

- [Render Graph](04-render-graph.md) — Pass registration and resource management
- [PBR Material System](06-pbr-material-system.md) — Material struct and GPU upload
- [Sort Key Architecture](07-sort-key-architecture.md) — Draw call ordering
- [Shadow System](18-shadow-system.md) — Cascade generation and PCF filtering
- [Shader Compatibility](07-shader-compatibility.md) — SM6.6 vs SM5 binding models

---

## File Manifest

```
renderer/passes/
├── geometry/
│   ├── pbr_pass.h                    # Forward PBR pass declaration
│   ├── pbr_pass.cpp                  # Forward PBR implementation
│   ├── gbuffer_pass.h                # Deferred GBuffer pass declaration
│   └── gbuffer_pass.cpp              # Deferred GBuffer implementation
├── lighting/
│   ├── lighting_pass.h               # Deferred lighting pass declaration (with IBL)
│   ├── lighting_pass.cpp             # Deferred lighting implementation
│   ├── shadow_pass.h                 # Shadow depth pass declaration
│   └── shadow_pass.cpp               # Shadow depth implementation
├── atmosphere/
│   ├── transmittance_lut_pass.h      # Precompute transmittance LUT (compute)
│   ├── transmittance_lut_pass.cpp    # Implementation
│   ├── sky_atmosphere_pass.h         # Bruneton sky rendering (fullscreen triangle)
│   ├── sky_atmosphere_pass.cpp       # Implementation
│   ├── sky_cubemap_capture_pass.h    # Cubemap capture for IBL (compute)
│   ├── sky_cubemap_capture_pass.cpp  # Implementation
│   ├── height_fog_pass.h             # Exponential fog blending (fullscreen triangle)
│   └── height_fog_pass.cpp           # Implementation
├── postprocess/
│   ├── postprocess_pass.h            # Post-processing chain
│   ├── postprocess_pass.cpp          # Implementation
│   ├── velocity_pass.h               # Motion vector generation
│   └── velocity_pass.cpp             # Implementation
└── debug/
    ├── debug_view_pass.h             # G-Buffer visualization (fullscreen triangle)
    ├── debug_view_pass.cpp           # Implementation
    ├── gbuffer_debug_pass.h          # Legacy — deprecated, use debug_view_pass
    └── gbuffer_debug_pass.cpp        # Legacy implementation
```
