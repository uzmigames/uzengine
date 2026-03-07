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

---

## Sprint 2 — Reflections + Color Grading

### 5. Screen-Space Reflections (SSR)

- [x] 5.1 Hi-Z depth mip chain generation (pixel shader, min reduction, 8 mips)
- [x] 5.2 SSR ray march: 32 steps with Hi-Z acceleration + 4-step binary refinement
- [x] 5.3 Roughness-based ray rejection (ssr_max_roughness threshold)
- [x] 5.4 Fallback to IBL cubemap where ray exits screen or fails to intersect
- [x] 5.5 SSR composite: additive blend in composite pass via t4 binding
- [x] 5.6 New shaders: `ssr.ps.hlsl`, `hiz_generate.ps.hlsl`
- [x] 5.7 New render targets: Hi-Z mip chain (R32_FLOAT, 8 mips), SSR result RT (RGBA16F)
- [x] 5.8 Per-scene toggle: `ssr_enabled = true`, `ssr_max_roughness = 0.5`

### 7. Color Grading (Composite Integration)

- [x] 7.1 White balance function (temperature/tint approximation) in composite pass
- [x] 7.2 Color temperature + tint adjustments in composite shader
- [x] 7.3 Contrast adjustment (post-tonemap, centered on 0.18 mid-gray)
- [x] 7.4 Saturation control (luminance-preserving desaturate/saturate)
- [x] 7.5 Per-scene properties: `color_temperature`, `color_tint`, `color_saturation`, `color_contrast`
- [x] 7.6 Scene loader parsing for all color grading properties

---

## Sprint 3 — Multi-Light + Shadow Quality

### 8. Point + Spot Lights

- [x] 8.1 PointLight component data: color, intensity, radius (PointLightData struct)
- [x] 8.2 SpotLight component data: color, intensity, radius, inner/outer angle, direction (SpotLightData struct)
- [x] 8.3 Light struct in LightFrameCB: support up to 8 punctual lights (GpuPunctualLight, 12 floats each)
- [x] 8.4 Attenuation: UE5-style inverse-square with radius falloff — `saturate(1-(d/r)^4)^2 / (d²+1)`
- [x] 8.5 Spot cone attenuation: smoothstep(outerCone, innerCone, angle) in both forward and deferred
- [x] 8.6 PBR shader loop: iterate punctualLights[], call EvaluateDirectLight per light
- [x] 8.7 ComponentRegistry: full PointLight/SpotLight component registrations with properties
- [x] 8.8 gatherPunctualLights(): scene traversal to populate GPU punctual light buffer

### 9. Cascaded Shadow Maps (CSM)

- [x] 9.1 4 cascades with PSSM split scheme (lambda = 0.75 blending log/linear)
- [x] 9.2 Shadow atlas: 4096x4096 texture divided into 4 quadrants (1024x1024 each)
- [x] 9.3 Per-cascade orthographic view-projection matrices computed in update()
- [x] 9.4 Shadow depth pass renders 4 cascades into atlas quadrants with separate viewports
- [x] 9.5 PCF sampling per cascade (16-tap Poisson with distance-based penumbra)
- [x] 9.6 Replace single shadow map path with CSM atlas path for directional light

---

## Sprint 4 — Atmospheric + Cinematic Polish

### 11. Volumetric Fog (Height-Based)

- [x] 11.1 Height-based exponential fog + distance fog (Beer-Lambert) in `volumetric_apply.ps.hlsl`
- [x] 11.2 Directional light inscattering (Henyey-Greenstein phase function, g=0.7)
- [x] 11.3 World position reconstruction from depth via invViewProj
- [x] 11.4 Per-scene properties: fog_density, fog_color, fog_height_falloff, fog_base_height, fog_max_opacity
- [x] 11.5 Scene loader parsing for all fog properties
- [x] 11.6 Shader registered in ShaderLibrary

### 12. Depth of Field (CoC + Blur)

- [x] 12.1 Circle of Confusion computation from depth + thin lens model (`dof_coc.ps.hlsl`)
- [x] 12.2 Variable-width separable Gaussian blur weighted by CoC (`dof_blur.ps.hlsl`)
- [x] 12.3 Bilateral filtering to prevent sharp pixels bleeding into blurred regions
- [x] 12.4 Per-scene properties: dof_focus_distance, dof_aperture, dof_focal_length
- [x] 12.5 Scene loader parsing for all DoF properties
- [x] 12.6 Shaders registered in ShaderLibrary

### 13. Cinematic Post-Process (Composite Enhancements)

- [x] 13.1 Chromatic aberration: per-channel radial UV offset in composite pass
- [x] 13.2 Vignette: smooth radial darkening (intensity + falloff configurable)
- [x] 13.3 Film grain: pseudo-random noise overlay (strength param)
- [x] 13.4 Per-scene properties: chromatic_aberration, vignette_intensity, vignette_falloff, film_grain_strength
- [x] 13.5 Scene loader parsing for all cinematic properties
- [x] 13.6 Integrated into existing `composite.ps.hlsl` (16-DWORD extended CompositeParams)

---

## Sprint 5 — Advanced Materials + Lighting

### 14. Subsurface Scattering (SSS) — Shader Infrastructure

- [x] 14.1 Separable SSS blur shader (`sss_blur.ps.hlsl`) based on Jimenez 2015
- [x] 14.2 Burley normalized profile kernel (5 samples per side)
- [x] 14.3 Bilateral depth weighting to prevent bleeding across depth edges
- [x] 14.4 Shader registered in ShaderLibrary

### 15. Anisotropic Materials

- [x] 15.1 Anisotropic GGX NDF (Burley 2012): `D_GGX_Anisotropic(ax, ay, NoH, XoH, YoH)`
- [x] 15.2 Anisotropic visibility term: `Vis_SmithJoint_Anisotropic` (height-correlated Heitz 2014)
- [x] 15.3 Material properties in MaterialCB: `anisotropy` (-1..1) and `anisotropyRotation` (radians)
- [x] 15.4 Tangent direction rotation by anisotropyRotation parameter in PSMain
- [x] 15.5 Branch in EvaluateDirectLight: isotropic path when ax==ay, anisotropic when different
- [x] 15.6 Applied to both directional and punctual light evaluation

### 18. Decals — Shader Infrastructure

- [x] 18.1 Screen-space decal projection shader (`decal.ps.hlsl`)
- [x] 18.2 Depth reconstruction + OBB volume test in decal local space
- [x] 18.3 Outputs albedo/normal/material modifications to G-Buffer MRT
- [x] 18.4 Shader registered in ShaderLibrary

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
- [ ] D9. GTAO (Ground Truth AO — replaces SSAO) — requires horizon-search + temporal accum
- [ ] D10. Screen-Space Contact Shadows — requires depth ray-march + temporal dithering
- [ ] D11. Auto Exposure (Histogram) — requires luminance/histogram compute shaders
- [ ] D12. HDRI Environment Maps — requires .hdr/.exr loader + equirect-to-cubemap
- [ ] D13. 3D LUT Color Grading — requires .cube parser + 3D texture upload
- [ ] D14. Area Lights (LTC) — requires LTC fitting LUT + rect/disk evaluation
- [ ] D15. Reflection Probes — requires runtime cubemap capture + parallax correction
- [ ] D16. Full Froxel Volumetric Fog — requires 3D voxel grid compute + temporal reprojection
- [ ] D17. Bokeh DoF (Hex/Shape) — upgrade from Gaussian to shaped bokeh
- [ ] D18. TAA Sharpening Pass (CAS/RCAS) — post-TAA detail recovery
