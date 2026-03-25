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

## References & Licenses

Every shader cites its published reference via `@ref` comments. Below is the complete list of papers, techniques, and third-party code used, grouped by license.

### MIT License

| Source | Used In | Description |
|--------|---------|-------------|
| AMD FidelityFX FSR 1.0 (`ffx_fsr1.h`) | `fsr_easu_cs.hlsl`, `fsr_rcas_cs.hlsl` | Edge Adaptive Spatial Upscaling + Robust Contrast-Adaptive Sharpening. AMD 2021 "FidelityFX Super Resolution" |
| AMD FidelityFX CAS (`ffx_cas.h`) | `cas_sharpen_ps.hlsl` | Contrast Adaptive Sharpening. AMD 2019 "FidelityFX Contrast Adaptive Sharpening" |
| SMAA (`SMAA.hlsl`) | `smaa_edge_ps.hlsl`, `smaa_blend_ps.hlsl`, `smaa_resolve_ps.hlsl` | Jimenez 2012 "SMAA: Enhanced Subpixel Morphological Antialiasing" |

### BSD-3 License

| Source | Used In | Description |
|--------|---------|-------------|
| Bruneton Precomputed Atmospheric Scattering (`functions.glsl`) | `transmittance_lut_cs.hlsl`, `single_scattering_lut_cs.hlsl`, `scattering_density_cs.hlsl`, `indirect_scattering_cs.hlsl`, `irradiance_lut_cs.hlsl`, `aerial_perspective_volume_cs.hlsl`, `sky_atmosphere_ps.hlsl`, `sky_cubemap_cs.hlsl`, `atmosphere_lut.hlsli` | Bruneton & Neyret 2017 "Precomputed Atmospheric Scattering" — full precomputation pipeline. Reference: `github.com/ebruneton/precomputed_atmospheric_scattering`. Implements: transmittance LUT (256x64 RGBA16F), multi-scatter LUT (32x32 RGBA16F, Hillaire 2020), single scattering 3D LUT (256x128x32 RGBA16F), scattering density 3D LUT, indirect scattering 3D LUT (4 orders accumulated), irradiance LUT (64x16 RGBA16F), aerial perspective volume (32x32x16 RGBA16F). Functions: `GetRMuMuSNuFromScatteringTextureUvwz`, `SampleScatteringTexture`, `SampleScatteringTexture4`, `GetSkyRadiance`, `GetSkyRadianceToPoint`, `GetTransmittance`, `GetTransmittanceToSun`, `RayIntersectsGround`, `GetIrradianceTextureUvFromRMuS`. |

### Public Domain / NVIDIA

| Source | Used In | Description |
|--------|---------|-------------|
| FXAA 3.11 (`Fxaa3_11.ush`) | `fxaa_ps.hlsl` | Lottes 2009/2011 "Fast Approximate Anti-Aliasing". NVIDIA public algorithm, no license restriction on reimplementation |

### ACES (Academy Software Foundation License)

| Source | Used In | Description |
|--------|---------|-------------|
| ACES CTL Reference (Academy S-2014-006) | `film_tonemap.hlsli`, `tonemap_ps.hlsl`, `color_grading_lut_cs.hlsl` | ACES tone curve (RRT glow, red modifier, 3-segment S-curve). Academy Color Encoding System reference implementation |

### Published Papers (no code license — algorithm reimplementation)

All techniques below are reimplemented from published papers. No source code was copied; only the mathematical algorithms and constants described in the papers are used.

| Paper | Author(s) | Year | Used In |
|-------|-----------|------|---------|
| "Precomputed Atmospheric Scattering" | Bruneton, Neyret | 2008/2017 | Full precomputation pipeline: transmittance LUT, 3D scattering LUT (4 orders), irradiance LUT, GetSkyRadiance/GetSkyRadianceToPoint lookups, AP volume dual-4D lookup |
| "A Scalable and Production Ready Sky and Atmosphere Rendering Technique" | Hillaire (Epic) | 2020 | `sky_atmosphere_ps.hlsl`, multi-scatter integration |
| "Physically Based and Unified Volumetric Rendering in Frostbite" | Hillaire (EA) | 2015 | `height_fog_ps.hlsl`, volumetric fog inscattering |
| "Physically Based Shading at Disney" | Burley | 2012 | `pbr_common.hlsli` (Burley diffuse), `lighting.hlsli` (SSS wrap diffuse) |
| "Real Shading in Unreal Engine 4" | Karis (Epic) | 2013 | `pbr_common.hlsli` (GGX NDF, Smith visibility, Schlick Fresnel) |
| "Moving Frostbite to PBR" | Lagarde, de Rousiers (EA) | 2014 | `pbr_common.hlsli` (multi-scatter energy compensation), `ibl.hlsli` |
| "Production Friendly Microfacet Sheen BRDF" | Estevez, Kulla | 2017 | `pbr_common.hlsli` (cloth shading) |
| "Ground-Truth Ambient Occlusion" (GTAO) | Jimenez et al. (Activision) | 2016 | `gtao_cs.hlsl`, `gtao_denoise_cs.hlsl`, `gtao_temporal_cs.hlsl` |
| "Practical Real-Time Strategies for Accurate Indirect Occlusion" | Jimenez et al. | 2016 | GTAO spatial filter, mip selection from screen radius |
| "High Quality Temporal Supersampling" | Karis (Epic) | 2014 | `taa_resolve_cs.hlsl` (YCoCg clamping, Halton jitter, HDR weighting) |
| "Temporal Reprojection Anti-Aliasing in INSIDE" | Pedersen (Playdead) | 2016 | `taa_resolve_cs.hlsl` (neighbourhood clamping refinements) |
| "A Reconstruction Filter for Plausible Motion Blur" | McGuire et al. | 2012 | `motion_blur_cs.hlsl`, `motionblur_tilemax_cs.hlsl`, `motionblur_neighbormax_cs.hlsl` |
| "Percentage-Closer Soft Shadows" (PCSS) | Fernando (NVIDIA) | 2005 | `pcss.hlsli` (blocker search, variable penumbra estimation) |
| "Cascaded Shadow Maps" | Engel | 2006 | `shadow_common.hlsli` (cascade selection, per-cascade PCF radius) |
| "Cascaded Shadow Maps" | Dimitrov, Bavoil | 2007 | `shadow_common.hlsli` (smooth cascade transitions) |
| "Screen-Space Contact Shadows" | Wihlidal (EA) | 2017 | `contact_shadows.hlsli` (screen-space ray march) |
| "Distance Field Soft Shadows" | Wright (Epic) | 2015 | `dfs_cone_trace_cs.hlsl`, `dfs_cull_objects_cs.hlsl`, `dfs_common.hlsli` |
| "Sphere Tracing" | Hart | 1996 | `dfs_cone_trace_cs.hlsl` (SDF ray marching foundation) |
| "Real-Time Rendering of Procedural Multiscale Cloud" | Schneider (Guerrilla) | 2015 | `cloud_raymarch_cs.hlsl` (Worley noise, weather map, beer-powder) |
| "The Real-Time Volumetric Cloudscapes of Horizon Zero Dawn" | Schneider, Vos | 2017 | Cloud noise pipeline, temporal reprojection |
| "Hi-Z Screen-Space Reflections" | Uludag (AMD) | 2014 | `ssr_trace_cs.hlsl` (hierarchical ray march) |
| "Stochastic Screen-Space Reflections" | Stachowiak (EA) | 2015 | `ssr_resolve_cs.hlsl`, `ssr_temporal_cs.hlsl` |
| "Screen Space Indirect Lighting with Visibility Bitmask" | Jimenez | 2023 | `ssgi_trace_cs.hlsl` (multi-direction screen-space GI) |

### UzEngine Original

All shaders not listed above are original UzEngine implementations with no third-party code dependencies:

- Editor grid, overlay, stats display
- GBuffer write/read
- Deferred lighting composition
- Decal projection + DBuffer merge
- IBL baking (BRDF LUT, irradiance, prefiltered specular, equirect-to-cubemap)
- Mesh SDF baking + global SDF compositing
- Bloom pyramid (threshold, downsample, upsample, composite)
- Color grading LUT generation
- Exposure histogram + adaptation
- Depth of field (CoC + bokeh gather)
- Lens flare
- Velocity buffer
- GPU culling (frustum + HiZ)
- HiZ pyramid
- Mipmap generation
- TSR (Temporal Super Resolution)
- Debug visualization modes

---

## Related Documentation

- **Rendering Architecture**: `docs/engine/renderer.md`
- **Render Graph Passes**: `docs/guides/render-graph-guide.md`
- **Shader System Spec**: `docs/specs/shader-system.md`
- **Shader Authoring Guide**: `docs/guides/shader-authoring.md`
- **PBR Reference**: `docs/guides/pbr-guide.md`

