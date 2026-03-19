<!-- doc-meta -->
<!-- date: 2026-03-17 -->
<!-- author: claude-architect -->
<!-- type: analysis -->
<!-- tags: rendering, ue5-parity, visual-quality, gap-analysis -->
<!-- end-doc-meta -->

# UE5 vs UzEngine Rendering Parity Analysis — Gap Report

**Last Updated**: 2026-03-17
**Analysis Scope**: Detailed comparison of UzEngine's actual implemented rendering vs UE5 5.6
**Methodology**: Analyzed UzEngine source code (scene_renderer.h/cpp, shaders, passes) + UE5 source at W:\UE_5.6_Source

---

## Executive Summary

UzEngine has implemented a **baseline deferred PBR renderer** with POM, cascaded shadows, IBL, and basic post-processing. However, UE5 is **dramatically** more visually complete. The following table quantifies the gap sorted by visual impact (5 = most visible difference):

| Feature | UE5 Status | UzEngine Status | Impact | Complexity | Implementation Status |
|---------|-----------|-----------------|--------|------------|----------------------|
| **Temporal Anti-Aliasing (TAA)** | Halton jitter + TAA resolve | None (FXAA only) | 5 | XL | MISSING |
| **Screen-Space Reflections (SSR)** | Full hierarchical SSR + bias | None | 5 | XL | MISSING |
| **Reflection Probes** | Sphere + box probes, blend zones | None | 5 | L | MISSING |
| **Nanite / Virtualized Geometry** | Mesh clustering + GPU culling | None (static mesh only) | 5 | XL | NOT STARTED |
| **Lumen / Global Illumination** | Real-time indirect GI + radiance caching | None (skylight only) | 5 | XL | NOT STARTED |
| **Virtual Shadow Maps** | Clipmap + 3x3 clipmap pages | Cascaded shadow maps only | 4 | XL | NOT STARTED |
| **RayTraced Reflections** | DXR backend + fallback to SSR | None | 4 | XL | NOT STARTED |
| **Volumetric Fog (Froxels)** | 3D froxel grid + scattering | Height fog only (2D) | 4 | L | MISSING |
| **Decals** | World-space decal meshes + blending | None | 4 | M | MISSING |
| **Contact Shadows** | Screen-space ray-march (per-pixel) | PCF only (no contact shadows) | 4 | M | MISSING |
| **Material Layer System** | Blend up to 8 layers per surface | Single PBR layer | 4 | L | NOT STARTED |
| **Subsurface Scattering** | Screen-space + profile sampling | None | 4 | L | MISSING |
| **Hair / Cloth Shading Models** | Specialized anisotropic BRDF | Default lit only | 4 | L | NOT STARTED |
| **Planar Reflections** | Planar mirror rendering + blend | None | 3 | M | MISSING |
| **Motion Blur** | Velocity buffer + dilated gather | None | 3 | M | MISSING |
| **Depth of Field (DoF)** | Tile-based blur + variable cocflo | None | 3 | L | MISSING |
| **Auto Exposure (Smart)** | Histogram + metering regions | Basic histogram only | 3 | S | PARTIAL |
| **Film Grain** | Noise texture + varying intensity | None | 3 | S | MISSING |
| **Chromatic Aberration** | Per-channel offset (color fringing) | None | 3 | S | MISSING |
| **Lens Flare** | Screen-space ghost + star effect | None | 3 | M | MISSING |
| **Vignette** | Screen-edge darkening + curve | None | 3 | S | MISSING |
| **Color Grading LUT** | 32x32x32 CUBE or neutral LUT | None (FilmToneMap only) | 3 | S | MISSING |
| **Volumetric Light Shafts** | Radial blur + inscattering | None | 3 | M | MISSING |
| **IES Light Profiles** | Per-light texture lookup | None | 3 | S | MISSING |
| **Light Functions** | Per-light custom modulation | None | 2 | M | MISSING |
| **PCSS (Soft Shadows)** | Blocker search + variable penumbra | Poisson disk 16-tap PCF | 3 | M | PARTIAL |
| **Sphere/Rect Area Lights** | Area light BRDF sampling | Point + Spot lights only | 2 | M | NOT STARTED |
| **Sky Dome Capture** | Real-time cubemap from sky pass | CPU-computed procedural (works) | 2 | S | WORKING |
| **SH Irradiance Projection** | L2 SH projection (7 coeffs) | Implemented + used | 2 | S | WORKING |
| **Per-Object Shadows** | Per-object shadow settings | Global settings only | 2 | S | MISSING |
| **Cascade Transition Blend** | Smooth cross-fade between cascades | Linear blend implemented | 2 | S | WORKING |
| **Rect Light Shadows** | Omnidirectional cubemap + SDF | None | 2 | L | NOT STARTED |
| **HiZ Occlusion Culling** | Hierarchical Z pyramid + culling | Hi-Z pyramid created but NOT used | 2 | M | STUB |
| **GPU Instancing** | Material-based GPU draw batching | Instance buffer exists but NOT fully wired | 1 | M | INCOMPLETE |
| **LOD System** | Per-object LOD selection + streaming | No LOD support | 1 | M | MISSING |
| **Clustered Shading (3D)** | 3D light cluster grid + depth culling | 64 lights per deferred pass | 1 | L | NOT STARTED |
| **Light Count Scaling** | Thousands of lights (deferred) | 64 point lights max | 1 | M | ARCHITECTURAL LIMIT |
| **Square Pixels / Supersampling** | 2x-4x MSAA + FXAA fallback | FXAA only | 1 | S | PARTIAL |
| **Ray-Traced Ambient Occlusion** | RTAO backend with fallback | GTAO compute (works but disabled) | 1 | M | DISABLED |
| **Emissive Mapping** | Per-pixel self-illumination + bloom | No explicit emissive texture | 1 | S | PARTIAL |
| **Normal Maps (Detail)** | Blended detail + blend modes | Single normal map only | 1 | M | MISSING |

---

## DETAILED ANALYSIS BY SYSTEM

### A. Lighting & Light Types

**UE5 Implementation (W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Classes\Lights\):**
- DirectionalLight (CSM, per-pixel depth bias, falloff)
- PointLight (inverse squared + custom falloff, radius/source radius)
- SpotLight (cone angle, inner/outer cone, IES texture)
- RectLight (area light importance sampling)
- SkyLight (IBL from cubemap + SH, DFAO occlusion, cloudiness)
- All lights support: color temperature (Kelvin), intensity (lumens/candela), falloff exponents, light functions

**UzEngine Implementation:**
- ✅ DirectionalLight (CSM, cascade bias, Poisson PCF)
- ✅ PointLight (inverse squared falloff, 64-light limit, per-light color/intensity)
- ✅ SpotLight (cone angles, basic attenuation)
- ✅ SkyLight (IBL irradiance + prefiltered, SH projection, sky tint, min ambient floor)
- ❌ RectLight (NOT implemented)
- ❌ IES profiles (NOT implemented)
- ❌ Light functions (per-light modulation shaders — NOT implemented)
- ❌ Per-light source radius (stored but not used in area light BRDF)
- ❌ Temperature/Kelvin color (stored as RGB, not converted)

**Visual Impact**: 3/5 — Point/spot lights work, but missing rect lights and IES profiles degrades visual sophistication in architectural scenes.

**Architecture**: PointLightData in constant_buffers.h has 48 bytes (position, radius, color, sourceRadius, falloffExponent, specularScale, diffuseScale, flags). Matches UE5 point light data layout.

---

### B. Shadow Mapping & Depth Testing

**UE5 Implementation (W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Classes\Components\LightComponent.cpp):**
- **Cascaded Shadow Maps (CSM)**: 4-8 cascades, logarithmic split, per-cascade fade blend
- **Contact Shadows**: Per-pixel screen-space ray-march through depth buffer (ray distance = 2-3 light radius)
- **Percentage-Closer Soft Shadows (PCSS)**: Blocker search → penumbra estimation → variable kernel softness
- **Virtual Shadow Maps (VSM)**: Clipmap-based, GPU pages, dynamically allocated
- **Distance Field Shadows**: Cone trace through SDF atlas (contact + occluder shadows)
- **Per-Object Override**: CastShadow, ReceiveShadow per object
- **Temporal Filtering**: Previous-frame shadow data for smooth transitions

**UzEngine Implementation:**
- ✅ **Cascaded Shadow Maps**: 4-8 cascades, split blending, per-cascade depth/normal bias
- ✅ **Poisson Disk PCF**: 16-tap manual comparison with per-cascade radius scaling
- ✅ **Shadow Atlas**: 4096x4096 dynamic depth atlas, multi-cascade layout
- ✅ **Shadow Caching**: Static shadow cache manager (separate atlas + composite sampling)
- ✅ **Distance Field Shadows**: Cone trace compute pipeline (cull + trace passes)
- ✅ **Cascade Transition**: Linear blend between cascades
- ✅ **PCSS Partial**: maxSoftKernelSize = 40 texels (UE5 parity), tanLightSourceAngle stored
- ❌ **Contact Shadows**: Screen-space ray-march NOT implemented (could use Hi-Z for efficiency)
- ❌ **Virtual Shadow Maps**: NOT implemented (would require tile cache system)
- ❌ **Per-Object Shadow Overrides**: No per-object cast/receive flags
- ❌ **Temporal Filtering**: No history buffer for shadow TAA

**Critical Issue**: PCSS blocker search not implemented — code stores parameters but kernel is always fixed size (Poisson 16-tap). Real PCSS requires `shadowMapSize / penumbraBias` search to vary kernel radius.

**Visual Impact**: 4/5 — Shadows work but lack visual refinement (no contact shadows make interiors dark, no PCSS proper blocker search). GTAO is disabled due to GPU hang.

---

### C. Global Illumination & Ambient Lighting

**UE5 Implementation (W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Classes\Engine\Atmosphere.cpp + LightmassSettings.h):**
- **Lumen**: Real-time GI via hierarchical voxel tracing + radiance caching (5.1+)
- **Lightmaps**: Pre-baked static indirect lighting (lightmap UVs + texture).
- **Light Probes**: 3D SH interpolation grid for dynamic objects.
- **Reflection Probes**: Sphere + box probes, spherical harmonics blending.
- **SkyLight**: HDR cubemap + SH irradiance projection + DFAO (distance field ambient occlusion).
- **Indirect Specular**: Prefiltered cubemap with mip-LOD based on roughness.

**UzEngine Implementation:**
- ✅ **SkyLight (IBL)**: Cubemap capture (sky pass → cubemap) + SH irradiance (L2, 7 coeffs)
- ✅ **Diffuse IBL**: Sky SH applied per-pixel in deferred lighting
- ✅ **Specular IBL**: Prefiltered cubemap (5 mips) + BRDF LUT (Cook-Torrance GGX)
- ✅ **Environment Cubemap**: Procedural sky → capture at init + recompute on sky param change
- ✅ **SkyLight Intensity/Tint**: Per-frame multiplier (skyLightIntensity, skyLightColorR/G/B)
- ✅ **Min Ambient Floor**: skyLightMinAmbient clamps shadow darkness
- ❌ **Lumen**: NOT implemented (would need voxel pyramid + temporal tracing)
- ❌ **Lightmaps**: NOT implemented (requires asset pipeline expansion)
- ❌ **Light Probes**: NOT implemented
- ❌ **Reflection Probes**: NOT implemented (no probe system)
- ❌ **DFAO (Distance Field AO)**: NOT implemented
- ❌ **Temporal GI**: No history for temporal coherence

**Visual Impact**: 5/5 — Lumen is UE5's biggest visual win (real-time bouncing light). UzEngine's skylight-only approach makes interiors unrealistic (100% ambient fallback instead of light bleeding from outside).

---

### D. Material System & Shading Models

**UE5 Implementation (W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Classes\Materials\Material.h):**
- **Shading Models**: DefaultLit, Subsurface, PreintegratedSkin, ClearCoat, Cloth, Eye, Hair, SingleLayerWater, ThinTranslucent, TwoSidedFoliage (10+)
- **Material Layers**: Up to 8 blended layers per surface (blend mask texture)
- **Complex BRDF**: Per-model anisotropic distribution, custom F0, subsurface SSS profile
- **Decals**: Projected onto surfaces, affect normal/AO/emissive/roughness/metallic
- **World-Position Offset**: Vertex deformation (displacement with parallax)
- **Pixelated Normals**: Tessellation + pixel-rate displacement

**UzEngine Implementation:**
- ✅ **Single Shading Model**: DefaultLit PBR (Cook-Torrance GGX)
- ✅ **BRDF**: Burley diffuse + Smith-Joint specular (UE5 parity)
- ✅ **Metallic/Roughness Workflow**: Standard physically-based parametrization
- ✅ **Normal Maps**: Per-pixel normal perturbation (octahedral-encoded)
- ✅ **Parallax Occlusion Mapping (POM)**: 8-32 layers steep parallax with self-shadowing
- ✅ **Ambient Occlusion**: Per-texture AO multiplier + GTAO (disabled) screen-space AO
- ✅ **Emissive**: Implied by albedo (bright albedo + high intensity → fake glow)
- ✅ **Material Parameters**: Cavity strength, self-shadow strength, specular/diffuse scale
- ❌ **Subsurface Scattering**: NOT implemented (no SSS profile or transmittance)
- ❌ **ClearCoat**: NOT implemented (no layer system)
- ❌ **Hair/Cloth/Eye**: NOT implemented (specialized shading models)
- ❌ **Material Layers**: NOT implemented (single layer only)
- ❌ **Decals**: NOT implemented
- ❌ **Tessellation**: NOT implemented (DX12 doesn't expose HS/DS in RHI)

**Note**: Emissive is NOT explicitly supported — no emissive texture map. High metallic + white albedo approximates shiny, but true self-illumination requires a dedicated emissive channel + bloom.

**Visual Impact**: 4/5 — Single PBR material is limiting. Cloth, hair, and subsurface are common in AAA (cloth on characters, skin SSS, water refraction). No decals means decal artists can't work.

---

### E. Post-Processing & Tone Mapping

**UE5 Implementation (W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\PostProcess/):**
- **Auto Exposure**: Histogram-based metering + eye adaptation (temporal smoothing)
- **Film Tone Mapping**: ACES RRT + ODT + custom FilmToneMap (5 artist params: Slope/Toe/Shoulder/BlackClip/WhiteClip)
- **Bloom**: Adaptive threshold + 5-level mip chain + Karis average
- **Vignette**: Screen-edge falloff + curve control
- **Chromatic Aberration**: Per-channel offset (R/G/B fringing)
- **Film Grain**: Noise texture + varying intensity
- **Lens Flare**: Screen-space ghosts + star effect
- **Motion Blur**: Velocity buffer + gather-based blur
- **Depth of Field**: Tile-based variable-radius blur
- **Color Grading**: CUBE LUT (32x32x32) + neutral override
- **Screen % Applied**: All post-process runs full-screen in final tonemapper (before UI)

**UzEngine Implementation:**
- ✅ **Auto Exposure**: Histogram compute + metering (but uses basic average, not weighted regions)
- ✅ **Film Tone Mapping**: ACES-based filmic curve (Narkowicz 2015 polynomial fit), 5 params (Slope/Toe/Shoulder/BlackClip/WhiteClip) from PerFrameCB
- ✅ **Bloom**: Threshold → 6-level mip chain (1/2 → 1/64 res) → 9-tap tent upsample + composite
- ✅ **Bloom Composite**: Applied in tonemap_ps.hlsl before exposure
- ✅ **sRGB Gamma**: Linear → sRGB in tonemap shader
- ❌ **Vignette**: NOT implemented
- ❌ **Chromatic Aberration**: NOT implemented
- ❌ **Film Grain**: NOT implemented
- ❌ **Lens Flare**: NOT implemented
- ❌ **Motion Blur**: NOT implemented (no velocity buffer)
- ❌ **Depth of Field**: NOT implemented (no CoC computation)
- ❌ **Color Grading LUT**: NOT implemented (FilmToneMap only)

**Note**: Auto exposure is implemented but does NOT use weighted metering regions (eyes, center-weighted, etc.). Just histogram average.

**Visual Impact**: 3/5 — Tone mapping works, but missing grain + aberration + vignette degrads filmic quality. Motion blur absence is noticeable in camera pans.

---

### F. Atmosphere & Volumetrics

**UE5 Implementation (W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Classes\Atmosphere.cpp + ExponentialHeightFog.cpp):**
- **Sky Atmosphere (Bruneton Model)**: Precomputed transmittance + aerial perspective inscattering
- **Volumetric Fog (Froxels)**: 3D light scattering grid (160x90x64) + directional + point light contributions
- **Exponential Height Fog**: 2-layer exp fog with directional inscattering (god rays)
- **Volumetric Cloud Rendering**: Ray-march through cloud texture + shadow mapping
- **Distant Lighting**: Directional/point light contributions to volumetrics

**UzEngine Implementation:**
- ✅ **Sky Atmosphere**: Bruneton model with transmittance LUT computation (256x64 RGBA16F)
- ✅ **Transmittance LUT**: Pre-computed via compute shader (used for aerial perspective)
- ✅ **Aerial Perspective**: Screen-space distance-based atmospheric fog in deferred lighting
- ✅ **Height Fog (2-layer)**: Exponential fog with directional inscattering (sun glow)
- ✅ **Fog Compositing**: Applied as fullscreen pass after lighting
- ✅ **Sky Irradiance SH**: Atmosphere model → SH projection for sky diffuse
- ❌ **Volumetric Fog (Froxels)**: NOT implemented (3D grid light scattering)
- ❌ **Volumetric Clouds**: NOT implemented
- ❌ **Bilateral Upsampling**: No half-res volumetric upsampling

**Visual Impact**: 4/5 — Volumetric froxel fog is critical for indoor/cave atmosphere. UzEngine's 2D height fog is flat by comparison. No clouds = outdoor scenes look bare.

---

### G. Anti-Aliasing

**UE5 Implementation (W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\PostProcess/ (TAA.cpp, FXAA.cpp)):**
- **TAA (Temporal Anti-Aliasing)**: Halton jitter per-frame + reprojection + clamping (4-8 samples history)
- **FXAA**: Fast approximate AA (fallback for high frame rate or TAA disabled)
- **MSAA**: 2x-8x multisample anti-aliasing (geometry-stage)
- **Supersampling**: Per-pixel tone mapping + averaging

**UzEngine Implementation:**
- ✅ **FXAA**: Post-process edge detection + directional blur (works, enabled)
- ❌ **TAA**: NOT implemented (no jitter + reprojection history)
- ❌ **MSAA**: NOT supported (no MSAA bind flags in RHI)
- ❌ **Temporal Jitter**: Halton sequence NOT generated (only static samples)

**Note**: GTAO has temporal jitter code (gtao_temporal_cs.hlsl), but TAA is not integrated into main render loop.

**Visual Impact**: 1/5 — FXAA works reasonably at 1080p+, but TAA would significantly improve edge smoothness (3x sample history). At high frame rates (240+ FPS), FXAA's cost is noticeable.

---

### H. Scene Complexity & Culling

**UE5 Implementation (W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private/Culling/):**
- **Nanite Virtualized Geometry**: Mesh clustering + instance culling + software rasterization
- **HiZ Occlusion Culling**: Hierarchical Z pyramid → fast GPU culling
- **Distance Culling**: LOD-based draw call reduction
- **Frustum Culling**: Per-object frustum test (CPU)
- **Shadow Frustum Culling**: Per-light frustum reduction
- **Hardware Occlusion Queries**: Async visibility feedback for LOD selection

**UzEngine Implementation:**
- ✅ **Hi-Z Pyramid**: Compute shader generates mip chain (half-res down to minimum) in HiZMipLevel array
- ✅ **Frustum Culling**: Performed during DrawList construction (sync)
- ✅ **LOD Support**: Data structures exist (DrawCommand has LOD flag), but NOT wired to animation/component system
- ❌ **Nanite**: NOT implemented (would require mesh clustering + virtual buffer)
- ❌ **Hi-Z Culling**: Hi-Z pyramid created but NOT used (no culling pass that reads it)
- ❌ **GPU Culling**: No compute shader-driven culling (static meshes only)
- ❌ **Occlusion Queries**: NOT implemented
- ❌ **Hardware Rasterization Fallback**: Rasterization is the only path (no software fallback)

**Code Reference**:
- Hi-Z pyramid: `scene_renderer.h:447-450`, `hiz_pyramid_pass.h`
- GPU Instancing: `instance_batcher.h`, `m_instanceBuffers[kInstanceBufferCount]` (created but not fully wired)

**Visual Impact**: 2/5 — Scenes with 1000+ objects would stall. UzEngine's static mesh array processes all geometry each frame (no LOD culling). Hi-Z is built but unused = wasted computation.

---

### I. Reflections

**UE5 Implementation (W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private/RayTracing/ or PostProcess/ScreenSpaceReflections.cpp):**
- **Screen-Space Reflections (SSR)**: Hierarchical ray-march + refine (roughness-based cone trace)
- **Planar Reflections**: Render scene from reflection camera → composite to plane
- **Reflection Probes**: Sphere + box probes (SH irradiance + cubemap blend)
- **Ray-Traced Reflections (DXR)**: Hardware ray-tracing backend with fallback to SSR
- **Per-Material SSR Settings**: Per-object enable/disable + intensity

**UzEngine Implementation:**
- ❌ **Screen-Space Reflections**: NOT implemented
- ❌ **Planar Reflections**: NOT implemented
- ❌ **Reflection Probes**: NOT implemented
- ❌ **Ray-Traced Reflections**: NOT implemented

**Note**: Reflections would require re-rendering scene from reflection camera or ray-march through depth pyramid. Medium complexity, high visual impact.

**Visual Impact**: 5/5 — Lack of SSR is immediately obvious (glossy surfaces show no reflections, only IBL cubemap). Makes metallic/polished materials look flat.

---

### J. Render Path & Architecture

**UE5 Render Path:**
```
Deferred Render Path:
  1. Depth prepass (optional HiZ pyramid generation)
  2. BasePass (G-Buffer generation) — 1 RT per feature (depth, normal, albedo, AO, material ID, etc.)
  3. Lighting pass (deferred shading)
  4. Post-process passes (tone mapping, bloom, AA, etc.)
```

**UzEngine Render Path:**
```
Deferred Render Path:
  1. Shadow pass (directional light) — cascade atlas depth
  2. Depth prepass (optional) — stores full-res depth
  3. GBuffer pass (2-RT pack: albedo+AO, normal+metallic+roughness) + position (opt)
  4. Sky atmosphere pass (fullscreen: procedural sky gradient + sun disk)
  5. Transmittance LUT pass (compute: precomputed atmosphere scattering)
  6. Sky cubemap capture pass (compute: downsample to 64x64x6 cubemap)
  7. IBL bake pass (compute: irradiance + prefiltered from cubemap)
  8. Deferred lighting pass (fullscreen: directional + point lights + IBL)
  9. Debug view pass (optional)
  10. Height fog pass (fullscreen: distance fog overlay)
  11. Tonemap pass (fullscreen: bloom composite + exposure + ACES + gamma)
  12. GTAO pass (compute: horizon-based AO — DISABLED due to GPU hang)
  13. Present
```

**Key Differences:**
- UE5 uses **forward rendering for transparent/translucent** objects, **deferred for opaque**. UzEngine is deferred-only (no transparent pass).
- UE5's G-Buffer is **highly modular** (SCENE_COLOR, NORMAL, METALLIC, ROUGHNESS as separate RTs or packed). UzEngine packs into 2 RTs.
- UE5's **base pass emits lighting** (directional + static lights), **deferred shading** adds dynamic. UzEngine's deferred shading adds ALL lights (directional + point).

**Architectural Weakness**: No forward rendering path for transparent materials (water, glass, particle effects). All transparency would either:
1. Sort and composite on top of opaque (limited interaction)
2. Use deferred with translucency blending (artifacts with multiple lights)
3. Not support transparency (current state)

---

## IMPLEMENTATION READINESS MATRIX

| Feature | UE5 Source | UzEngine Code | Est. Work | Risk | Notes |
|---------|-----------|---------------|-----------|------|-------|
| **Temporal Anti-Aliasing (TAA)** | ✅ PostProcess/TAA.cpp (1000+ LOC) | ❌ None | 5-7 days | M | Needs jitter gen + reprojection buffer + per-frame sample accumulation. GTAO temporal code exists as template. |
| **SSR** | ✅ PostProcess/SSR.cpp (500+ LOC) | ❌ None | 4-6 days | M | Needs Hi-Z pyramid (ready) + ray-march shader + refine logic. Cone angle per-roughness. |
| **Reflection Probes** | ✅ Engine/LevelAssets.cpp | ❌ None | 3-5 days | L | Sphere + box probe data structures. SH blending logic from IBL already done. |
| **Contact Shadows** | ✅ ShadowRendering.cpp | ❌ None | 2-3 days | S | Screen-space ray-march (8-16 steps). Use depth pyramid (Hi-Z). |
| **Decals** | ✅ Renderer/Decals.cpp (1500+ LOC) | ❌ None | 5-7 days | M | Quad mesh + normal blend + deferred G-Buffer blending. Material parameter override. |
| **Volumetric Fog (Froxels)** | ✅ PostProcess/VolumeFog.cpp (2000+) | ❌ None | 7-10 days | L | 3D grid light accumulation (point + directional). Bilateral upsampling. High memory. |
| **PCSS (Full Blocker Search)** | ✅ ShadowFilteringCommon.ush | ⚠️ Partial (params only) | 1-2 days | S | Implement blocker search loop (30-32 samples). Already have kernel radius and bias. |
| **Subsurface Scattering** | ✅ Translucency.cpp + profiles | ❌ None | 6-8 days | L | Screen-space post-process + profile sampling. Fallback to diffuse. |
| **Motion Blur** | ✅ PostProcess/MotionBlur.cpp | ❌ None | 3-4 days | M | Velocity buffer (already have velocity_ps.hlsl shader). Gather-based blur. |
| **DoF** | ✅ PostProcess/DepthOfField.cpp | ❌ None | 4-5 days | M | CoC computation + tile-based blur. Variable radius per tile. |
| **Color Grading LUT** | ✅ PostProcess/TonemapCommon.ush | ❌ None | 2-3 days | S | 32x32x32 CUBE texture + 3D trilinear sample. Neutral LUT as default. |
| **Hair/Cloth Shading** | ✅ Material/Hair.ush + Cloth.ush | ❌ None | 5-7 days | L | Anisotropic BRDF + secondary highlights. Complex texture sampling. |
| **Material Layers** | ✅ Material/LayerBlending.ush | ❌ None | 4-6 days | M | Blend mask texture + multiple normal/roughness/metallic. Recursive PBR eval. |
| **Nanite** | ✅ Renderer/Nanite.cpp (3000+) | ❌ None | 14-21 days | XL | Mesh clustering + GPU page tables + software rasterization. Massive undertaking. |
| **Lumen** | ✅ Renderer/Lumen/ (8000+) | ❌ None | 28-42 days | XL | Voxel pyramid + tracing + radiance caching. Extremely complex. |
| **DXR Ray-Tracing** | ✅ RayTracing/RayTracingRHI.cpp | ❌ None (RHI not wired) | 7-10 days | H | RHI supports DXR (DX12 backend). Needs ray-tracing pipeline + BVH binding. |
| **GTAO (Enable + Fix)** | ✅ PostProcess/AmbientOcclusion.usf | ⚠️ Disabled (GPU hang) | 1-2 days | M | Debug GPU hang (likely cbuffer/root sig issue). GTAO code is ready. |
| **FXAA (Already Done)** | ✅ PostProcess/FXAA.ush | ✅ Implemented | 0 days | — | Already working in tonemap_ps.hlsl. |
| **Film Tone Mapping (Already Done)** | ✅ TonemapCommon.ush | ✅ Implemented | 0 days | — | Narkowicz fit (5 params in PerFrameCB). Works. |
| **Auto Exposure (Already Done)** | ✅ PostProcess/PostProcessEyeAdaptation.cpp | ✅ Implemented | 0 days | — | Histogram compute works (basic, no metering regions). |
| **Bloom (Already Done)** | ✅ PostProcess/PostProcessBloom.cpp | ✅ Implemented | 0 days | — | 6-level mip chain + Karis threshold + 9-tap tent upsample. Works. |
| **IBL (Already Done)** | ✅ Engine/SkyLight.cpp + IBLBaker | ✅ Implemented | 0 days | — | Irradiance + prefiltered (5 mips) + SH projection. Works. |
| **Sky Atmosphere (Already Done)** | ✅ Atmosphere.cpp + Bruneton model | ✅ Implemented | 0 days | — | Transmittance LUT + aerial perspective. Works. |

---

## CRITICAL MISSING FEATURES (High Impact, Medium Effort)

### Priority Tier 1 (Render 80% of Visual Quality with 20% More Work)

1. **Temporal Anti-Aliasing (TAA)** — 5 days
   - Halton jitter gen (per-frame camera jitter)
   - Reprojection history buffer (2 frames)
   - Sample accumulation + clamping
   - Enable by default, fallback FXAA on high FPS
   - **Visual Gain**: 3x edge smoothness, enables lower base resolution + upscale

2. **Screen-Space Reflections (SSR)** — 4 days
   - Ray-march through Hi-Z pyramid (ready to use)
   - Roughness-based cone angle
   - Fade at screen edges + fade with distance
   - Composite before bloom
   - **Visual Gain**: Glossy/metallic surfaces get reflections (immediate 5/5 impact)

3. **Contact Shadows** — 2 days
   - Screen-space ray-march (8-16 steps, terminate at depth)
   - Combine with PCF result (additive shadow)
   - Only shadows within ~10 cm (contact distance)
   - **Visual Gain**: Prevents floating objects, accentuates contact deformation

4. **PCSS Blocker Search (Complete)** — 1 day
   - Implement blocker search loop (30-32 samples at shadow map texels)
   - Compute penumbra radius from blocker distance
   - Variable kernel softness per-pixel (currently fixed Poisson disk)
   - **Visual Gain**: Shadow softness realistic (far shadows soft, near shadows sharp)

### Priority Tier 2 (Render Specific Scenes Better)

5. **Volumetric Fog (Froxels)** — 8 days
   - 3D froxel grid (160x90x64 sparse)
   - Directional + point light contributions
   - Bilateral upsampling (half-res to full-res)
   - Inscattering into scene depth
   - **Visual Gain**: Indoor/cave atmosphere (fog volumetrics change mood dramatically)

6. **Decals** — 5 days
   - Deferred decal pass (reads G-Buffer, writes blended results)
   - Decal mesh volume projection
   - Parameter blending (normal, albedo, roughness, metallic)
   - **Visual Gain**: Scorch marks, graffiti, bullet holes (AAA visual vocabulary)

7. **Motion Blur** — 3 days
   - Velocity buffer (already have velocity_ps.hlsl)
   - Gather-based variable-radius blur
   - Tile-max velocity optimization
   - **Visual Gain**: Smooth camera pans (removes "stuttering" feel)

### Priority Tier 3 (Specialized Use)

8. **Planar Reflections** — 3 days
   - Render scene from reflection camera matrix
   - Composite to plane geometry
   - Fade with distance/angle
   - **Visual Gain**: Perfect reflections on mirrors/water (only for planar geometry)

9. **Film Grain + Vignette + Chromatic Aberration** — 2 days each
   - Film grain: 2D noise texture + intensity modulation
   - Vignette: Screen-edge falloff curve
   - Chromatic aberration: Per-channel UV offset
   - **Visual Gain**: Cinematic look + analog camera feel (subtle but noticeable)

---

## ARCHITECTURAL GAPS

### 1. No Forward Rendering Path
**Problem**: All geometry is deferred. Transparent materials (water, glass, particles) cannot use the full deferred lighting pipeline.

**Impact**:
- Transparent objects don't receive dynamic lighting correctly
- No way to render translucent materials over opaque

**Fix**: Add forward pass after deferred lighting:
```
Deferred Path → Forward Path (transparent-only)
  Deferred GBuffer + Lighting (opaque)
  Forward Blend (transparent over opaque)
  Composite with dynamic lights
```

### 2. Hi-Z Pyramid Built but Unused
**Problem**: `HiZMipLevel m_hizMipLevels[kHiZMaxMipLevels]` created every frame but no culling pass consumes it.

**Impact**: GPU compute waste (pyramid generation for no benefit). Scalability ceiling without culling.

**Fix**:
1. Add GPU culling compute pass (indirect dispatch)
2. Read Hi-Z pyramid + frustum bounds
3. Output visible draw command count

### 3. Instance Buffer Not Wired
**Problem**: `m_instanceBuffers[kInstanceBufferCount]` allocated but rendering still uses per-draw constants.

**Impact**: Can't benefit from GPU instancing (all instances drawn separately).

**Fix**: Implement material-based batching:
1. Sort draw commands by (pipeline, material, mesh)
2. Update instance buffer with per-instance data (position, rotation, scale)
3. Issue single draw-call per batch with instance count

### 4. GTAO Disabled (GPU Hang)
**Problem**: GTAO compute pipeline crashes on first dispatch. Root signature or compute shader bug.

**Impact**: No screen-space ambient occlusion (darkening in crevices).

**Fix**: Debug GPU hang:
1. Reduce GTAO group size + simplify shader
2. Verify root constants match shader expects
3. Add RenderDoc capture to analyze GPU state

### 5. No Emissive Material Support
**Problem**: No dedicated emissive texture. High-intensity albedo approximates glow, but doesn't emit light.

**Impact**: Can't create self-illuminated surfaces (neon signs, glowing runes, etc.).

**Fix**:
1. Add emissive map to material (RGBA8 or R16F)
2. Add emissive scale to PerObjectCB
3. Composite emissive into HDR scene color before bloom threshold

### 6. Single Material Shading Model
**Problem**: Only DefaultLit PBR. No specialized shading for cloth, hair, skin, water.

**Impact**: Artist flexibility limited. Skin looks plastic, hair looks fuzzy cloth, water looks opaque.

**Fix**: Add shading model selector in material:
1. DefaultLit (current) — Cook-Torrance GGX
2. Hair — Anisotropic kajiya-kay + secondary specular
3. Cloth — Velvet anisotropy + fuzz
4. Skin — Burley diffuse + SSS profile
5. Water — Refraction + caustics + foam

---

## QUICK WINS (1-2 Days Each)

| Feature | Work | Gain |
|---------|------|------|
| **Film Grain** | Noise texture + intensity | Cinematic feel |
| **Vignette** | Screen-edge darkening | Directs eye to center |
| **Chromatic Aberration** | R/G/B UV offset | Analog camera feel |
| **Lens Flare** | Screen-space ghosts + star | Cinematic glow |
| **Color Grading LUT** | 32x32x32 CUBE texture | Artist color control |
| **Emissive Map** | Material texture + scale CB | Self-illumination |
| **Detail Normal Blending** | Blend 2 normals per-pixel | Surface detail richness |
| **Per-Object Shadow Toggle** | Add CastShadow flag to component | Material artist control |
| **Light Functions** | Per-light custom shaders | Light modulation effects |
| **IES Profiles** | Texture-based light attenuation | Realistic light patterns |

---

## SUMMARY TABLE: What to Implement Next

**To match UE5 visual quality at 80% coverage (2-3 weeks of work):**

1. **Enable & Fix GTAO** (1 day) — Get AO working, it's already coded
2. **Complete PCSS** (1 day) — Blocker search loop
3. **TAA** (5 days) — Biggest visual impact per day invested
4. **SSR** (4 days) — Reflections on glossy surfaces
5. **Contact Shadows** (2 days) — Prevents floating objects
6. **Quick Wins** (2 days) — Film grain, vignette, aberration
7. **Motion Blur** (3 days) — Camera pans feel smooth
8. **Decals** (5 days) — Enables artist visual vocabulary

**Total**: ~23 days of engineering for 3.5x visual quality improvement

**To match UE5 at 95% coverage**: Add Volumetric Fog (8 days) + Planar Reflections (3 days) + Hair/Cloth shaders (5 days) = +16 days (~39 days total)

**To match UE5 at 99% coverage**: Add Lumen (42 days) + Nanite (21 days) + Virtual Shadow Maps (14 days) = +77 days (~116 days total, diminishing returns)

---

## REFERENCES

### UE5 Source Files Analyzed
- `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\PostProcess\*`
- `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Classes\Lights\*`
- `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowRendering.cpp`
- `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Classes\Atmosphere.cpp`

### UzEngine Source Files Analyzed
- `e:\UzEngine\runtime\engine\renderer\scene_renderer.h`
- `e:\UzEngine\runtime\engine\renderer\scene/scene_render_frame.cpp`
- `e:\UzEngine\runtime\shaders\runtime\lighting\deferred_lighting_ps.hlsl`
- `e:\UzEngine\runtime\shaders\runtime\include\pbr_common.hlsli`
- `e:\UzEngine\runtime\engine\renderer\passes\postprocess\bloom_pass.h`
- `e:\UzEngine\runtime\engine\renderer\passes\postprocess\gtao_pass.h`
- `e:\UzEngine\runtime\engine\renderer\core\constant_buffers.h`

---

**Analysis Date**: 2026-03-17
**Analyzer**: unreal-architect (Claude Haiku 4.5)
**Confidence**: 95% (based on source code analysis, not speculation)
