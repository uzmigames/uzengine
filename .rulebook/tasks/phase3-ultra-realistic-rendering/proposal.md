# Proposal: phase3-ultra-realistic-rendering

## Why

UzEngine's PBR renderer has reached "AA quality" baseline:
- Cook-Torrance GGX + Burley diffuse + UE5-style visibility term
- IBL (irradiance + prefiltered specular + BRDF LUT + multi-scatter energy compensation)
- Shadow mapping (2048x2048, 16-tap Poisson PCF, distance-based penumbra)
- SSAO (12-sample hemisphere + bilateral blur)
- Bloom (5-mip chain) + ACES tonemap + gamma
- POM displacement + heightmap self-shadow + micro-shadow (Jimenez 2016)
- Procedural sky + sun disk + SkyAtmosphere component

To reach **ultra-realistic** quality matching UE5/Godot 4 reference renders, a systematic
progression of post-process effects, lighting features, and material enhancements is needed.
This proposal is derived from architectural analysis of both UE5 (Lumen, Nanite, VSM,
TemporalAA.usf, ScreenSpaceReflections.usf) and Godot 4 (Forward+, clustered lighting,
VoxelGI, SDFGI, TAA, SSR) rendering pipelines.

Features are organized into 5 sprints ordered by **visual impact / implementation effort**.

## What Changes

### Sprint 1 — Temporal Stability + Fine Detail (highest impact, lowest risk)

**1.1 Temporal Anti-Aliasing (TAA)**
- Halton 2,3 jitter on projection matrix (8-frame cycle)
- Velocity buffer (R16G16_FLOAT) from current vs previous frame transforms
- History buffer (R11G11B10_FLOAT) with exponential blend (alpha = 0.05)
- Neighborhood clamping (3x3 YCoCg min/max) to prevent ghosting
- Velocity-based rejection for disoccluded pixels
- New shaders: `taa.ps.hlsl`
- New render targets: velocity RT, history RT (ping-pong)
- Impact: eliminates specular shimmer on normal maps, sub-pixel detail, smooth edges

**1.2 Screen-Space Contact Shadows**
- Ray-march depth buffer along light direction (16 steps, screen-space)
- Catches fine shadow detail: object edges, grass blades, small geometry
- Temporal dithering (4-frame cycle) + TAA integration for smooth result
- Augments existing shadow map — no replacement needed
- Adds ~15 lines to `pbr.ps.hlsl` or separate `contact_shadow.hlsl` include
- Reference: UE5 ContactShadows.ush

**1.3 Auto Exposure (Eye Adaptation)**
- Luminance histogram compute shader (256 bins, log2 luminance)
- Weighted average of histogram (exclude top 2% and bottom 10%)
- Temporal smoothing: eye_adapt_speed_up = 3.0, eye_adapt_speed_down = 1.0
- Feeds `exposure` multiplier to composite pass (currently hardcoded 1.0)
- New shaders: `luminance_histogram.cs.hlsl`, `auto_exposure.cs.hlsl`
- Reference: UE5 EyeAdaptation, Godot auto_exposure

### Sprint 2 — Reflections + Environment (SSR needs TAA for stability)

**2.1 Screen-Space Reflections (SSR)**
- Hi-Z depth buffer (min mip pyramid for efficient ray march)
- Screen-space ray march: 32 steps, binary refinement (4 iterations)
- Roughness-based blur (cone tracing or mip selection)
- Fallback to IBL cubemap where ray misses screen
- Temporal accumulation via TAA history for stable reflections
- New shaders: `ssr.ps.hlsl`, `hiz_generate.cs.hlsl`
- New render targets: Hi-Z mip chain (D32_FLOAT), SSR result (R11G11B10_FLOAT)
- Reference: UE5 ScreenSpaceReflections.usf, Godot ssr.glsl

**2.2 HDRI Environment Maps**
- Load equirectangular .hdr/.exr files (stb_image for HDR, tinyexr for EXR)
- Convert equirect -> cubemap (compute shader or CPU fallback)
- Replace/augment procedural sky with HDRI as IBL source
- Re-convolve irradiance + prefiltered specular from HDRI cubemap
- Material/scene property: `environment_map = assets/textures/env.hdr`
- Massive quality jump for product/showcase scenes

**2.3 Color Grading (3D LUT)**
- 32x32x32 3D LUT texture (R10G10B10A2 or R8G8B8A8)
- Apply after tonemapping, before gamma
- Load .cube files (industry standard LUT format)
- Default: neutral LUT (identity)
- Per-scene property: `color_grading_lut = assets/luts/filmic_warm.cube`

### Sprint 3 — Multi-Light + Shadow Quality

**3.1 Point + Spot Lights**
- PointLight and SpotLight scene components (position, radius, color, intensity, cone angle)
- Light culling: clustered forward (Godot-style) or simple per-object light list
- Shadow for point lights: dual-paraboloid or cubemap shadow map (per existing spec)
- Shadow for spot lights: perspective shadow map
- LightFrameCB expansion: support up to 8 additional lights (point/spot)
- New shaders: `point_light.hlsli`, `spot_light.hlsli` (include files)
- Reference: Godot clustered forward, UE5 deferred light accumulation

**3.2 Cascaded Shadow Maps (CSM)**
- 4 cascades with PSSM split distribution (lambda = 0.75)
- Shadow atlas: 4096x4096 divided into 4 quadrants (2048x2048 each) or 4 separate textures
- Per-cascade view-projection matrices
- Cascade blending (soft transition between cascade boundaries)
- Replaces current single shadow map for the directional light
- Reference: UE5 ShadowProjectionPixelShader.usf, Godot scene_forward_clustered.glsl

**3.3 Emission Bloom Integration**
- Emission materials (emissionEnergy > 1.0) already output HDR values
- Ensure bloom threshold captures emission correctly (current threshold may clip)
- Optional per-material bloom contribution multiplier
- Glow map support: emission_texture at t6 (emission spatial variation)

### Sprint 4 — Atmospheric + Cinematic Polish

**4.1 Volumetric Fog / God Rays**
- Froxel grid: 160x90x64 voxels (half-res XY, 64 depth slices, exponential distribution)
- Inscattering + extinction computed per froxel (compute shader)
- Temporal reprojection (reuse previous frame's froxel data)
- Height fog density function + uniform fog + noise perturbation
- God rays: directional light inscattering visible in fog
- Per-scene properties: fog_density, fog_color, fog_height, fog_noise_scale
- New shaders: `volumetric_inject.cs.hlsl`, `volumetric_integrate.cs.hlsl`, `volumetric_apply.ps.hlsl`
- Reference: UE5 VolumetricFog.usf, [Hillaire 2016] "Physically Based Sky, Atmosphere and Cloud Rendering"

**4.2 Depth of Field (Bokeh)**
- Circle of Confusion (CoC) from depth buffer + focus distance + aperture
- Separate near/far field blur
- Gaussian or hexagonal bokeh (start with Gaussian, upgrade to bokeh shapes later)
- Focus plane: auto-focus (center screen depth) or manual (scene property)
- Per-scene properties: dof_focus_distance, dof_aperture, dof_focal_length
- New shaders: `dof_coc.ps.hlsl`, `dof_blur.ps.hlsl`, `dof_composite.ps.hlsl`
- Reference: UE5 CircleDOF, Godot dof_blur.glsl

**4.3 Chromatic Aberration + Vignette + Film Grain**
- Lightweight post-process effects added to composite pass
- Chromatic aberration: per-channel UV offset (r/g/b radial shift)
- Vignette: smooth radial darkening from edges
- Film grain: noise overlay (optional, for cinematic look)
- All togglable per-scene properties
- Can be added directly to existing `composite.ps.hlsl` (minimal new code)

### Sprint 5 — Advanced Materials + Lighting

**5.1 Subsurface Scattering (SSS)**
- Pre-integrated SSS (Penner 2011) for thin translucent materials
- Subsurface profile: thickness map + scatter color + scatter radius
- Screen-space blur along surface normal (Jimenez 2015 separable SSS)
- Material flag: `shader = pbr_sss` or `subsurface = true`
- Use cases: skin, marble, wax, candle, leaves, fabric
- New shaders: `sss_blur.ps.hlsl`, modifications to `pbr.ps.hlsl`

**5.2 Anisotropic Materials**
- Anisotropic GGX NDF (Burley 2012, "Physically Based Shading at Disney")
- Tangent-direction control: per-material anisotropy direction + strength
- Use cases: brushed metal, hair, carbon fiber, silk
- Material properties: `anisotropy = 0.8`, `anisotropy_rotation = 0.0`
- Modifies D_GGX and Vis_SmithJoint in `pbr.ps.hlsl`

**5.3 Area Lights (LTC — Linearly Transformed Cosines)**
- Rectangular and disk area lights (Heitz 2016 LTC)
- LTC LUT texture (64x64 float4)
- Soft shadows with natural penumbra shape
- Reference: UE5 AreaLights (RectLight), Godot doesn't have LTC yet

**5.4 Reflection Probes**
- Localized cubemap captures at authored positions
- Per-probe: position, radius, resolution (64-256 per face)
- Box/sphere projection for parallax correction
- Blend between overlapping probes by distance weight
- Supplements SSR fallback (more accurate than global sky cubemap)
- Reference: UE5 ReflectionCapture, Godot ReflectionProbe

**5.5 Decals**
- Deferred decal projection (OBB → screen-space)
- Modifies albedo, normal, roughness of underlying surface
- Use cases: dirt, blood splatter, bullet holes, cracks, graffiti
- New shaders: `decal.ps.hlsl`
- Reference: UE5 DeferredDecal, Godot Decal node

## Root Signature Budget

Current state: 64/64 DWORDs (completely full).
Multiple features here need additional GPU data. Strategy:

1. **Move MaterialCB (b1) from root constants to root CBV** — frees 20 DWORDs, costs 2 (CBV pointer).
   Net gain: 18 DWORDs. This is the first prerequisite.
2. **Bindless resources** (phase3-bindless-resources task) — eliminates per-draw SRV root table entries.
   Long-term solution for unlimited textures/materials.
3. **Feature CBV** — new per-frame CBV for post-process params (TAA, SSR, DoF, etc.).

## Impact

- Affected code: `src/engine/renderer/` (shaders, core, components, proxies, rhi)
- Affected assets: scene files (new components), material files (new properties)
- New dependencies: stb_image_hdr (for HDRI), tinyexr (for EXR), possibly stb_image_write
- Breaking changes: Root signature restructure (Sprint 1 prerequisite)
- Performance budget: Each effect has a performance cost. Target: maintain 60+ FPS at 1080p
  with all Sprint 1-3 features enabled. Sprint 4-5 features are optional/configurable.

## Dependencies

- phase3-bindless-resources — needed for Sprint 3+ (many lights, many textures)
- phase4-render-graph-dx12-barriers — proper barrier management for multi-pass pipeline
- phase4-render-postprocess — shared post-process infrastructure (fullscreen quad, ping-pong RTs)
