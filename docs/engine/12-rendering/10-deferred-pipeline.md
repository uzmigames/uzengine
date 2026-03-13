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
1. Clear all four render targets (3 color + depth)
2. Begin render pass with three color attachments + depth
3. Bind PSO, per-frame constant buffer, and per-object constant buffer ring
4. For each draw command in the sorted DrawList:
   - Allocate per-object CB (model matrix, normal matrix, material texture indices)
   - Bind per-object CB at root param 2
   - Issue `DrawIndexed(indexCount, 1, baseIndex, baseVertex, instanceStart)`
5. End render pass

**Resources Written**:
- `albedoTarget` — RGBA8Unorm, rgb = linear base color, a = 1.0
- `normalTarget` — RGBA16Float, rgb = world-space normal [-1,1], a = 1.0
- `metalRoughTarget` — RGBA8Unorm, r = metallic, g = roughness, b = AO, a = 1.0
- `depthBuffer` — D32Float, standard near-to-far depth

### Lighting Pass

**Register**: `AddLightingPass(builder, desc)`

**Execution**:
1. Clear backbuffer to black
2. Begin render pass with single color attachment (backbuffer), no depth
3. Bind PSO (fullscreen triangle, no vertex/index buffers)
4. Bind per-frame CB at root param 1 (contains invViewProj for position reconstruction)
5. Bind lighting CB at root param 3 (point light array)
6. For SM6.6 bindless: push G-Buffer SRV indices via root constants
7. For SM5 fallback: bind G-Buffer textures at t0-t3 via SetFragmentTexture
8. Issue `Draw(3, 0)` — fullscreen triangle via SV_VertexID
9. End render pass

**Resources Read**:
- `albedoGBuffer` — Read (ShaderResource state)
- `normalGBuffer` — Read (ShaderResource state)
- `metalRoughGBuffer` — Read (ShaderResource state)
- `depthGBuffer` — Read as SRV (ShaderResource state), NOT as depth target

**Resources Written**:
- `backbuffer` — Write (RenderTarget state)

## G-Buffer Layout

The G-Buffer is a set of four textures that store all material data needed for lighting evaluation.

| Target | Format | Resolution | Purpose |
|--------|--------|------------|---------|
| Albedo | RGBA8Unorm | Full-res | rgb = linear base color (sRGB→linear conversion in PS), a = 1.0 |
| Normal | RGBA16Float | Full-res | rgb = world-space normal in [-1,1], a = 1.0 (no packing) |
| MetalRough | RGBA8Unorm | Full-res | r = metallic [0,1], g = roughness [0,1], b = AO [0,1], a = 1.0 |
| Depth | D32Float | Full-res | Standard hardware depth (0.0 = far, 1.0 = near) |

**Total bandwidth per pixel**: 4 + 8 + 4 + 4 = **20 bytes** (before compression).

### Why These Formats?

- **RGBA8Unorm for Albedo**: 8 bits per channel sufficient for sRGB color quantization; alpha unused (always 1.0)
- **RGBA16Float for Normal**: 16-bit floats preserve ±1.0 normal values without packing artifacts (no tangent frame reconstruction needed — normals already world-space)
- **RGBA8Unorm for MetalRough**: Metallic, roughness, and AO all use [0,1] range; UNORM packs efficiently to 8 bits each
- **D32Float for Depth**: Full 32-bit precision maintains depth accuracy at distance (no reversed-Z yet — standard 0=far, 1=near)

## Shaders

### GBuffer Pixel Shader (`gbuffer_ps.hlsl`)

**Input**: Per-vertex interpolants (position, normal, tangent, UV, world position)

**Output**: Three MRT targets (albedo, normal, metallic/roughness)

**Key operations**:
1. Sample PBR texture maps (or use defaults if index == 0)
2. Apply tangent-space normal mapping if normal map bound
3. Output raw material data with no lighting

**Binding paths**:
- **SM6.6 bindless** (`#define UZ_BINDLESS`): Sample textures via `ResourceDescriptorHeap[index]`
- **SM5 fallback**: Sample register-bound textures `t0` (albedo), `t1` (normal), `t2` (roughness), `t3` (metallic), `t4` (AO)

**Constant buffers**:
- `b1`: PerFrameCB (viewProj matrix)
- `b2`: PerObjectCB (model, normalMatrix, texture indices)

**Defaults** (when texture index == 0):
- Albedo: `float3(1.0, 1.0, 1.0)` (white)
- Metallic: `0.0` (dielectric)
- Roughness: `0.5` (perceptual)
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

1. **Read G-Buffer**
   ```hlsl
   float3 albedo    = gbufferAlbedo.Sample(pointSampler, uv).rgb;
   float3 N         = normalize(gbufferNormal.Sample(pointSampler, uv).rgb);
   float4 metalRough = gbufferMetalRough.Sample(pointSampler, uv);
   float  depth     = gbufferDepth.Sample(pointSampler, uv).r;
   ```

2. **Reconstruct world position from depth**
   ```hlsl
   float3 worldPos = ReconstructWorldPosition(uv, depth);
   ```
   Uses `invViewProj` matrix from PerFrameCB to transform NDC+depth back to world space.

3. **Evaluate Cook-Torrance BRDF for directional light**
   ```hlsl
   float3 L_dir = normalize(lightDir.xyz);
   float3 Lo = EvaluateBRDF(N, V, L_dir, lightColor.xyz, albedo, metallic, alpha, F0);
   ```

4. **Evaluate Cook-Torrance BRDF for each point light**
   ```hlsl
   for (uint i = 0; i < pointLightCount; ++i) {
       float3 lightVec  = pointLights[i].position - worldPos;
       float  dist      = length(lightVec);
       float  attenuation = SmoothAttenuation(dist, radius);
       Lo += EvaluateBRDF(N, V, lightVec/dist, radiance, albedo, metallic, alpha, F0);
   }
   ```

5. **Tonemap (Reinhard) + gamma correction**
   ```hlsl
   color = color / (color + 1.0);           // Reinhard tonemap
   color = pow(color, 1.0 / 2.2);           // sRGB gamma
   ```

**Constant buffers**:
- `b1`: PerFrameCB (viewProj, invViewProj, cameraPos, timeParams, directional light)
- `b3`: LightingCB (point light array, count)

**Binding paths**:
- **SM6.6 bindless**: G-Buffer SRV indices pushed via root constants
- **SM5 fallback**: G-Buffer textures bound at `t0-t3`

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
    .metalRoughTarget = builder.CreateResource(...),
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
- Albedo: None → RenderTarget (write) → ShaderResource (read by lighting)
- Normal: None → RenderTarget (write) → ShaderResource (read by lighting)
- MetalRough: None → RenderTarget (write) → ShaderResource (read by lighting)
- Depth: None → DepthStencilWrite (write) → ShaderResource (read by lighting as SRV, not DSV)

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
- Albedo: 4 bytes (RGBA8)
- Normal: 8 bytes (RGBA16F)
- MetalRough: 4 bytes (RGBA8)
- Depth: 4 bytes (R32F)
- **Total**: 20 bytes per pixel

At 1920×1080 with 60 FPS:
```
20 bytes/pixel × 2,073,600 pixels × 60 frames/sec = 2.5 GB/sec
```

This is well within modern GPU bandwidth (300+ GB/sec on RTX 4090).

**Per-pixel Lighting Bandwidth** (lighting pass):
- G-Buffer inputs: 20 bytes (read-only)
- Backbuffer output: 4 bytes (RGBA8 write)
- **Total**: 24 bytes per pixel

Lighting evaluation does relatively few operations per byte fetched, making this pass memory-bandwidth-limited rather than compute-limited.

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

### Phase 3+ (Future)

- [ ] Virtual Shadow Maps (VSM) for efficient dynamic lighting
- [ ] Clustered deferred for even more lights (per-cluster light lists)
- [ ] Global illumination (light probes, screen-space GI)
- [ ] Image-based lighting (IBL with prefiltered cubemaps)

## Rules

1. **ALWAYS render geometry to GBuffer first** — deferred relies on all material data being present before lighting
2. **NEVER read backbuffer in lighting pass** — it's a write-only target; use separate outputs for accumulation if needed
3. **NEVER exceed 64 point lights per frame** — LightingCB array is fixed at this size; cull or batch into multiple passes if needed
4. **ALWAYS keep depth as SRV in lighting pass** — depth is read as a shader resource texture, not as DSV
5. **ALWAYS update invViewProj** — lighting reconstruction fails silently if invViewProj is stale or incorrect
6. **NEVER pack normals** — RGBA16Float is the minimum to avoid banding at grazing angles (RGBA10A2 would be cheaper but introduces artifacts)
7. **ALWAYS use smooth attenuation** — hard cutoffs at radius cause visible light discontinuities; use windowing function

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

| Feature | UE5 | Godot 4 | **UzEngine 1.3** |
|---------|-----|---------|-----------------|
| Deferred G-Buffer | ✅ (4-5 targets) | ✅ (3-4 targets) | ✅ (3 + depth) |
| Multiple lights | ✅ (Cluster deferred) | ✅ (Per-mesh forward) | ✅ (64-light deferred) |
| Point light attenuation | ✅ (Inverse-square) | ✅ (Inverse-square) | ✅ (Windowed inverse-square) |
| Directional light | ✅ (Cascaded shadows) | ✅ (Orthographic) | ✅ (No shadows yet) |
| Deferred complexity | High (cluster building) | Medium (tiling) | **Low (fullscreen)** |

UzEngine's approach favors **simplicity** and **straightforward performance** over extreme scalability. Cluster-based deferred and tiled light culling are planned for Phase 2+ when they become necessary.

## References

1. **"Deferred Rendering in Killzone 2"** (Valient 2007) — foundational deferred shading paper
2. **"Tiled Deferred Shading"** (Olsson et al. 2011) — light culling via screen-space tiling
3. **"Clustered Deferred and Forward Shading"** (Persson 2012) — 3D cluster-based light binning
4. **Real-time Rendering, 4th Edition** (Akenine-Möller, Haines, Hoffman) — comprehensive rendering reference
5. **DirectX 12 Documentation** — GPU resource binding, render passes, barriers

