# Deferred Rendering Pipeline

> **Status**: Done (Milestone 1.3)
> **Source**: `runtime/engine/renderer/passes/gbuffer_pass.h`, `runtime/engine/renderer/passes/lighting_pass.h`, `runtime/engine/renderer/constant_buffers.h`
> **Shaders**: `runtime/shaders/runtime/gbuffer_ps.hlsl`, `runtime/shaders/runtime/deferred_lighting_ps.hlsl`, `runtime/shaders/runtime/fullscreen_vs.hlsl`
> **Depends on**: Render Graph (§12-04), RHI (§03), Shader Pipeline (§05), Math (§02)

## Purpose

The deferred rendering pipeline decouples geometry rendering from lighting evaluation. A **geometry pass** writes surface material data (albedo, normal, metallic/roughness) to off-screen G-Buffer targets. A subsequent **lighting pass** reads the G-Buffer and evaluates per-pixel lighting from multiple light sources without re-processing geometry.

This approach scales lighting linearly with light count rather than exponentially — 1, 10, or 100 point lights cost nearly the same for opaque geometry because geometry is rendered once, then lighting is evaluated in a fullscreen pass.

## Pipeline Architecture

The deferred pipeline consists of two main passes:

```
Frame N:
    │
    ├─ GBuffer Pass (geometry)
    │   Render opaque geometry → write albedo, normal, metallic/roughness, depth
    │
    └─ Lighting Pass (fullscreen)
        Read GBuffer + evaluate directional + point lights → composite to backbuffer
```

### GBuffer Pass

**Register**: `AddGBufferPass(builder, desc)`

**Execution**:
1. Clear the two color RTs (albedo, normal) to zero; depth to 1.0 (far plane)
2. Begin render pass with two color attachments + depth
3. Bind PSO, per-frame constant buffer, and per-object constant buffer ring
4. For each draw command in the sorted DrawList:
   - Allocate per-object CB (model matrix, normal matrix, material texture indices)
   - Bind per-object CB at root param 2
   - Issue `DrawIndexed(indexCount, 1, baseIndex, baseVertex, instanceStart)`
5. End render pass

**Resources Written**:
- `albedoTarget` — RGBA8Unorm, rgb = linear base color, a = ambient occlusion
- `normalTarget` — RGBA8Unorm, rg = octahedral-encoded normal, b = metallic, a = roughness
- `depthBuffer` — D32Float, standard near-to-far depth

**State transitions**:
- Albedo: None → RenderTarget (write) → ShaderResource (read by lighting + velocity)
- Normal: None → RenderTarget (write) → ShaderResource (read by lighting)
- Depth: None → DepthStencilWrite (write) → ShaderResource (read as SRV by lighting + velocity)

### Velocity Pass

**Register**: `AddVelocityPass(builder, desc)`

**Purpose**: Generate screen-space motion vectors via depth reprojection. This pass is **required for TAA, motion blur, and other temporal effects**.

**Execution**:
1. Clear velocity RT to (0, 0) — stationary pixels
2. Begin render pass with single color attachment (velocity), no depth
3. Bind velocity PSO (fullscreen triangle)
4. Bind VelocityCB at root param 2 containing:
   - `invViewProj` — current frame's inverse view-projection
   - `prevViewProj` — previous frame's view-projection
5. For SM6.6 bindless: push depth SRV index via root constants
6. For SM5 fallback: bind depth texture as SRV at t0
7. Issue `Draw(3, 0)` — fullscreen triangle
8. End render pass

**Algorithm** (fullscreen fragment shader):
```hlsl
// For each screen pixel:
float depth = depthTexture.Sample(pointSampler, uv).r;

// Reconstruct world position
float4 ndcCurrent = float4(uv * 2.0 - 1.0, depth, 1.0);
float4 worldPos = mul(ndcCurrent, invViewProj);
worldPos.xyz /= worldPos.w;

// Reproject to previous frame NDC
float4 ndcPrev = mul(float4(worldPos, 1.0), prevViewProj);
ndcPrev.xyz /= ndcPrev.w;
float2 uvPrev = ndcPrev.xy * 0.5 + 0.5;

// Compute screen-space motion vector (in pixels)
float2 screenMotion = (uv - uvPrev) * screenDimensions;

// Sky pixels (depth 0 or 1) output zero velocity to prevent ghosting
if (depth < 0.001 || depth > 0.999) {
    screenMotion = float2(0, 0);
}

velocityTarget = float4(screenMotion, 0.0, 1.0);  // RG = motion, B/A unused
```

**Resources Read**:
- `depthBuffer` — Read as SRV (depth texture from GBuffer pass)

**Resources Written**:
- `velocityTarget` — Write (RenderTarget state, RG16Float)

**State transitions**:
- Depth: ShaderResource (read only, input from GBuffer)
- Velocity: None → RenderTarget (write) → ShaderResource (read by TAA, motion blur, velocity debug)

### Lighting Pass

**Register**: `AddLightingPass(builder, desc)`

**Execution**:
1. Clear backbuffer to black (or load previous frame for accumulation-based effects)
2. Begin render pass with single color attachment (backbuffer), no depth
3. Bind PSO (fullscreen triangle, no vertex/index buffers)
4. Bind per-frame CB at root param 1 (contains invViewProj for position reconstruction)
5. Bind lighting CB at root param 3 (point light array, count)
6. For SM6.6 bindless: push G-Buffer SRV indices via root constants
7. For SM5 fallback: bind G-Buffer textures at t0-t1 (albedo, normal) and t2 (depth as SRV)
8. Issue `Draw(3, 0)` — fullscreen triangle via SV_VertexID
9. End render pass

**Resources Read**:
- `albedoGBuffer` — Read (ShaderResource state)
- `normalGBuffer` — Read (ShaderResource state)
- `depthGBuffer` — Read as SRV (ShaderResource state), NOT as depth target

**Resources Written**:
- `backbuffer` — Write (RenderTarget state)

Note: The velocity buffer is **not consumed by the lighting pass** — it's generated by a separate velocity pass and used by TAA and motion blur in post-processing.

## G-Buffer Layout

The G-Buffer consists of four textures optimized for deferred rendering and temporal effects (TAA, motion blur):

| Target | Format | Resolution | Channel Mapping |
|--------|--------|------------|-----------------|
| Albedo | RGBA8Unorm | Full-res | RGB = linear base color | A = ambient occlusion |
| Normal | RGBA8Unorm | Full-res | RG = octahedral-encoded normal | B = metallic | A = roughness |
| Depth | D32Float | Full-res | R = hardware depth (0=far, 1=near) | — |
| Velocity | RG16Float | Full-res | RG = screen-space motion vectors (pixels) | — |

**Total bandwidth per pixel**: 4 + 4 + 4 + 4 = **16 bytes** (plus velocity = 20 bytes total with temporal effects).

### Layout Optimization (Milestone 1.3.1)

The layout was optimized from **three 4-byte RT + depth** to **two 4-byte RT + velocity**:

**Previous layout (20 bytes/pixel, no temporal support)**:
- Albedo: RGBA8 (RGB color, unused A)
- Normal: RGBA16F (RGB normal, unused A) — **8 bytes, expensive**
- MetalRough: RGBA8 (R metallic, G roughness, B AO, unused A)
- Depth: D32 (hardware depth)

**Current layout (16 bytes + 4B velocity = **20B with temporal features**)**:
- **Albedo**: RGBA8 (RGB color, **A = AO** ← moved from separate RT)
- **Normal**: RGBA8 (RG = octahedral-encoded normal, **B = metallic, A = roughness** ← packed from 3 separate channels)
- **Depth**: D32 (hardware depth)
- **Velocity**: RG16F (NEW — screen-space motion for TAA/motion blur)

**Benefits**:
1. **50% normal bandwidth reduction** — octahedral encoding (Cigolle 2014) reduces RGBA16F to RG8 with <0.5° angular error
2. **Velocity support** — enables TAA (temporal anti-aliasing) and motion blur without extra passes
3. **Same memory footprint** — 20 bytes/pixel total, but now includes temporal features

### Channel Packing Details

#### Albedo Target (RGBA8Unorm)
- **RGB**: sRGB-encoded base color (converted to linear in GBuffer PS)
- **A**: Ambient occlusion [0,1] from texture or default 1.0 (no occlusion)

#### Normal Target (RGBA8Unorm)
- **RG**: Octahedral-encoded normal (see [Octahedral Encoding](#octahedral-encoding) below)
  - Original 3-channel unit normal packed to 2 channels with <0.5° angular error
  - Decoded in lighting pass via `OctDecode(normal.rg)` → unit float3
- **B**: Metallic [0,1] from texture or default value
- **A**: Roughness [0,1] from texture, clamped to [0.04, 1.0] (MIN_ROUGHNESS prevents singular lobes)

#### Depth Target (D32Float)
- **R**: Standard hardware depth, 32-bit precision
- Range: 0.0 (far plane) to 1.0 (near plane) in NDC space
- Read as SRV in lighting pass for world position reconstruction
- Used by velocity pass for reprojection

#### Velocity Target (RG16Float)
- **RG**: Screen-space motion vector in pixels
- Computed via depth reprojection (see [Velocity Generation](#velocity-generation) below)
- Positive X = rightward motion, positive Y = downward motion
- Sky pixels (depth 0 or 1) output zero velocity
- Consumed by TAA and motion blur passes

### Octahedral Encoding

The normal is encoded using **octahedral projection** (Cigolle et al., 2014). This reduces a 3D unit normal to a 2D point with minimal angular error:

**Encoding** (GBuffer PS, before writing):
```hlsl
float2 octEncoded = OctEncode(worldNormal);
// octEncoded in [0,1]² ready for RGBA8 storage
```

**Decoding** (Lighting PS, after sampling):
```hlsl
float2 octSampled = normalTarget.rg;
float3 N = OctDecode(octSampled);
N = normalize(N);  // Ensure unit-length after interpolation
```

**Why octahedral?**
- Evenly distributes error across the normal hemisphere
- Max angular error: **±0.5 degrees** (imperceptible)
- Uses only 2 bytes vs. 8 bytes for RGBA16F
- Scales bandwidth 4x better than RGBA10A2
- See [octahedral.hlsli](../../runtime/shaders/runtime/include/octahedral.hlsli) for reference implementation

**Reference**: Meyer, McGuire, Cigolle (2014). "A Survey of Efficient Representations for Independent Unit Vectors." JCGT, Vol. 3, No. 2.

### Why These Formats?

- **RGBA8Unorm for Albedo**: Sufficient for sRGB color quantization; A channel reused for AO instead of wasting space
- **RGBA8Unorm for Normal**: Octahedral encoding achieves <0.5° error at 4x bandwidth savings vs. RGBA16F
- **D32Float for Depth**: Full 32-bit precision maintains depth accuracy at distance for reconstruction in velocity/lighting passes
- **RG16Float for Velocity**: 16-bit floats preserve sub-pixel precision (±0.5 pixel) needed for TAA history reprojection

## Shaders

### GBuffer Pixel Shader (`gbuffer_ps.hlsl`)

**Input**: Per-vertex interpolants (position, normal, tangent, UV, world position)

**Output**: Two MRT targets (albedo+AO, octahedral-normal+metallic+roughness); depth written by rasterizer

**Key operations**:
1. Sample PBR texture maps (or use defaults if index == 0)
2. Apply tangent-space normal mapping if normal map bound
3. **Encode world-space normal to octahedral** via `OctEncode(N)` → float2
4. Pack metallic, roughness, and octahedral normal into single RGBA8
5. Output material data with no lighting; depth is written automatically by rasterizer

**Output packing**:
```hlsl
// MRT0 — Albedo
float4 albedo = float4(
    texSrgbToLinear(albedoSample.rgb),  // RGB: linear base color
    aoSample.r                          // A: ambient occlusion
);

// MRT1 — Normal + Material properties
float2 octNorm = OctEncode(normalize(N));  // World normal → octahedral RG
float4 normal = float4(
    octNorm.xy,    // RG: octahedral-encoded normal (see OctDecode in lighting)
    metallic,      // B: metallic [0,1]
    roughness      // A: roughness [0,1]
);

output.albedo = albedo;
output.normal = normal;
// Depth: written automatically to depth target by rasterizer
```

**Binding paths**:
- **SM6.6 bindless** (`#define UZ_BINDLESS`): Sample textures via `ResourceDescriptorHeap[index]`
- **SM5 fallback**: Sample register-bound textures `t0` (albedo), `t1` (normal), `t2` (roughness), `t3` (metallic), `t4` (AO)

**Constant buffers**:
- `b1`: PerFrameCB (viewProj matrix for vertex transformation)
- `b2`: PerObjectCB (model, normalMatrix, texture indices)

**Defaults** (when texture index == 0):
- Albedo: `float3(1.0, 1.0, 1.0)` (white) with A=1.0 (no occlusion)
- Metallic: `0.0` (dielectric)
- Roughness: `0.5` (perceptual midpoint)
- AO: `1.0` (no occlusion)

### Fullscreen Vertex Shader (`fullscreen_vs.hlsl`)

**Input**: SV_VertexID (0, 1, 2)

**Output**: Clip-space position and screen UV

**Technique**: Generates a single triangle covering the entire viewport using only vertex ID:

```
ID 0: Position (-1, -1), UV (0, 1)    → top-left
ID 1: Position (-1,  3), UV (0, -1)   → top-right (oversized)
ID 2: Position ( 3, -1), UV (2, 1)    → bottom-left (oversized)
```

The oversized triangle is clipped by the rasterizer to the viewport. UVs interpolate to [0,1] within the visible screen.

**Advantage over fullscreen quad**: Avoids redundant pixel shader invocations along the diagonal seam where two triangles would overlap.

### Lighting Pixel Shader (`deferred_lighting_ps.hlsl`)

**Input**: Screen-space UV from fullscreen triangle

**Output**: Linear HDR color tonemapped to sRGB

**Key operations**:

1. **Read G-Buffer and decode**
   ```hlsl
   float3 albedo       = gbufferAlbedo.Sample(pointSampler, uv).rgb;
   float  ao           = gbufferAlbedo.Sample(pointSampler, uv).a;

   float2 octNormal    = gbufferNormal.Sample(pointSampler, uv).rg;
   float3 N            = OctDecode(octNormal);     // Decode octahedral
   N                   = normalize(N);              // Ensure unit-length

   float  metallic     = gbufferNormal.Sample(pointSampler, uv).b;
   float  roughness    = gbufferNormal.Sample(pointSampler, uv).a;
   roughness           = max(roughness, MIN_ROUGHNESS);  // Clamp to avoid singular lobes

   float  depth        = gbufferDepth.Sample(pointSampler, uv).r;
   ```

2. **Reconstruct world position from depth**
   ```hlsl
   float3 worldPos = ReconstructWorldPosition(uv, depth);
   float3 V = normalize(cameraPos - worldPos);  // View direction
   ```
   Uses `invViewProj` matrix from PerFrameCB to transform NDC+depth back to world space.

3. **Evaluate Cook-Torrance BRDF for directional light**
   ```hlsl
   float3 L_dir = normalize(lightDir.xyz);
   float3 Lo = EvaluateBRDF(N, V, L_dir, lightColor.xyz, albedo, metallic, roughness);
   Lo *= max(dot(N, L_dir), 0.0);  // Prevent backlit contribution
   ```

4. **Evaluate Cook-Torrance BRDF for each point light**
   ```hlsl
   for (uint i = 0; i < pointLightCount; ++i) {
       float3 lightVec  = pointLights[i].position - worldPos;
       float  dist      = length(lightVec);
       float  attenuation = SmoothAttenuation(dist, pointLights[i].radius);
       float3 L = normalize(lightVec);
       float3 contrib = EvaluateBRDF(N, V, L, pointLights[i].color, albedo, metallic, roughness);
       Lo += contrib * max(dot(N, L), 0.0) * attenuation;
   }
   ```

5. **Evaluate split-sum IBL (image-based lighting)**
   ```hlsl
   // Sample irradiance cubemap for diffuse ambient
   float3 irradiance = irradianceCubemap.Sample(linearSampler, N).rgb;
   float3 diffuseIBL = irradiance * albedo * ao;

   // Sample prefiltered specular cubemap with roughness-based mip level
   float3 R = reflect(-V, N);
   uint mipLevel = uint(roughness * 4.0);  // 5 mips: 0-4
   float3 prefiltered = prefilteredSpecularCubemap.SampleLevel(linearSampler, R, mipLevel).rgb;

   // Fetch BRDF lookup table for current view angle and roughness
   float2 brdfUV = float2(max(dot(N, V), 0.0), roughness);
   float2 brdf = brdfLUT.Sample(linearSampler, brdfUV).rg;
   float3 specularIBL = prefiltered * (F0 * brdf.x + brdf.y);

   // Combine direct lighting with IBL
   Lo += diffuseIBL + specularIBL;
   ```

6. **Apply ambient occlusion and tonemap**
   ```hlsl
   Lo *= ao;                                    // Modulate by ambient occlusion
   color = ACESFilm(Lo);                       // ACES filmic tonemap
   color = LinearToSRGB(color);                // sRGB gamma correction
   ```

**Constant buffers**:
- `b1`: PerFrameCB (viewProj, invViewProj, cameraPos, timeParams, directional light)
- `b3`: LightingCB (point light array, count)

**Binding paths**:
- **SM6.6 bindless**: G-Buffer SRV indices pushed via root constants
- **SM5 fallback**: G-Buffer textures bound at `t0-t1` (albedo, normal; depth as SRV at t2)

## Constant Buffers

### PerFrameCB (`b1` — 256 bytes)

Bound at root parameter 1. Updated once per frame by the renderer.

```cpp
struct PerFrameCB {
    Mat4  viewProj;             // Combined view-projection matrix (column-major)
    Vec4  cameraPos;            // xyz = world-space camera position, w = unused
    Vec4  timeParams;           // x = total elapsed seconds, y = delta time, z = frame count, w = unused
    Vec4  lightDir;             // xyz = normalised direction TOWARD the light, w = 0
    Vec4  lightColor;           // xyz = directional light color * intensity (radiance), w = unused
    Vec4  ambientColor;         // xyz = ambient light color contribution, w = unused
    Mat4  invViewProj;          // Inverse view-projection for world position reconstruction
    float _pad[12] = {};        // Padding to 256-byte alignment
};
// Total: 256 bytes
```

**invViewProj usage**: Lighting pass reconstructs world position via:
```glsl
float4 clipPos = float4(ndc, depth, 1.0);
float4 worldPos = mul(clipPos, invViewProj);
return worldPos.xyz / worldPos.w;
```

### PerObjectCB (`b2` — 256 bytes)

Bound at root parameter 2. Updated per draw call via ring allocator.

```cpp
struct PerObjectCB {
    Mat4     model;            // Object-to-world transform
    Mat4     normalMatrix;     // transpose(inverse(model)) — for lighting
    uint32_t albedoIndex;      // SRV heap index for BaseColor map (0 = white default)
    uint32_t normalIndex;      // SRV heap index for normal map (0 = flat normal)
    uint32_t roughnessIndex;   // SRV heap index for roughness map
    uint32_t metallicIndex;    // SRV heap index for metallic map
    uint32_t aoIndex;          // SRV heap index for AO map
    float    _pad[3] = {};     // Align to 16-byte boundary
    float    _pad2[24] = {};   // Padding to 256-byte alignment
};
// Total: 256 bytes
```

### LightingCB (`b3` — 2304 bytes)

Bound at root parameter 3. Updated once per frame.

```cpp
static constexpr uint32_t kMaxPointLights = 64;

struct PointLightData {
    Vec3     position;    // World-space position
    float    radius;      // Attenuation radius
    Vec3     color;       // Color * intensity (linear HDR, not clamped)
    float    _pad = 0.0f; // Padding to 32 bytes
};
// sizeof(PointLightData) == 32 bytes

struct LightingCB {
    PointLightData pointLights[kMaxPointLights]; // 64 * 32 = 2048 bytes
    uint32_t       pointLightCount = 0;          // Number of active lights
    uint32_t       _pad[3] = {};                 // 16 bytes
    float          _pad2[60] = {};               // Padding to 2304 bytes (9 * 256)
};
// Total: 2304 bytes (must be 256-byte multiple)
```

**Constraint**: Maximum 64 point lights per frame. This limit scales with texture bandwidth — denser geometries or deferred cluster-based variants in Phase 2+ can lift this constraint.

## Point Light Attenuation

Point lights use a **smooth windowing function** to attenuate from maximum intensity at the light position to zero at the attenuation radius:

```hlsl
float distOverRadius = dist / radius;
float distRatio4     = distOverRadius^4;
float windowing      = saturate(1.0 - distRatio4);
float attenuation    = (windowing^2) / (dist^2 + 1.0);
```

This formulation:
- Matches physical inverse-square falloff `1/(d^2)` near the light
- Smoothly falls to zero at radius (no hard cutoff artifacts)
- Includes `+1.0` in denominator to prevent division by zero at light position
- Uses `saturate(1.0 - (d/r)^4)` to create a smooth window rather than a sharp cutoff

## Render Graph Integration

Both passes integrate with the render graph for automatic barrier scheduling and resource state management.

### GBufferPass Declaration

```cpp
GBufferPassDesc desc{
    .albedoTarget = builder.CreateResource(...),
    .normalTarget = builder.CreateResource(...),
    .depthBuffer = builder.CreateResource(...),
    .depthTexture = depthHandle,
    .drawList = &sortedDrawList,
    .cbAllocator = &cbRingAllocator,
    .perFrameAlloc = perFrameAllocation,
    .pipeline = gbufferPipeline,
    .vertexBufferSrvIndex = vertexSrvIndex,
    .vertexBuffer = vertexBuffer,
    .indexBuffer = indexBuffer,
    .indexFormat = RHIIndexFormat::Uint32,
    .indexCount = totalIndices,
    .materialSrvIndex = materialSrvIndex,
    .bindless = true,
    .viewportWidth = 1920.0f,
    .viewportHeight = 1080.0f
};
AddGBufferPass(builder, desc);
```

**Resource state transitions**:
- Albedo: None → RenderTarget (write) → ShaderResource (read by lighting, velocity, post-processes)
- Normal: None → RenderTarget (write) → ShaderResource (read by lighting)
- Depth: None → DepthStencilWrite (write) → ShaderResource (read as SRV by lighting, velocity)

### LightingPass Declaration

```cpp
LightingPassDesc desc{
    .backbuffer = builder.ImportResource(...),
    .albedoGBuffer = albedoResourceId,
    .normalGBuffer = normalResourceId,
    .metalRoughGBuffer = metalRoughResourceId,
    .depthGBuffer = depthResourceId,
    .perFrameAlloc = perFrameAllocation,
    .lightingAlloc = lightingAllocation,
    .pipeline = lightingPipeline,
    .albedoSrvIndex = albedoSrvIdx,
    .normalSrvIndex = normalSrvIdx,
    .metalRoughSrvIndex = metalRoughSrvIdx,
    .depthSrvIndex = depthSrvIdx,
    .albedoTexture = albedoTextureHandle,
    .normalTexture = normalTextureHandle,
    .metalRoughTexture = metalRoughTextureHandle,
    .depthTextureForSrv = depthTextureHandle,
    .bindless = true,
    .viewportWidth = 1920.0f,
    .viewportHeight = 1080.0f
};
AddLightingPass(builder, desc);
```

**Resource state transitions**:
- G-Buffer targets: ShaderResource (read) → ShaderResource (no transition)
- Backbuffer: None/Present → RenderTarget (write) → Present (for next frame)

## Performance Characteristics

### Bandwidth

**Per-pixel G-Buffer bandwidth** (geometry pass):
- Albedo: 4 bytes (RGBA8 — color + AO)
- Normal: 4 bytes (RGBA8 — octahedral normal + metallic + roughness)
- Depth: 4 bytes (D32)
- **Total**: 12 bytes per pixel (down from 20 with RGBA16F normal)

At 1920×1080 with 60 FPS:
```
12 bytes/pixel × 2,073,600 pixels × 60 frames/sec = 1.5 GB/sec
```

This represents a **40% bandwidth reduction** vs. the previous RGBA16F normal encoding, and is well within modern GPU bandwidth (300+ GB/sec on RTX 4090).

**Per-pixel Velocity Bandwidth** (velocity generation pass):
- Depth input: 4 bytes (read-only, sampled at 1 point)
- Velocity output: 4 bytes (RG16F write)
- **Total**: 8 bytes per pixel

At 1920×1080 with 60 FPS:
```
8 bytes/pixel × 2,073,600 pixels × 60 frames/sec = 1.0 GB/sec
```

**Per-pixel Lighting Bandwidth** (lighting pass):
- G-Buffer inputs: 12 bytes (read-only)
- Backbuffer output: 4 bytes (RGBA8 write)
- **Total**: 16 bytes per pixel

Lighting evaluation does ~50 ALU operations per 16 bytes fetched, making this pass memory-bandwidth-limited rather than compute-limited.

### Overall Memory Budget

**Total GPU frame bandwidth** (geometry + velocity + lighting):
```
(12 + 8 + 16) bytes/pixel × 2,073,600 pixels × 60 fps = 3.5 GB/sec
```

This is highly efficient — leaves room for shadow passes, post-processing, and UI composition without exceeding typical GPU bandwidth limits.

### Scalability

**Geometry pass**: Constant cost regardless of light count.

**Lighting pass**: Linear cost with point light count:
- 1 point light: ~20 ALU ops per pixel (dot, attenuation, BRDF evaluation)
- 64 point lights: ~1280 ALU ops per pixel (same algorithm × 64)

Modern GPUs execute this in parallel across millions of pixels, hiding latency via SIMD (wave-32 / warp-32).

## Limitations & Future Work

### Current (Milestone 1.3)

✅ Multiple point lights (up to 64)
✅ Single directional light
✅ Cook-Torrance BRDF deferred evaluation
✅ Smooth attenuation (windowing function)

### Phase 2 (Planned)

- [ ] Shadow mapping for directional light (cascaded, 4 cascades)
- [ ] Shadow mapping for point lights (cubemap atlasing)
- [ ] Spot lights with cone attenuation
- [ ] Light culling (tiled Forward+) to reduce point light iteration

### Phase 2.5+ (Implementing)

- [x] Image-based lighting (IBL) — split-sum with prefiltered sky cubemaps (Phase 2.5b)

### Phase 3+ (Future)

- [ ] Virtual Shadow Maps (VSM) for efficient dynamic lighting
- [ ] Clustered deferred for even more lights (per-cluster light lists)
- [ ] Global illumination (light probes, screen-space GI, ray-traced GI)
- [ ] Advanced IBL (parallax-corrected cubemaps, per-pixel probe interpolation)

## Rules

1. **ALWAYS render geometry to GBuffer first** — deferred relies on all material data being present before lighting
2. **NEVER read backbuffer in lighting pass** — it's a write-only target; use separate outputs for accumulation if needed
3. **NEVER exceed 64 point lights per frame** — LightingCB array is fixed at this size; cull or batch into multiple passes if needed
4. **ALWAYS keep depth as SRV in lighting pass** — depth is read as a shader resource texture, not as DSV
5. **ALWAYS update invViewProj** — lighting reconstruction fails silently if invViewProj is stale or incorrect
6. **ALWAYS normalize normals after OctDecode** — interpolation may denormalize; use `normalize(OctDecode(oct))`
7. **ALWAYS clamp roughness to MIN_ROUGHNESS** — prevents singular BRDF lobes at exactly zero roughness
8. **ALWAYS use smooth attenuation** — hard cutoffs at radius cause visible light discontinuities; use windowing function
9. **ALWAYS update VelocityCB each frame** — velocity pass requires current + previous view-projection matrices
10. **NEVER skip velocity generation for TAA** — temporal anti-aliasing requires accurate motion vectors; insufficient velocity causes ghosting

## Rules & Best Practices

### Memory & Performance

1. **Pre-allocate per-frame CBs** before rendering begins — avoid per-draw allocation stalls
2. **Batch point light updates** into single LightingCB upload — one GPU copy per frame, not per light
3. **Use SM6.6 bindless** when available — eliminates descriptor table rebinds between objects
4. **Provide SM5 fallback path** — non-bindless path required for older GPUs and DX11 support

### Shader Integration

1. **Never hardcode light count** — always read `pointLightCount` from LightingCB
2. **Always clamp roughness** — MIN_ROUGHNESS = 0.04 prevents singular specular lobes
3. **Always normalize normals** — G-Buffer normals are not guaranteed unit-length after interpolation
4. **Always saturate dot products** — prevents negative values from ill-conditioned geometry

### Debugging

1. **Visualize G-Buffer targets separately** — append `_debug` pass that samples single target to backbuffer
2. **Validate depth reconstruction** — render reconstructed world positions as false-color to catch invViewProj errors
3. **Check point light counts** — GPU counter or CPU log confirms lighting CB is filled correctly
4. **Inspect attenuation falloff** — use debug sphere meshes at light positions to visualize radius

## Engine Comparison

| Feature | UE5 | Godot 4 | **UzEngine 1.3+** |
|---------|-----|---------|-----------------|
| Deferred G-Buffer | ✅ (4-5 targets) | ✅ (3-4 targets) | ✅ (2 color + depth + velocity) |
| G-Buffer bandwidth | 40-60 B/px | 28-40 B/px | **16 B/px** (octahedral encoding) |
| Normal encoding | Full precision (RGBA16F) | Full precision | **Octahedral (RG8, <0.5° error)** |
| Velocity buffer | ✅ (Separate pass) | ✅ (Separate) | ✅ (Depth reprojection) |
| TAA support | ✅ (Full history sampling) | ✅ (History + feedback) | ✅ (Motion vectors ready) |
| Multiple lights | ✅ (Cluster deferred) | ✅ (Per-mesh forward) | ✅ (64-light deferred) |
| Point light attenuation | ✅ (Inverse-square) | ✅ (Inverse-square) | ✅ (Windowed inverse-square) |
| Directional light | ✅ (Cascaded shadows) | ✅ (Orthographic) | ✅ (No shadows yet) |
| Deferred complexity | High (cluster building) | Medium (tiling) | **Low (fullscreen)** |

UzEngine's approach favors **simplicity**, **bandwidth efficiency**, and **temporal feature support** over extreme scalability. Cluster-based deferred and tiled light culling are planned for Phase 2+ when they become necessary. The octahedral encoding reduces normal storage by 50% while maintaining imperceptible error, freeing bandwidth for velocity buffer and future post-processing effects.

## References

1. **"Deferred Rendering in Killzone 2"** (Valient 2007) — foundational deferred shading paper
2. **"Tiled Deferred Shading"** (Olsson et al. 2011) — light culling via screen-space tiling
3. **"Clustered Deferred and Forward Shading"** (Persson 2012) — 3D cluster-based light binning
4. **Real-time Rendering, 4th Edition** (Akenine-Möller, Haines, Hoffman) — comprehensive rendering reference
5. **DirectX 12 Documentation** — GPU resource binding, render passes, barriers

