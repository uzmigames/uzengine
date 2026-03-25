<!-- doc-meta
type: reference
status: active
updated: 2026-03-24
version: 1.0
-->

# UzEngine Shader Reference

Complete catalog of all HLSL shaders and shared include files in the UzEngine runtime. 98 shader programs organized by category + 20 shared libraries.

**Quick Stats**
- **Shader Programs**: 98 total (23 compute, 36 pixel, 6 vertex, 1 pixel+vertex shader pairs, misc)
- **Shared Includes**: 20 reusable libraries
- **Total Lines**: ~50,000+ HLSL
- **Rendering Pipeline**: Deferred lit, forward PBR, tone-mapped post-process
- **Platforms**: SM6.6 (bindless), SM5 (traditional IA), all RHI backends via unified compilation

---

## Rendering Pipeline Overview

```
Frame Setup
    ↓
[Depth Prepass] → shadow_depth_vs.hlsl
    ↓
[GBuffer Pass] → mesh_vs.hlsl → gbuffer_ps.hlsl
    ↓
[Decal Pass] → decal_vs.hlsl → decal_ps.hlsl → (optional) decal_apply_cs.hlsl
    ↓
[Deferred Lighting] → fullscreen_vs.hlsl → deferred_lighting_ps.hlsl
    ├─ Reads: GBuffer, shadow atlas, IBL LUTs, GTAO
    ├─ Outputs: lit color + velocity
    └─ Debug: shadow grid visualization
    ↓
[Post-Process]
    ├─ Exposure/histogram → color grading LUT → tonemap
    ├─ FXAA / SMAA / TAA / TSR (upsampling)
    ├─ Bloom (threshold → downsample → upsample → composite)
    ├─ SSR (trace → resolve + temporal)
    ├─ Motion Blur (velocity-based gather)
    ├─ DoF (CoC → disk blur)
    ├─ Lens Flare (screen artifacts)
    └─ GTAO (occlusion culling)
    ↓
[Output] → UI Composit

Compute Passes (async)
    ├─ Atmosphere: LUT baking (transmittance, multi-scatter, irradiance, aerial perspective)
    ├─ Clouds: noise generation, ray marching, temporal reprojection
    ├─ IBL: BRDF LUT, irradiance, prefiltered specular, equirect conversion
    ├─ GI: SDF baking, radiance caches, SSGI ray marching
    ├─ Shadows: distance field cone tracing, atlas cull/upsample
    ├─ Culling: GPU frustum + HiZ test, indirect draw compaction
    ├─ Mips: generic downsampling pipeline
    └─ Velocity: per-pixel motion buffer
```

---

## Editor Shaders (5 programs)

| File | Stage | Purpose | Notes |
|------|-------|---------|-------|
| `editor/grid_vs.hlsl` | VS | Grid overlay vertex transform | Infinite ground plane with major/minor lines, fade at horizon |
| `editor/grid_ps.hlsl` | PS | Grid overlay shading | Computes grid pattern, crosshair, distance-based alpha fade |
| `editor/overlay_vs.hlsl` | VS | Stats overlay vertex pass | Fullscreen quad for FPS counter, frame time graph |
| `editor/overlay_rect_ps.hlsl` | PS | Rectangle UI primitives | Renders colored rects for bars, graph backgrounds |
| `editor/overlay_text_ps.hlsl` | PS | Text rendering for stats | SDF font atlas sampling (monospace for timing readout) |

---

## Atmosphere & Sky (15 programs)

| File | Stage | Purpose | Reference |
|------|-------|---------|-----------|
| `atmosphere/transmittance_lut_cs.hlsl` | CS | Precompute sky transmittance LUT (256×64 RGBA16F) | Bruneton 2017 — single-scattered ray optical depth |
| `atmosphere/multi_scatter_lut_cs.hlsl` | CS | Precompute multi-scatter contribution LUT (32×32 RGBA16F) | Bruneton 2017 — multiple bounces integrated |
| `atmosphere/single_scattering_lut_cs.hlsl` | CS | Single Rayleigh + Mie scattering LUT | Bruneton 2017 — ray integral over altitude |
| `atmosphere/scattering_density_cs.hlsl` | CS | Compute scattering density for iteration | Bruneton 2017 — per-altitude density texture |
| `atmosphere/indirect_scattering_cs.hlsl` | CS | Accumulated indirect multi-scatter | Bruneton 2017 — accumulate bounces into final LUT |
| `atmosphere/irradiance_lut_cs.hlsl` | CS | Ground irradiance from sky hemisphere (64×64 RGBA16F) | Bruneton 2017 — cosine-weighted SH projection |
| `atmosphere/aerial_perspective_volume_cs.hlsl` | CS | 3D aerial perspective volume (32×32×16 RGBA16F) | Bruneton 2017 — inscattering for distant geometry |
| `atmosphere/sky_atmosphere_ps.hlsl` | PS | Render dynamic sky dome | Uses LUTs, samples transmittance, multi-scatter; sun disk |
| `atmosphere/sky_cubemap_cs.hlsl` | CS | Capture sky to cubemap faces | Samples sky_atmosphere_ps per-face for IBL |
| `atmosphere/sky_cubemap_ps.hlsl` | PS | Sky cubemap capture via graphics pipeline | Alternative to compute-based capture |
| `atmosphere/skybox_cubemap_ps.hlsl` | PS | Render pre-baked cubemap as static skybox | Simple cubemap sample for fixed backgrounds |
| `atmosphere/height_fog_ps.hlsl` | PS | Exponential height fog with aerial perspective | 2-layer fog, directional inscattering, distance fade |
| `atmosphere/cloud_noise_shape_cs.hlsl` | CS | 3D Worley/Perlin shape noise (128³) | Cloud billowing patterns, base shape frequency |
| `atmosphere/cloud_noise_detail_cs.hlsl` | CS | 3D detail erosion noise (32³) | High-frequency detail for cloud boundaries |
| `atmosphere/cloud_weather_cs.hlsl` | CS | 2D weather map (coverage, type, wind offset) | Controls cloud density, precipitation, animated winds |
| `atmosphere/cloud_raymarch_cs.hlsl` | CS | Volumetric cloud ray marching with beer-powder | Multi-scatter lighting, shadow sampling, temporal jitter |
| `atmosphere/cloud_temporal_cs.hlsl` | CS | Temporal reprojection for cloud upscaling | Checkerboard→fullscreen via history blend + rejection |
| `atmosphere/cloud_composite_ps.hlsl` | PS | Composite cloud result onto scene color | Premultiplied alpha blend with depth awareness |
| `atmosphere/god_rays_cs.hlsl` | CS | Screen-space god rays from sun occlusion | Radial blur along sun direction from depth discontinuities |

---

## Geometry (8 programs)

| File | Stage | Purpose | Reference |
|------|-------|---------|-----------|
| `geometry/mesh_vs.hlsl` | VS | Standard mesh vertex transform | Transforms vertex position/normal/tangent to world space; supports instancing (UZ_INSTANCED via StructuredBuffer) |
| `geometry/mesh_ps.hlsl` | PS | Forward PBR shading | Cook-Torrance GGX + Burley diffuse + IBL; used for transparent/alpha-tested geometry |
| `geometry/gbuffer_ps.hlsl` | PS | GBuffer deferred write pass | Outputs albedo, normal (octahedral), roughness/metallic to RT targets |
| `geometry/gbuffer_debug_ps.hlsl` | PS | GBuffer visualization | Split-screen debug modes (normal, roughness, metallic, depth) |
| `geometry/decal_vs.hlsl` | VS | Decal OBB projection setup | Transforms unit cube to world space for decal volume projection |
| `geometry/decal_ps.hlsl` | PS | DBuffer decal sampling via depth reconstruction | Projects decal textures (albedo, normal, roughness) onto geometry using depth texture |

---

## Lighting & Shading (7 programs)

| File | Stage | Purpose | Reference |
|------|-------|---------|-----------|
| `lighting/deferred_lighting_ps.hlsl` | PS | Main deferred lighting pass | Reads GBuffer + depth, shades with directional/point/spot lights, applies IBL, CSM shadows, contact shadows, GTAO, fog. Includes shadow debug grid (3×3 visualization) |
| `lighting/fullscreen_vs.hlsl` | VS | Fullscreen triangle for deferred pass | Renders via SV_VertexID (3 verts, no VB) to cover whole screen |
| `lighting/shadow_depth_vs.hlsl` | VS | Shadow depth-only pass | Outputs position only, no PS; used for depth prepass and shadow atlas rendering |
| `lighting/cluster_assign_cs.hlsl` | CS | Light clustering for forward+ | Assigns point/spot lights to 3D clusters (frustum-aligned grid) |

---

## Compute Shaders (8 programs)

| File | Purpose | Notes |
|------|---------|-------|
| `compute/gpu_cull_cs.hlsl` | GPU frustum + HiZ occlusion culling | Reads HiZ pyramid, depth bounds; outputs visible instance indices |
| `compute/hiz_pyramid_cs.hlsl` | Hierarchical-Z pyramid generation | Min-max downsampling for fast occlusion testing |
| `compute/draw_compact_cs.hlsl` | Compact sparse visible indices into dense indirect args | Post-cull GPU sort for cache-friendly rendering |
| `compute/decal_apply_cs.hlsl` | DBuffer→GBuffer merge (fullscreen) | Alpha-weighted lerp of decal data into GBuffer |
| `compute/mipgen_cs.hlsl` | Generic mipmap generation | Box filter downsampling for any texture format |

---

## IBL Pipeline (4 programs)

| File | Stage | Purpose | Reference |
|------|-------|---------|-----------|
| `ibl/brdf_lut_cs.hlsl` | CS | Pre-integrate BRDF split-sum LUT (512×512 RG16F) | GGX importance sampling; (f0 response, roughness attenuation) |
| `ibl/irradiance_cs.hlsl` | CS | Diffuse irradiance cubemap (32×32) | Cosine-weighted hemisphere integral over environment |
| `ibl/prefilter_cs.hlsl` | CS | Prefiltered specular cubemap with roughness mips (128×128, 5 mips) | GGX importance sampling per-roughness level |
| `ibl/equirect_to_cubemap_cs.hlsl` | CS | Equirectangular HDR→cubemap faces | Convert panorama format to cubemap for IBL |

---

## Global Illumination (3 programs)

| File | Purpose | Notes |
|------|---------|-------|
| `gi/mesh_sdf_bake_cs.hlsl` | Per-mesh signed distance field (32³ volume) | Bakes geometry SDF for distance-based indirect shadows |
| `gi/global_sdf_composite_cs.hlsl` | Composite mesh SDFs into global distance field | Merges per-mesh SDF into world-space clipmap grids |
| `gi/radiance_cache_trace_cs.hlsl` | Trace radiance probes through global SDF | Computes indirect radiance at probe positions for SSGI |

---

## Shadow Techniques (3 programs)

| File | Purpose | Reference |
|------|---------|-----------|
| `shadows/dfs_cone_trace_cs.hlsl` | Distance field shadow cone tracing | Traces rays through global SDF for soft contact shadows |
| `shadows/dfs_cull_objects_cs.hlsl` | Cull objects for distance field shadow atlas | Frustum+depth test for efficient shadow rendering |
| `shadows/dfs_upsample_cs.hlsl` | Upsample half-resolution DFS result | Bilateral filtering to full resolution |

---

## Screen-Space GI (4 programs)

| File | Purpose | Notes |
|------|---------|-------|
| `ssgi/ssgi_trace_cs.hlsl` | Screen-space GI ray marching | Multiple directions per pixel, Hi-Z acceleration |
| `ssgi/ssgi_spatial_denoise_cs.hlsl` | Spatial bilateral denoiser for SSGI | Edge-aware filtering across edges |
| `ssgi/ssgi_temporal_accum_cs.hlsl` | Temporal accumulation for SSGI | History blend with reject-on-disocclusion |
| `ssgi/ssgi_upsample_cs.hlsl` | Upsample half-resolution SSGI to full | Reprojection-aware upsampling |
| `ssgi/prev_color_reduce_cs.hlsl` | Downsample previous frame color | Used for SSGI temporal reprojection |

---

## Post-Process Effects (30 programs)

### Tone Mapping & Color Grading

| File | Purpose | Reference |
|------|---------|-----------|
| `postprocess/tonemap_ps.hlsl` | ACES FilmToneMap + color grading LUT | 3-segment S-curve, film grain, vignette, chromatic aberration |
| `postprocess/color_grading_lut_cs.hlsl` | Generate 32³ color grading 3D LUT | White balance, shadows/midtones/highlights, blue correction |
| `postprocess/exposure_histogram_cs.hlsl` | 64-bin luminance histogram | Per-frame brightness distribution analysis |
| `postprocess/exposure_average_cs.hlsl` | Weighted average from histogram + temporal adaptation | Auto-exposure with time-averaged smoothing |

### Antialiasing

| File | Purpose | Reference |
|------|---------|-------|
| `postprocess/fxaa_ps.hlsl` | FXAA 3.11 edge-adaptive antialiasing | Fast single-pass edge-based AA |
| `postprocess/smaa_edge_ps.hlsl` | SMAA edge detection pass | Enhanced Subpixel Morphological AA |
| `postprocess/smaa_blend_ps.hlsl` | SMAA blending weight computation | Per-edge directional weights |
| `postprocess/smaa_resolve_ps.hlsl` | SMAA final resolve with neighborhood weights | Blends edges with computed weights |
| `postprocess/taa_resolve_cs.hlsl` | Temporal anti-aliasing resolve | YCoCg neighbourhood clamping, Halton jitter accumulation |

### Upsampling & Super-Resolution

| File | Purpose | Reference |
|------|---------|-------|
| `postprocess/tsr_resolve_cs.hlsl` | Temporal super-resolution (3-pass) | Dilate + reject + history blend for 2× upsampling |
| `postprocess/fsr_easu_cs.hlsl` | AMD FSR 1.0 EASU (edge-adaptive spatial upsampling) | Directional edge reconstruction upscaler |
| `postprocess/fsr_rcas_cs.hlsl` | AMD FSR 1.0 RCAS (contrast-adaptive sharpening) | Post-EASU detail enhancement pass |

### Reflections

| File | Purpose | Reference |
|------|---------|-------|
| `postprocess/ssr_trace_cs.hlsl` | Hi-Z screen-space reflection ray marching | Hierarchical-Z acceleration, binary search |
| `postprocess/ssr_resolve_cs.hlsl` | SSR resolve with Fresnel fallback to IBL | Blends ray result with environment reflection |
| `postprocess/ssr_temporal_cs.hlsl` | Temporal filter for SSR denoising | History-based noise reduction |

### Ambient Occlusion

| File | Purpose | Reference |
|------|---------|-------|
| `postprocess/gtao_cs.hlsl` | Ground Truth Ambient Occlusion (GTAO) | Screen-space occlusion via horizon angle search |
| `postprocess/gtao_denoise_cs.hlsl` | GTAO spatial bilateral denoiser | Edge-aware filter along depth discontinuities |
| `postprocess/gtao_temporal_cs.hlsl` | GTAO temporal accumulation | Multi-frame filtering with history reprojection |
| `postprocess/gtao_upsample_ps.hlsl` | Upsample half-resolution GTAO to full | Depth-aware nearest-neighbor reprojection |

### Bloom

| File | Purpose | Reference |
|------|---------|-------|
| `postprocess/bloom_threshold_cs.hlsl` | Extract bright pixels with Karis average (1/(1+luma)) | Prevents firefly artifacts from extreme highlights |
| `postprocess/bloom_downsample_cs.hlsl` | Compute-based bloom mip chain downsampling | Configurable filter kernel |
| `postprocess/bloom_downsample_ps.hlsl` | Graphics-based bloom downsampling | Tent filter, 2× resolution reduction per level |
| `postprocess/bloom_blur_h_ps.hlsl` | Horizontal Gaussian blur for bloom | Separable blur 1st pass |
| `postprocess/bloom_blur_v_ps.hlsl` | Vertical Gaussian blur for bloom | Separable blur 2nd pass |
| `postprocess/bloom_upsample_cs.hlsl` | Bloom mip chain upsampling (additive tent) | Taps 4 parents per pixel, additive blend |
| `postprocess/bloom_composite_ps.hlsl` | Composite final bloom onto scene color | Lerp-based blending with bloom intensity |

### Motion & Depth Effects

| File | Purpose | Reference |
|------|---------|-------|
| `postprocess/velocity_ps.hlsl` | Per-pixel velocity buffer (current - previous projection) | Reprojection basis for temporal effects |
| `postprocess/motion_blur_cs.hlsl` | Per-pixel gather motion blur | Depth-aware velocity-weighted sampling |
| `postprocess/motionblur_tilemax_cs.hlsl` | Compute per-tile max velocity (preprocessing) | Fast neighbor lookup for motion blur |
| `postprocess/motionblur_neighbormax_cs.hlsl` | Neighbor-max velocity in 3×3 tiles | Second-pass velocity dilation for blur coverage |
| `postprocess/dof_setup_cs.hlsl` | Depth of field CoC computation | Circle of confusion radius from depth |
| `postprocess/dof_blur_cs.hlsl` | Disk gather bokeh blur | Per-pixel adaptive disk blur sampling |

### Visual Effects

| File | Purpose | Notes |
|------|---------|-------|
| `postprocess/lensflare_cs.hlsl` | Screen-space lens flare | Ghosts + halo + chromatic aberration from bright spots |
| `postprocess/cas_sharpen_ps.hlsl` | AMD Contrast Adaptive Sharpening (CAS) | Spatially-aware detail enhancement |

---

## Debug Visualization (2 programs)

| File | Purpose | Notes |
|------|---------|-------|
| `debug/debug_view_ps.hlsl` | Debug visualization modes | Normals, roughness, metallic, AO, depth, cascade indices, etc. |
| `debug/vis_fullscreen_ps.hlsl` | Fullscreen texture visualization utility | For rapid debugging of intermediate textures |

---

## Shared Include Libraries (20 files)

### Core Shading

| File | Provides | Usage |
|------|----------|-------|
| `include/pbr_common.hlsli` | GGX NDF, SmithJoint G, Fresnel, Burley diffuse, multi-scatter energy compensation | All lighting/PBR shaders |
| `include/ibl.hlsli` | IBL helpers (Hammersley, ImportanceSampleGGX, CubeFaceDirection, ComputeEnvLOD, IntegrateBRDF) | IBL shaders, deferred lighting |
| `include/sh_irradiance.hlsli` | Spherical harmonics projection/evaluation | Sky irradiance, probe-based GI |

### Lighting & Shadows

| File | Provides | Usage |
|------|----------|-------|
| `include/light_types.hlsli` | Light data structures (PointLightGPU, SpotLightGPU, DirectionalLightGPU) | Deferred lighting, forward shading |
| `include/lighting.hlsli` | Point/spot/directional light evaluation, attenuation | All lighting passes |
| `include/shadow_common.hlsli` | Shadow map sampling, PCF, cascade selection (CSM) | Deferred lighting, forward, contact shadows |
| `include/pcss.hlsli` | Percentage-Closer Soft Shadows (blocker search + variable penumbra) | Shadow mapping |
| `include/shadow_debug.hlsli` | Shadow debug visualization grid (3×3 buffer tiles) | Deferred lighting (`--shadow-debug 1`) |
| `include/contact_shadows.hlsli` | Screen-space contact shadow ray marching | Deferred lighting (heightmap self-shadows) |

### Distance Fields & GI

| File | Provides | Usage |
|------|----------|-------|
| `include/dfs_common.hlsli` | Distance field shadow utilities | DFS cone trace, DFS cull, casters |
| `include/sdf_common.hlsli` | Signed distance field functions | Mesh/global SDF tracing |
| `include/radiance_cache_common.hlsli` | Radiance probe sampling, lookup | SSGI, radiance cache traces |

### Atmosphere

| File | Provides | Usage |
|------|----------|-------|
| `include/atmosphere_lut.hlsli` | Transmittance/multi-scatter/irradiance LUT sampling | Sky, aerial perspective, height fog |
| `include/aerial_perspective.hlsli` | Aerial perspective volume sampling | Distance fog, forward/deferred shading |
| `include/units.hlsli` | Unit conversion (cm↔km for atmosphere) | Atmosphere LUT sampling |

### Surface Details

| File | Provides | Usage |
|------|----------|-------|
| `include/pom.hlsli` | Parallax Occlusion Mapping (steep parallax + linear interp) | Mesh PS (displacement detail) |
| `include/octahedral.hlsli` | Octahedral normal encoding/decoding | GBuffer compression (16-bit normal), deferred lighting |

### Post-Processing

| File | Provides | Usage |
|------|----------|-------|
| `include/film_tonemap.hlsli` | ACES 3-segment tonemap curve with artist parameters | Tonemap PS, exposure pipeline |

### Utilities

| File | Provides | Usage |
|------|----------|-------|
| `include/depth_common.hlsli` | Reversed-Z depth utilities, linear depth conversion | All shaders using depth (CSM, occlusion, etc.) |
| `include/math_util.hlsli` | Math utilities (PI, saturate, remap, noise, sample distributions) | All computation shaders |

---

## Compile Targets

### RHI Support
All shaders compile for **multiple RHI backends**:
- **SM6.6 (DX12/Vulkan)**: Bindless resources, descriptor heaps, wave intrinsics
- **SM5 (DX11)**: Traditional root signatures, descriptor tables, no bindless
- **Metal**: MSL translation via DXC backend

### Variant System
Shaders support compile-time variants via `-D` flags:
- `UZ_BINDLESS` — Enable bindless resource access (SM6.6+)
- `UZ_INSTANCED` — Enable GPU instancing (requires bindless)
- `UZ_REVERSED_Z` — Enable reversed-Z depth (default on DX12/Vulkan)
- `UZ_DEBUG_*` — Debug visualization modes
- `SHADOW_DEBUG_MODE` — Shadow grid visualization

---

## Build & Integration

### Shader Manager
All shaders compile via `ShaderManager::CompileShader()`:
```cpp
/// Compiles HLSL to bytecode (DXIL/SPIR-V) via DXC compiler.
/// Entry point: "main" (ALWAYS, never PSMain/VSMain)
/// Output: cached bytecode in .zig-cache/
Result<ShaderBytecode> CompileShader(
    const std::string& hlsl_source,
    const std::string& entrypoint, // "main"
    const std::string& profile,     // "ps_6_6", "cs_6_6", etc.
    const std::vector<std::string>& defines // {"-DFLAGS"}
);
```

### Rebuild Cache Invalidation
Shader header changes require cache clear:
```bash
# After modifying ANY .hlsli or shader
rm -rf runtime/build/generated/shaders runtime/.zig-cache
cd runtime && zig build
```

---

## Shader Source Citations

Every shader follows this documentation pattern:
```hlsl
/// @ref Author Year "Paper Title" — technique description
/// @constants <named constants and published default values>
```

**Example** (from deferred_lighting_ps.hlsl):
```hlsl
/// @ref Karis 2013 "Real Shading in Unreal Engine 4"
/// @ref Ramamoorthi 2001 "An Efficient Representation for Irradiance Environment Maps"
/// @constants None (all parameters from dynamic CBs)
```

---

## Key Principles

1. **Entry Point is Always `main`** — ShaderManager hardcodes entry="main". Never use PSMain/VSMain/CSMain.

2. **Reversible-Z Depth** — UzEngine uses reversed-Z (DX12/Vulkan standard). Depth range [1.0, 0.0], comparison: `>= 1.0` for opaque, `> 0.0` for transparent.

3. **Bindless by Default** — SM6.6 path uses `ResourceDescriptorHeap[]` indexing. SM5 fallback uses descriptor tables.

4. **Published References Only** — Every shader must cite papers/standards. No guessing at parameters.

5. **Performance First** — Shaders prioritize GPU efficiency over readability (wave intrinsics, texture cache behavior, quantization).

6. **Visual Verification** — All rendering shaders have visual validation via demos + screenshot verification.

---

## Related Documentation

- **Rendering Architecture**: `docs/engine/renderer.md`
- **Render Graph Passes**: `docs/guides/render-graph-guide.md`
- **Shader System Spec**: `docs/specs/shader-system.md`
- **Shader Authoring Guide**: `docs/guides/shader-authoring.md`
- **PBR Reference**: `docs/guides/pbr-guide.md`

