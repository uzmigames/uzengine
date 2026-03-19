<!-- doc-meta
title: SSGI Data Flow — Screen-Space Global Illumination
category: analysis
created: 2026-03-19
-->

# SSGI Data Flow — Screen-Space Global Illumination

This document maps UE5's standalone SSGI pipeline to UzEngine, covering every
stage from GBuffer input to deferred lighting compositing.

---

## 1. UE5 Reference Table

| Stage | UE5 File | Lines | Description |
|-------|----------|-------|-------------|
| GI Method selection | `IndirectLightRendering.cpp` | 39-45 | `r.DynamicGlobalIlluminationMethod=2` selects SSGI |
| Half-res CVar | `IndirectLightRendering.cpp` | 57-60 | `r.DiffuseIndirect.HalfRes` default **1** (half resolution) |
| Ray count CVar | `IndirectLightRendering.cpp` | 62-64 | `r.DiffuseIndirect.RayPerPixel` default **6** (overridden to quality-based) |
| Quality-to-RayCount map | `ScreenSpaceRayTracing.cpp` | 684-704 | Q1=4, Q2=8, Q3=16, Q4=32 rays/pixel |
| SSGI Quality CVar | `ScreenSpaceRayTracing.cpp` | 60-63 | `r.SSGI.Quality` default **4** (32 rays) |
| GroupSize calculation | `ScreenSpaceRayTracing.cpp` | 650-682 | `GroupX * GroupY * RayCount == 256` threads |
| Downscale factor | `IndirectLightRendering.cpp` | 812 | `DownscaleFactor = HalfRes ? 2 : 1` |
| TracingViewportSize | `IndirectLightRendering.cpp` | 866-872 | `ViewRect / DownscaleFactor` |
| Prev frame color reduction | `ScreenSpaceRayTracing.cpp` | 745-800 | 5-mip R11G11B10 pyramid of prev scene color |
| PrevSceneColor format | `SSRTPrevFrameReduction.usf` | 69 | `PF_FloatR11G11B10` (5 mips) |
| Leak-free reprojection | `ScreenSpaceRayTracing.cpp` | 55-58 | `r.SSGI.LeakFreeReprojection` default **1** |
| SSGI trace shader | `SSRT/SSRTDiffuseIndirect.usf` | 1-586 | `MainCS` compute shader entry point |
| Ray direction: cosine hemisphere | `SSRTDiffuseIndirect.usf` | 220-228 | `CosineSampleHemisphereConcentric(E)` in tangent space |
| Random seed | `SSRTDiffuseIndirect.usf` | 215-218 | `Rand3DPCG16(PixelPos, StateFrameIndexMod8)` |
| Sample distribution | `SSRTDiffuseIndirect.usf` | 413 | `Hammersley16(RaySequenceId, CONFIG_RAY_COUNT, RandomSeed)` |
| Ray march function | `SSRT/SSRTRayCast.ush` | 453+ | `CastScreenSpaceRay` — HiZ hierarchical march |
| Ray march steps | `SSRTDiffuseIndirect.usf` | 24-37 | Q1=8, Q2=8, Q3=8, Q4=12 steps per ray |
| Start mip level | `SSRTDiffuseIndirect.usf` | 455 | `StartMipLevel = 1.0` |
| Hit shading | `SSRTDiffuseIndirect.usf` | 485-488 | Sample reduced prev-frame scene color at hit UV+mip |
| Karis weighting | `SSRTDiffuseIndirect.usf` | 492-494 | `weight = rcp(1 + Luminance(color))` firefly suppression |
| Per-pixel accumulation | `SSRTDiffuseIndirect.usf` | 546-573 | Average all rays via LDS, undo Karis |
| Output textures | `SSRTDiffuseIndirect.usf` | 583-584 | `IndirectDiffuseOutput` (RGBA16F) + `AmbientOcclusionOutput` (R16F) |
| Output AO inversion | `SSRTDiffuseIndirect.usf` | 577 | `AO = 1 - AO` (hit=occluded, inverted for shader convention) |
| Denoiser dispatch | `IndirectLightRendering.cpp` | 1200-1230 | `DenoiseScreenSpaceDiffuseIndirect` |
| Denoiser settings | `ScreenSpaceDenoise.cpp` | 2960-2969 | `ReconstructionSamples=8`, temporal accum enabled, signal=ScreenSpaceDiffuseIndirect |
| Temporal accumulation | `ScreenSpaceDenoise.cpp` | 2967 | `bUseTemporalAccumulation = r.GI.TemporalAccumulation != 0` |
| Composite shader | `DiffuseIndirectComposite.usf` | 1-270 | `MainPS` pixel shader |
| Composite blend (SSGI) | `IndirectLightRendering.cpp` | 1454-1457 | `FinalColor = SceneColor * AO + IndirectDiffuse` (dual-src blend) |
| Composite permutation | `IndirectLightRendering.cpp` | 1433 | `FApplyDiffuseIndirectDim = 1` for SSGI |
| Upscale check | `IndirectLightRendering.cpp` | 1435 | `bUpscale = denoised extent != scene color extent` |
| Min luminance | `ScreenSpaceRayTracing.cpp` | 65-68 | `r.SSGI.MinimumLuminance` default **0.5** |
| Sky distance | `ScreenSpaceRayTracing.cpp` | 80-83 | `r.SSGI.SkyDistance` default **10000000** km |
| Reject uncertain rays | `ScreenSpaceRayTracing.cpp` | 70-73 | `r.SSGI.RejectUncertainRays` default **1** |

---

## 2. UzEngine Mapping Table

| Component | UE5 Equivalent | UzEngine Status | UzEngine Path |
|-----------|---------------|----------------|---------------|
| HiZ depth pyramid | `FurthestHZBTexture` | **Partial** — SSR uses depth SRV as HiZ (single mip) | `ssr_trace_cs.hlsl:467` uses `depthSRV` |
| SSR trace (specular) | `SSRTReflections.usf` | **Exists** | `runtime/shaders/runtime/postprocess/ssr_trace_cs.hlsl` |
| SSR resolve | `SSRTReflections.usf` resolve | **Exists** | `runtime/shaders/runtime/postprocess/ssr_resolve_cs.hlsl` |
| GBuffer: depth (D32F) | `SceneDepthTexture` | **Exists** | `gbuffer.h` — `depth` (D32Float) |
| GBuffer: normal+metal+rough | `GBufferA/B` | **Exists** | `gbuffer.h` — `normal` (RGBA8Unorm: oct RG, metal B, rough A) |
| GBuffer: albedo+AO | `GBufferC` | **Exists** | `gbuffer.h` — `albedo` (RGBA8Unorm: RGB + AO alpha) |
| GBuffer: velocity | `SceneVelocity` | **Exists** | `gbuffer.h` — `velocity` (RG16Float) |
| GBuffer: emissive | N/A (Substrate) | **Exists** | `gbuffer.h` — `emissive` (RGBA16Float) |
| PerFrameCB | `View` uniform buffer | **Exists** | `constant_buffers.h` — 512 bytes |
| Prev-frame scene color | `ScreenSpaceRayTracingInput` | **NEW** — needs history buffer | N/A |
| Prev-frame color mip pyramid | `SSRTPrevFrameReduction.usf` | **NEW** — 5-mip R11G11B10 | N/A |
| SSGI trace compute shader | `SSRTDiffuseIndirect.usf` | **NEW** | N/A |
| SSGI denoiser (spatial) | `ScreenSpaceDenoise.cpp` | **NEW** — simplified spatial | N/A |
| SSGI denoiser (temporal) | `ScreenSpaceDenoise.cpp` | **NEW** — temporal accumulation | N/A |
| SSGI composite | `DiffuseIndirectComposite.usf` | **Modify** — add to deferred lighting | `deferred_lighting_ps.hlsl:958-1037` |
| Render pass registration | `IndirectLightRendering.cpp` | **Modify** | `frame_lighting.cpp:27` |
| Compute root signature | Per-pass root sig | **Exists** — 8 root constants + b1 CBV + UAV/SRV tables | Used by SSR/GTAO/DFS |
| Bilateral upsample | `DIM_UPSCALE_DIFFUSE_INDIRECT` | **NEW** — if half-res | N/A |
| CB: ssgiSrvIndex | N/A (UE5 uses RDG bindings) | **NEW** — add to PerFrameCB | `constant_buffers.h` |

---

## 3. Data Flow Diagram

```
Frame N-1 HDR Scene Color
    |
    v
[1. PrevSceneColor Mip Pyramid]  (CS, 5 mips, R11G11B10)
    |   Skip mip 0 (start at half-res), generate mips 0..4
    |   Leak-free: use prev depth to reject disoccluded pixels
    v
GBuffer (Frame N)
    |
    +---> Depth (D32F)
    +---> Normal+Metal+Rough (RGBA8)
    +---> Velocity (RG16F) -----> [4. Temporal Denoise]
    |
    v
[2. SSGI Trace]  (CS, half-res)
    |   Input: GBuffer depth, GBuffer normal, HiZ pyramid, PrevSceneColor pyramid
    |   Per pixel: generate N rays via cosine-hemisphere + Hammersley QMC
    |   Per ray: HiZ ray march (8-12 steps), sample prev color at hit
    |   Karis luminance weighting for firefly suppression
    |   Output: IndirectDiffuse (RGBA16F, half-res) + AO mask (R16F, half-res)
    v
[3. Spatial Denoise]  (CS, half-res)
    |   Cross-bilateral filter: depth+normal edge-stopping
    |   8 reconstruction samples
    |   Output: denoised IndirectDiffuse + AO (half-res)
    v
[4. Temporal Accumulate]  (CS, half-res)
    |   Reproject via velocity buffer
    |   Blend current + history (exponential moving average)
    |   Clamp history via neighborhood color bounds (variance clipping)
    |   Output: temporally stable IndirectDiffuse + AO (half-res)
    v
[5. Bilateral Upsample]  (CS or PS, full-res)
    |   Joint bilateral upsample using full-res depth+normal
    |   Output: SSGI result (RGBA16F, full-res)
    v
[6. Composite into Deferred Lighting]  (PS, full-res)
    |   In deferred_lighting_ps.hlsl ambient section:
    |   diffuseIBL_ssgi = ssgiColor * kD * albedo
    |   ambient = (diffuseIBL + diffuseIBL_ssgi) * AO + specularIBL
    |   Or: SceneColor = SceneColor * ssgiAO + ssgiIndirect
    v
Final HDR Scene Color --> Post-Process (Bloom, TAA, Tonemap)
```

---

## 4. Resource Requirements

### 4.1 New Textures

| Name | Format | Resolution | Mips | Usage | Notes |
|------|--------|-----------|------|-------|-------|
| `m_prevSceneColor` | R11G11B10F | Full-res | 1 | SRV | Copy of prev frame HDR (before tonemap) |
| `m_prevSceneColorMips` | R11G11B10F | Half-res | 5 | UAV+SRV | Reduced prev color pyramid |
| `m_prevSceneAlpha` | R8Unorm | Half-res | 5 | UAV+SRV | Leak-free depth mask (optional) |
| `m_ssgiTraceOutput` | RGBA16Float | Half-res | 1 | UAV+SRV | Raw SSGI indirect diffuse |
| `m_ssgiAOOutput` | R16Float | Half-res | 1 | UAV+SRV | Raw SSGI ambient occlusion |
| `m_ssgiDenoised` | RGBA16Float | Half-res | 1 | UAV+SRV | Spatially denoised diffuse |
| `m_ssgiDenoisedAO` | R16Float | Half-res | 1 | UAV+SRV | Spatially denoised AO |
| `m_ssgiHistory` | RGBA16Float | Half-res | 1 | UAV+SRV | Temporal history (ping-pong with denoised) |
| `m_ssgiHistoryAO` | R16Float | Half-res | 1 | UAV+SRV | Temporal AO history |
| `m_ssgiResult` | RGBA16Float | Full-res | 1 | UAV+SRV | Final upsampled SSGI for lighting |

**Total GPU memory** (at 1920x1080):
- Half-res = 960x540
- R11G11B10F 5 mips ~ 2.0 MB
- RGBA16F half * 4 textures ~ 4 * 960*540*8 = 16.6 MB
- R16F half * 3 textures ~ 3 * 960*540*2 = 3.1 MB
- RGBA16F full * 1 = 1920*1080*8 = 16.6 MB
- **Total ~ 38 MB** (acceptable for a GI feature)

### 4.2 New SRV Indices in PerFrameCB

| Field | Type | Default | Purpose |
|-------|------|---------|---------|
| `ssgiSrvIndex` | `uint32_t` | `UINT32_MAX` | Bindless SRV for upsampled SSGI result |
| `ssgiAOSrvIndex` | `uint32_t` | `UINT32_MAX` | Bindless SRV for SSGI AO mask |
| `ssgiEnabled` | `uint32_t` | `0` | 1 = SSGI active, 0 = disabled |

These consume 3 of the 7 remaining padding DWORDs in PerFrameCB (currently `_pad2[7]`).

### 4.3 Compute Root Signature Bindings (per pass)

All SSGI compute passes use the existing compute root signature:
- `b0`: 8 root constants (SRV indices, dimensions, parameters)
- `b1`: PerFrameCB (viewProj, invViewProj, camera, near/far)
- Slot 4: UAV descriptor table (u0, u1)
- Slot 5: SRV descriptor table (t0..tN via bindless heap)
- `s0`: Static linear-clamp sampler

---

## 5. Changes Required

### C++ Engine Changes

| # | File | Change |
|---|------|--------|
| 1 | `runtime/engine/renderer/core/params/constant_buffers.h` | Add `ssgiSrvIndex`, `ssgiAOSrvIndex`, `ssgiEnabled` to PerFrameCB (replace 3 of `_pad2[7]`) |
| 2 | `runtime/engine/renderer/scene/scene_renderer.h` | Add SSGI texture handles, UAV/SRV indices, history ping-pong state, `SSGISettings` struct |
| 3 | `runtime/engine/renderer/scene/scene_lifecycle.cpp` | Create SSGI textures in `CreateResources()`, destroy in `DestroyResources()` |
| 4 | `runtime/engine/renderer/frame/frame_lighting.cpp` | Register 5 new render graph passes: PrevColorReduce, SSGITrace, SpatialDenoise, TemporalAccum, BilateralUpsample — inserted after GTAO/DFS, before deferred lighting |
| 5 | `runtime/engine/renderer/pipelines/pipeline_cache.h` | Add `GetSsgiTrace()`, `GetSsgiSpatialDenoise()`, `GetSsgiTemporalAccum()`, `GetSsgiBilateralUpsample()`, `GetPrevColorReduce()` PSO getters |
| 6 | `runtime/engine/renderer/pipelines/pipeline_cache.cpp` | Create 5 new compute PSOs from the new HLSL shaders |
| 7 | `runtime/engine/renderer/scene/scene_renderer.cpp` | Populate `ssgiSrvIndex` / `ssgiEnabled` in PerFrameCB before lighting pass; copy current HDR to prev-frame buffer at end of frame |
| 8 | `runtime/engine/ecs/environment/` | (Optional) Add `SSGIComponent` or `PostProcessVolume` SSGI settings — quality, intensity, half-res toggle |

### Shader Changes

| # | File | Change |
|---|------|--------|
| 9 | `runtime/shaders/runtime/ssgi/prev_color_reduce_cs.hlsl` | **NEW** — Downsample prev scene color into 5-mip pyramid (port SSRTPrevFrameReduction.usf) |
| 10 | `runtime/shaders/runtime/ssgi/ssgi_trace_cs.hlsl` | **NEW** — Main SSGI trace: cosine-hemisphere rays, HiZ march, prev-color sampling, Karis weighting (port SSRTDiffuseIndirect.usf) |
| 11 | `runtime/shaders/runtime/ssgi/ssgi_spatial_denoise_cs.hlsl` | **NEW** — Cross-bilateral spatial filter (depth+normal edge-stopping, 8 samples) |
| 12 | `runtime/shaders/runtime/ssgi/ssgi_temporal_accum_cs.hlsl` | **NEW** — Temporal accumulation: velocity reproject, neighborhood clamp, EMA blend |
| 13 | `runtime/shaders/runtime/ssgi/ssgi_bilateral_upsample_cs.hlsl` | **NEW** — Joint bilateral upsample from half-res to full-res |
| 14 | `runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl` | **MODIFY** — In ambient section (~line 958-1037): when `ssgiEnabled`, read SSGI texture and add indirect diffuse contribution, modulate existing ambient by SSGI AO |

### Lua API Changes

| # | File | Change |
|---|------|--------|
| 15 | `runtime/engine/scripting/api/core/lua_camera_api.cpp` | Add `set_ssgi_enabled(bool)`, `set_ssgi_quality(1-4)`, `set_ssgi_intensity(float)` bindings |

---

## 6. Sub-Tasks

Each sub-task touches 1-2 files and can be implemented independently (except
where noted as blocking).

### Task 1: PerFrameCB Extension
- **Files**: `constant_buffers.h`
- **Work**: Replace 3 of `_pad2[7]` with `ssgiSrvIndex`, `ssgiAOSrvIndex`, `ssgiEnabled`. Update `static_assert`.
- **Blocks**: Tasks 5, 6

### Task 2: Prev-Frame Color Reduction Shader
- **Files**: `runtime/shaders/runtime/ssgi/prev_color_reduce_cs.hlsl` (new)
- **Work**: Port `SSRTPrevFrameReduction.usf`. Downsample prev HDR scene color to half-res, then generate 4 additional mips. Leak-free mode using prev depth buffer for disocclusion rejection. Format: R11G11B10F, 5 mips.
- **UE5 ref**: `SSRTPrevFrameReduction.usf:1-130`, `ScreenSpaceRayTracing.cpp:745-800`

### Task 3: SSGI Trace Compute Shader
- **Files**: `runtime/shaders/runtime/ssgi/ssgi_trace_cs.hlsl` (new)
- **Work**: Port `SSRTDiffuseIndirect.usf`. Key algorithm:
  - Thread layout: `[numthreads(TILE_X, TILE_Y, RAY_COUNT)]` where `TILE_X * TILE_Y * RAY_COUNT = 256`
  - Load GBuffer (depth, normal) into LDS per tile
  - Per ray: generate cosine-hemisphere direction via `Hammersley16 + CosineSampleHemisphereConcentric`
  - HiZ ray march via `CastScreenSpaceRay` (reuse pattern from `ssr_trace_cs.hlsl`)
  - On hit: sample prev-frame color pyramid at hit UV + mip level
  - Karis luminance weighting: `weight = rcp(1 + Luminance(color))`
  - Average all rays per pixel in LDS, undo Karis: `color *= rcp(1 - Luminance(color))`
  - Output: `IndirectDiffuseOutput[pixel] = float4(color, 1)`, `AmbientOcclusionOutput[pixel] = 1 - hitFraction`
- **Quality presets**: Q1(4 rays, 8 steps), Q2(8, 8), Q3(16, 8), Q4(32, 12)
- **UE5 ref**: `SSRTDiffuseIndirect.usf:1-586`, `SSRTRayCast.ush:69-160`

### Task 4: Spatial Denoise Compute Shader
- **Files**: `runtime/shaders/runtime/ssgi/ssgi_spatial_denoise_cs.hlsl` (new)
- **Work**: Cross-bilateral spatial filter. 8 samples in a disk pattern around each pixel. Edge-stopping weights based on depth difference and normal similarity. Separable (horizontal then vertical) or single-pass rotated disk. Output same resolution as input (half-res).
- **UE5 ref**: `ScreenSpaceDenoise.cpp:2960-2985` — `ReconstructionSamples=8`

### Task 5: Temporal Accumulation Compute Shader
- **Files**: `runtime/shaders/runtime/ssgi/ssgi_temporal_accum_cs.hlsl` (new)
- **Work**: Reproject current pixel to prev frame via velocity buffer. Sample history at reprojected UV. Clamp history to 3x3 neighborhood color AABB (variance clipping). Blend: `result = lerp(history, current, alpha)` where `alpha ~ 0.05-0.1` for stable pixels, `1.0` for disoccluded. Output to history buffer (ping-pong).
- **Requires**: Velocity buffer (GBuffer already has it), prev-frame depth for disocclusion detection.
- **UE5 ref**: `ScreenSpaceDenoise.cpp:2967` — temporal accum flag

### Task 6: Bilateral Upsample Compute Shader
- **Files**: `runtime/shaders/runtime/ssgi/ssgi_bilateral_upsample_cs.hlsl` (new)
- **Work**: Joint bilateral upsample from half-res to full-res. 4-tap bilinear with depth+normal edge-stopping weights. For each full-res pixel, find 4 nearest half-res neighbors, weight by `exp(-depthDiff^2 / sigma_d) * max(0, dot(N_full, N_half))`. Output: RGBA16F full-res SSGI result.
- **UE5 ref**: `IndirectLightRendering.cpp:1435-1438` — `DIM_UPSCALE_DIFFUSE_INDIRECT`

### Task 7: Render Pass Registration (C++)
- **Files**: `frame_lighting.cpp`, `scene_renderer.h`
- **Work**: Add 5 new `rgBuilder.AddPass()` calls between GTAO/DFS and deferred lighting:
  1. `SSGI_PrevColorReduce` — dispatch prev color mip pyramid
  2. `SSGI_Trace` — dispatch SSGI trace CS
  3. `SSGI_SpatialDenoise` — dispatch spatial filter CS
  4. `SSGI_TemporalAccum` — dispatch temporal accumulation CS
  5. `SSGI_BilateralUpsample` — dispatch upsample CS
- Each pass declares proper Read/Write on RG resources with correct state transitions (UAV barriers between trace and denoise, SRV transitions for lighting).
- Populate `ssgiSrvIndex` in PerFrameCB before lighting pass.
- At end of frame: copy HDR scene color to `m_prevSceneColor` for next frame's reduction.

### Task 8: Pipeline Cache Extension (C++)
- **Files**: `pipeline_cache.h`, `pipeline_cache.cpp`
- **Work**: Add 5 compute PSO entries. Compile new HLSL shaders with quality permutations for the trace pass (Q1-Q4 via `#define QUALITY N`).

### Task 9: Resource Creation (C++)
- **Files**: `scene_lifecycle.cpp`, `scene_renderer.h`
- **Work**: Create all SSGI textures (see Section 4.1), allocate SRV/UAV indices from descriptor heap. Handle resize. Add ping-pong index for temporal history.

### Task 10: Deferred Lighting Compositing (Shader)
- **Files**: `deferred_lighting_ps.hlsl`
- **Work**: In ambient section (~line 958): when `ssgiEnabled != 0`, sample `ssgiSrvIndex` texture and `ssgiAOSrvIndex` texture at screen UV. Composite:
  ```hlsl
  // SSGI replaces SH-based diffuse IBL
  float3 ssgiColor = ssgiTex.SampleLevel(linearSampler, uv, 0).rgb;
  float  ssgiAO    = ssgiAOTex.SampleLevel(linearSampler, uv, 0).r;
  // Blend SSGI with existing IBL diffuse
  float3 diffuseIBL = kD * albedo * lerp(irradiance, ssgiColor, ssgiBlendWeight);
  // Apply SSGI AO to combined ambient
  ambient *= ssgiAO;
  ```
  The SSGI output is pre-multiplied indirect radiance (not modulated by albedo) — the shader applies `kD * albedo` like UE5's `DiffuseIndirectComposite.usf` does.

### Task 11: Lua API Bindings
- **Files**: `lua_camera_api.cpp` (or new `lua_ssgi_api.cpp`)
- **Work**: Expose `uz.rendering.set_ssgi_enabled(bool)`, `uz.rendering.set_ssgi_quality(int)`, `uz.rendering.set_ssgi_intensity(float)`.

---

## 7. Key Algorithm Details (from UE5)

### 7.1 Ray Direction Generation

UE5 generates diffuse ray directions using cosine-weighted hemisphere sampling
with Hammersley quasi-random sequences:

```
@source UE5 SSRTDiffuseIndirect.usf:220-228 — ComputeL
RandomSeed = Rand3DPCG16(int3(PixelPosition, StateFrameIndexMod8))
E = Hammersley16(RaySequenceId, CONFIG_RAY_COUNT, RandomSeed)
TangentL = CosineSampleHemisphereConcentric(E)
L = mul(TangentL, TangentBasis(N))
```

- `Hammersley16`: low-discrepancy 2D sequence using radical inverse (van der Corput)
- `CosineSampleHemisphereConcentric`: concentric disk mapping for cosine distribution
- `Rand3DPCG16`: per-pixel 3D PCG hash with temporal jitter via `StateFrameIndexMod8`

### 7.2 Screen-Space Ray March

The same `CastScreenSpaceRay` function from SSR is reused for SSGI:

```
@source UE5 SSRTRayCast.ush:453+ — CastScreenSpaceRay
- Project ray to screen space (clip-space start/end)
- March in HZB UV space with batched depth comparisons (4 samples per batch)
- Start at mip 1, step down mips on miss, step up on hit
- Refine hit with linear segment intersection
- Output: HitUVz (UV + device-Z), mip Level, bHit, bUncertain
```

For SSGI the `RayRoughness` is set to `1.0` (fully diffuse — uses coarser mip
during march for broader acceptance), and `SlopeCompareToleranceScale` is the
default (not the 4.0 used by specular SSR).

### 7.3 Hit Shading

On a ray hit, UE5 reads from the **previous frame's reduced scene color** (not
the current GBuffer). This is critical — it captures full lighting (direct +
indirect from prior frame) at the hit point, not just albedo:

```
@source UE5 SSRTDiffuseIndirect.usf:485-488
ReducedColorUV = HitUVz.xy * ColorBufferScaleBias.xy + ColorBufferScaleBias.zw
SampleColor = ColorTexture.SampleLevel(sampler, ReducedColorUV, Level)
```

The `Level` from the ray march determines which mip to sample, providing
automatic LOD for distant/uncertain hits.

### 7.4 Composite Blend

UE5 uses dual-source blending to composite SSGI:

```
@source UE5 IndirectLightRendering.cpp:1454-1457
FinalColor = SceneColor * AO + IndirectDiffuse
BlendState: BO_Add, BF_One, BF_Source1Color (RGB), BF_One, BF_Source1Alpha (A)
```

Where:
- `Source0` RGB = indirect diffuse color (pre-multiplied by `kD * albedo`)
- `Source1` RGB = AO factor (per-channel, from SSGI AO mask applied to existing scene color)

In UzEngine, since we use a fullscreen pixel shader for deferred lighting (not
a separate composite pass), we can integrate directly: read the SSGI texture
in the ambient section and blend there, avoiding the need for dual-source blend.

---

## 8. Implementation Priority

Recommended order for incremental implementation:

1. **Task 1** (PerFrameCB) — 30 min, unblocks everything
2. **Task 9** (Resource creation) — 1 hr, creates all textures
3. **Task 8** (Pipeline cache) — 1 hr, compiles shaders
4. **Task 3** (SSGI trace shader) — 3 hr, core algorithm (most complex)
5. **Task 2** (Prev color reduce) — 1 hr, builds input for trace
6. **Task 7** (Render pass registration) — 2 hr, wires everything together
7. **Task 10** (Composite in deferred lighting) — 1 hr, makes result visible
8. **Task 4** (Spatial denoise) — 2 hr, quality improvement
9. **Task 5** (Temporal accumulation) — 2 hr, stability improvement
10. **Task 6** (Bilateral upsample) — 1 hr, full-res output
11. **Task 11** (Lua API) — 30 min, scripting access

**Minimum viable SSGI** (tasks 1, 3, 7, 9, 10): trace at half-res, no denoise,
direct composite. Noisy but functionally correct. Denoise and temporal can be
added incrementally.
