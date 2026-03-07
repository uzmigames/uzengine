# Ultra-Realistic Rendering — Task List

## 0. Prerequisites

- [x] 0.1 Move MaterialCB (b1) from 20 root constants to root CBV (frees 18 DWORDs for new features)
- [x] 0.2 Create shared PostProcessCB root CBV for TAA/SSR/DoF/exposure params
- [x] 0.3 Add velocity buffer render target (R16G16_FLOAT) to renderer — writes per-pixel motion vectors
- [x] 0.4 Add previous-frame viewProj storage for velocity/reprojection
- [x] 0.5 Deferred Shading Pipeline (Forward + Deferred configurable, like UE5/Godot)
  - [x] 0.5.1 `RenderPath` enum (Forward/Deferred) in `render_path.h`
  - [x] 0.5.2 Shared BRDF include (`brdf_common.hlsli`) — D_GGX, Vis_Smith, F_Schlick, Burley diffuse, OctEncode/Decode, PCF shadow
  - [x] 0.5.3 G-Buffer write shader (`gbuffer_write.ps.hlsl`) — 4 MRT: AlbedoAO, OctNormal, Material, Emission HDR
  - [x] 0.5.4 Deferred lighting shader (`deferred_lighting.ps.hlsl`) — fullscreen pass, reads G-Buffer + depth, same PBR as forward
  - [x] 0.5.5 G-Buffer render targets (RGBA8 + RGB10A2 + RGBA8 + R11G11B10F = 16 bytes/pixel)
  - [x] 0.5.6 PSO creation for gbuffer_write (4 MRT, PBR vertex layout) and deferred_lighting (fullscreen)
  - [x] 0.5.7 Render path branching in `render()` — Deferred: sky_draw + gbuffer_write + deferred_lighting; Forward: unchanged
  - [x] 0.5.8 Shader registration in ShaderLibrary (gbuffer_write, deferred_lighting)
  - [ ] 0.5.9 Fix deferred path GPU hang (initial G-Buffer barrier states) — WIP
  - [ ] 0.5.10 WS command `renderer.set_path` for runtime switching + `.uzscene` config

---

## Sprint 1 — Temporal Stability + Fine Detail

### 1. Temporal Anti-Aliasing (TAA)

- [x] 1.1 Halton 2,3 jitter sequence (8-frame cycle) applied to projection matrix
- [x] 1.2 Velocity buffer pass: write (current - previous) screen-space motion per pixel
- [x] 1.3 History buffer (R11G11B10_FLOAT, ping-pong double buffer)
- [x] 1.4 TAA resolve shader (`taa.ps.hlsl`): sample history at reprojected UV
- [x] 1.5 Neighborhood clamping: 3x3 YCoCg AABB clamp to prevent ghosting
- [x] 1.6 Velocity rejection: disoccluded pixels fall back to current frame
- [ ] 1.7 Sharpening pass (optional): subtle CAS or RCAS after TAA to recover detail — deferred
- [x] 1.8 Wire TAA into post-process chain (after HDR scene, before bloom)
- [x] 1.9 Toggle: per-scene `taa_enabled = true` (default on)

### 2. GTAO (Ground Truth Ambient Occlusion — replaces current SSAO)

- [ ] 2.1 Replace hemisphere-sample SSAO with GTAO horizon-search algorithm (2-4 directions per pixel)
- [ ] 2.2 Horizon angle integration (traces screen-space depth along slice directions)
- [ ] 2.3 Spatial bilateral filter pass (adapt existing ssao_blur infrastructure)
- [ ] 2.4 Temporal accumulation: blend with previous frame GTAO using velocity buffer (shared with TAA)
- [ ] 2.5 Multi-bounce approximation: recover indirect light lost by AO (Jimenez 2016)
- [ ] 2.6 Update `ssao.ps.hlsl` or create `gtao.ps.hlsl` (replacing old 12-sample kernel)
- [ ] 2.7 Per-scene tuning: `ao_radius = 0.5`, `ao_intensity = 1.5`, `ao_quality = medium`

### 3. Screen-Space Contact Shadows

- [ ] 3.1 Depth buffer ray-march along light direction in screen space (16 steps)
- [ ] 3.2 Temporal dithering: 4-frame Bayer offset pattern for step jitter
- [ ] 3.3 Fade by distance (contact shadows only meaningful up to ~5m from camera)
- [ ] 3.4 Multiply contact shadow factor with existing shadowFactor in `pbr.ps.hlsl`
- [ ] 3.5 Per-scene toggle: `contact_shadows = true`, `contact_shadow_length = 0.1`

### 4. Auto Exposure (Eye Adaptation)

- [ ] 4.1 Luminance compute shader: downsample HDR scene to log2 luminance (R16_FLOAT)
- [ ] 4.2 Histogram compute shader: 256 bins over log2 luminance range
- [ ] 4.3 Weighted average: exclude bottom 10% and top 2% of histogram
- [ ] 4.4 Temporal smoothing: adapt_speed_up = 3.0 s, adapt_speed_down = 1.0 s
- [ ] 4.5 Feed computed EV100 exposure to composite pass (replace hardcoded 1.0)
- [ ] 4.6 Manual exposure override: `exposure_mode = auto|manual`, `exposure_value = 1.0`
- [ ] 4.7 New shaders: `luminance_histogram.cs.hlsl`, `auto_exposure.cs.hlsl`

---

## Sprint 2 — Reflections + Environment

### 5. Screen-Space Reflections (SSR)

- [ ] 5.1 Hi-Z depth mip chain generation (compute shader, min reduction)
- [ ] 5.2 SSR ray march: 32 steps with Hi-Z acceleration + 4-step binary refinement
- [ ] 5.3 Roughness-based ray spread (cone tracing via mip selection on Hi-Z)
- [ ] 5.4 Fallback to IBL cubemap where ray exits screen or fails to intersect
- [ ] 5.5 Temporal accumulation: blend SSR result into TAA history for stability
- [ ] 5.6 SSR composite: blend reflected color into scene based on Fresnel + roughness mask
- [ ] 5.7 New shader: `ssr.ps.hlsl`, `hiz_generate.cs.hlsl`
- [ ] 5.8 New render targets: Hi-Z mip chain, SSR result RT (R11G11B10_FLOAT)
- [ ] 5.9 Per-scene toggle: `ssr_enabled = true`, `ssr_max_roughness = 0.5`

### 6. HDRI Environment Maps

- [ ] 6.1 Load equirectangular HDR images (stb_image `.hdr`, tinyexr `.exr`)
- [ ] 6.2 Equirect-to-cubemap conversion (compute shader or CPU)
- [ ] 6.3 Reconvolve irradiance cubemap from HDRI source (reuse existing convolution code)
- [ ] 6.4 Reconvolve prefiltered specular cubemap from HDRI source
- [ ] 6.5 Scene property: `environment_map = assets/env/studio.hdr`
- [ ] 6.6 Blend between procedural sky and HDRI (optional skybox render)
- [ ] 6.7 Rotation control: `environment_rotation = 45.0` (degrees around Y)

### 7. Color Grading (3D LUT)

- [ ] 7.1 Parse .cube LUT files (industry standard, Adobe/DaVinci Resolve)
- [ ] 7.2 Upload as 32x32x32 3D texture (R8G8B8A8_UNORM)
- [ ] 7.3 Apply LUT lookup in composite pass after tonemapping, before gamma
- [ ] 7.4 Default: neutral (identity) LUT when no .cube specified
- [ ] 7.5 Per-scene property: `color_grading_lut = assets/luts/filmic.cube`
- [ ] 7.6 Color temperature + tint adjustments (simple matrix in composite)

---

## Sprint 3 — Multi-Light + Shadow Quality

### 8. Point + Spot Lights

- [ ] 8.1 PointLight scene component: position, color, intensity, radius, attenuation
- [ ] 8.2 SpotLight scene component: + direction, inner/outer cone angles
- [ ] 8.3 Light struct in LightFrameCB: support up to 8 punctual lights
- [ ] 8.4 Attenuation: inverse-square with radius falloff (UE5 GetLocalLightAttenuation)
- [ ] 8.5 Spot cone attenuation: smoothstep(outerCone, innerCone, angle)
- [ ] 8.6 PBR shader loop: iterate punctual lights, call EvaluateDirectLight per light
- [ ] 8.7 Scene loader: parse PointLight/SpotLight node types
- [ ] 8.8 Light culling (basic): per-object light list based on distance/radius (defer clustered to later)

### 9. Cascaded Shadow Maps (CSM)

- [ ] 9.1 4 cascades with PSSM split scheme (lambda = 0.75 blending log/linear)
- [ ] 9.2 Shadow atlas: 4096x4096 texture divided into 4 quadrants (1024x1024 each)
- [ ] 9.3 Per-cascade orthographic view-projection matrices
- [ ] 9.4 Cascade selection in pixel shader based on view-space depth
- [ ] 9.5 Cascade blending: soft transition in overlap region (dithered or lerp)
- [ ] 9.6 PCF sampling per cascade (reuse existing 16-tap Poisson)
- [ ] 9.7 Stabilize cascade frusta (texel snapping to prevent shadow swimming)
- [ ] 9.8 Replace single shadow map path with CSM path for directional light

### 10. Emission Bloom Integration

- [ ] 10.1 Verify bloom threshold captures emission HDR values (emissionEnergy > 1.0)
- [ ] 10.2 Emission texture support: `emission_texture` at t6 (spatial emission variation)
- [ ] 10.3 Per-material bloom contribution: `bloom_multiplier = 1.0` (optional)
- [ ] 10.4 Test: neon sign material with high emission should produce visible bloom halo

---

## Sprint 4 — Atmospheric + Cinematic Polish

### 11. Volumetric Fog / God Rays

- [ ] 11.1 Froxel grid: 160x90x64 voxels (half-res XY, 64 exponential depth slices)
- [ ] 11.2 Inscattering + extinction compute shader per froxel
- [ ] 11.3 Temporal reprojection: blend previous frame froxel data (reduce noise)
- [ ] 11.4 Height fog: exponential density falloff with configurable base height
- [ ] 11.5 Noise perturbation: 3D noise texture for non-uniform fog density
- [ ] 11.6 God rays: directional light inscattering visible through fog
- [ ] 11.7 Integration pass: composite fog over scene in final pass
- [ ] 11.8 Per-scene properties: fog_density, fog_color, fog_height_falloff, fog_max_opacity
- [ ] 11.9 New shaders: `volumetric_inject.cs.hlsl`, `volumetric_integrate.cs.hlsl`, `volumetric_apply.ps.hlsl`

### 12. Depth of Field (Bokeh)

- [ ] 12.1 Circle of Confusion (CoC) computation from depth + focus params
- [ ] 12.2 Near/far field separation (half-res blur targets)
- [ ] 12.3 Gaussian blur (start simple, upgrade to hex/bokeh shape later)
- [ ] 12.4 Composite: blend blurred near/far with sharp in-focus region
- [ ] 12.5 Auto-focus mode: sample center-screen depth as focus distance
- [ ] 12.6 Per-scene properties: dof_enabled, dof_focus_distance, dof_aperture, dof_focal_length
- [ ] 12.7 New shaders: `dof_coc.ps.hlsl`, `dof_blur.ps.hlsl`, `dof_composite.ps.hlsl`

### 13. Cinematic Post-Process (Composite Enhancements)

- [ ] 13.1 Chromatic aberration: per-channel radial UV offset in composite pass
- [ ] 13.2 Vignette: smooth radial darkening (intensity + falloff params)
- [ ] 13.3 Film grain: noise overlay (optional, strength param)
- [ ] 13.4 All togglable per-scene: `chromatic_aberration = 0.005`, `vignette_intensity = 0.3`
- [ ] 13.5 Integrate into existing `composite.ps.hlsl` (minimal new shader code)

---

## Sprint 5 — Advanced Materials + Lighting

### 14. Subsurface Scattering (SSS)

- [ ] 14.1 Pre-integrated SSS lookup (Penner 2011): curvature + NdotL -> scatter color
- [ ] 14.2 Subsurface profile: scatter_color, scatter_radius, thickness map
- [ ] 14.3 Screen-space separable blur (Jimenez 2015) for diffuse lighting
- [ ] 14.4 Material flag: `shader = pbr_sss` or `subsurface_enabled = true`
- [ ] 14.5 Thickness map texture slot for thin-slab translucency
- [ ] 14.6 New shader: `sss_blur.ps.hlsl`, `pbr.ps.hlsl` modifications

### 15. Anisotropic Materials

- [ ] 15.1 Anisotropic GGX NDF (Burley 2012): separate roughness X/Y
- [ ] 15.2 Anisotropic visibility term (Smith joint for anisotropic)
- [ ] 15.3 Material properties: `anisotropy = 0.8`, `anisotropy_rotation = 0.0`
- [ ] 15.4 Tangent direction from mesh tangent + rotation parameter
- [ ] 15.5 Test materials: brushed metal, carbon fiber

### 16. Area Lights (LTC)

- [ ] 16.1 LTC fitting data: 64x64 float4 LUT texture (Heitz 2016)
- [ ] 16.2 Rectangular area light evaluation using LTC
- [ ] 16.3 Disk area light evaluation
- [ ] 16.4 RectLight / DiskLight scene components (position, size, color, intensity)
- [ ] 16.5 Soft shadow approximation from area light extent
- [ ] 16.6 New shader include: `area_light_ltc.hlsli`

### 17. Reflection Probes

- [ ] 17.1 ReflectionProbe scene component: position, radius, resolution
- [ ] 17.2 Runtime cubemap capture at probe position (offline or on-demand)
- [ ] 17.3 Box/sphere parallax correction for localized reflections
- [ ] 17.4 Probe blending: distance-weighted blend of overlapping probes
- [ ] 17.5 SSR fallback chain: SSR -> probe -> global sky cubemap
- [ ] 17.6 Per-probe irradiance + prefiltered specular convolution

### 18. Decals

- [ ] 18.1 Decal component: OBB projection volume, material reference
- [ ] 18.2 Screen-space decal projection (depth-test against scene depth)
- [ ] 18.3 Decal material: albedo, normal, roughness modification of underlying surface
- [ ] 18.4 Decal sorting and blending (priority-based layering)
- [ ] 18.5 New shader: `decal.ps.hlsl`

---

## Deferred (Beyond Sprint 5)

- [ ] D1. Global Illumination (SDFGI/VoxelGI/SSGI) — phase7-global-illumination task
- [ ] D2. Virtual Shadow Maps (clipmap approach) — phase7-virtual-shadow-maps task
- [ ] D3. Temporal Upsampling (DLSS/FSR/XeSS) — phase7-temporal-upsampling-dlss-fsr task
- [ ] D4. Motion Blur (per-object + camera) — phase7-motion-blur task
- [ ] D5. GPU-Driven Rendering (indirect draws) — phase9-gpu-driven-rendering task
- [ ] D6. Nanite-style Virtual Geometry — phase10-nanite-virtual-geometry task
- [ ] D7. Clustered Forward Shading (for 100+ lights) — phase7-clustered-shading-3d task
- [ ] D8. Sky Atmosphere (Bruneton scattering model) — phase7-sky-atmosphere-bruneton task
