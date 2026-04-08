<!-- doc-meta: ue5-implementation-checklist, Quick-reference checklist for implementing UE5 rendering features in UzEngine, 2026-04-04 -->

# UE5 Rendering Features — Implementation Checklist

**Purpose**: Rapid reference for implementing each rendering subsystem from UE5 source.

---

## 1. PBR BRDF System

### Checklist

- [ ] **Import D_GGX** from BRDF.ush:284
  - Single NDF function
  - Roughness → alpha (α² = roughness²)
  - Formula: `a2 / (π(d² + 1))` where `d = (NoH * a2 - NoH) * NoH + 1`

- [ ] **Implement Vis_Smith** from BRDF.ush:337
  - Height-correlated masking-shadowing
  - Two separate visibility components for V and L
  - Formula: `1 / (Vis_SmithV * Vis_SmithL)`

- [ ] **Use F_Schlick** from BRDF.ush:376
  - Schlick Fresnel approximation
  - Clamp SpecularColor.g at 50× minimum
  - Formula: `saturate(50 * F0.g) * Fc + (1 - Fc) * F0`

- [ ] **Implement Diffuse_Burley** from BRDF.ush:183
  - Disney BRDF diffuse model (preferred)
  - Uses VoH (view-half dot product)
  - Formula: `DiffuseColor * (1/π) * FdV * FdL` where `FdV = 1 + (FD90 - 1) * pow5(1 - NoV)`

- [ ] **Specular BRDF composition**
  - `f_specular = D * Vis * F`
  - Vis already includes `1/(4*NoL*NoV)` division

- [ ] **Area light support**
  - Apply SphereMaxNoH from BRDF.ush:82 for light radius
  - Reduces hot specular highlights on small lights

### Reference Files

- `Engine/Shaders/Private/BRDF.ush` — All BRDF functions
- `Engine/Shaders/Private/BasePassCommon.ush` — Integration with base pass

### Expected Result

Identical specular/diffuse results to UE5 on test sphere (PSNR ≥ 35 dB vs UE5 reference).

---

## 2. IBL Pipeline

### Checklist

- [ ] **BRDF Lookup Table (LUT)**
  - Precompute: 256×256 RG16F texture
  - X-axis: NoV (cos view angle, 0-1)
  - Y-axis: Roughness (0-1)
  - Values: `float2(scale, bias)` for `lerp(F0, 1.0, Fc) = F0 + (1 - F0) * Fc`
  - Reference: Karis 2013 split-sum integration

- [ ] **Prefiltered Environment Map**
  - Mip 0: Original cubemap (6×128×128 or 6×256×256)
  - Mip 1-5: Roughness-dependent convolution
  - Mip N roughness = `N / (num_mips - 1)`
  - Each mip: importance-sampled Phong lobe

- [ ] **Irradiance (Diffuse Indirect)**
  - Option A: Spherical Harmonics (9 coefficients, mobile)
  - Option B: Irradiance cubemap (32×32, deferred)
  - Option C: Lumen (dynamic, expensive)
  - Start with Option B (cubemap convolution)

- [ ] **Multi-Scattering Compensation**
  - Single-scatter Fresnel: `F_single = F_Schlick(...)`
  - Multi-scatter compensation: `E_avg = AverageFresnel(SpecularColor)`
  - `compensation = (1 - E_avg)² / (1 - E_avg²)`
  - Apply: `final = F_single + (1 - F_single) * compensation`
  - Reference: Lazarov 2016

- [ ] **Split-Sum Integration**
  - Deferred light pass: `specular = prefilteredEnv.Sample(mip) * (LUT.r * F0 + LUT.g)`
  - Mip level: `mip = roughness * (num_mips - 1)`

- [ ] **Reflection Probe System**
  - Box-projected parallax correction
  - Blend radius (smooth transitions between probes)
  - Capture resolution: 128×128 (scalable to 256×256)

### Reference Files

- `Engine/Shaders/Private/IndirectLightingCache.ush` — Indirect lighting
- `Engine/Shaders/Private/ReflectionCommon.ush` — Reflection probe sampling
- `Engine/Source/Runtime/Renderer/Private/Lumen/Lumen.cpp` — Lumen if using

### Expected Result

Metallic surfaces: realistic reflections (no black).  
Rough surfaces: smooth indirect diffuse from irradiance.

---

## 3. Cascaded Shadow Maps + PCSS

### Checklist

- [ ] **Cascade Setup**
  - 4 cascades default
  - Resolution: 2048×2048 per cascade (D32F)
  - Split scheme: Logarithmic split (UE5 standard)
  - Fade region: 10-20% overlap for blending

- [ ] **Cascade Binding**
  - Constant buffer: Array of 4 light matrices + depth ranges
  - Per-pixel: Determine cascade index from depth
  - Compare to cascade light space

- [ ] **PCSS Algorithm**
  - Step 1: Find blocker depth (shadow map samples in search radius)
  - Step 2: Estimate penumbra: `penumbra = (receiver - blocker) / blocker`
  - Step 3: Filter radius: `radius = penumbra * light_size_degrees / (receiver - light_z)`
  - Step 4: Percentage-closer filtering (16-32 samples in filter radius)

- [ ] **Cascade Transition Blending**
  - Blend region: Last 10-20% of cascade depth range
  - Smooth transition from near cascade to far cascade
  - No visible seams or popping

- [ ] **Contact Shadows** (cheap near-field shadows)
  - Screen-space ray march toward light (4-8 steps)
  - Terminate on depth hit
  - Falloff with distance

### Reference Files

- `Engine/Shaders/Private/ShadowProjection.usf` — Shadow projection
- `Engine/Source/Runtime/Renderer/Private/ShadowSceneRenderer.cpp` — Shadow rendering

### Expected Result

Soft shadows from distant light.  
No light leaking under overhang.  
Smooth cascade blending (no banding).

---

## 4. Tone Mapping (ACES)

### Checklist

- [ ] **Color Space Matrices**
  - Copy hardcoded transformation matrices from ACESCommon.ush
  - AP1↔XYZ conversion
  - D65↔D60 chromatic adaptation
  - sRGB conversion

- [ ] **ACES Reference Rendering Transform (RRT)**
  - Input: Linear HDR color
  - Step 1: Convert to AP1 (working color space)
  - Step 2: Apply RRT tone curve
  - Step 3: Convert to sRGB or display color space

- [ ] **Fitted Tone Curve** (simplified, from Karis 2013)
  ```glsl
  float3 ACESFilm(float3 x)
  {
      float a = 2.51;
      float b = 0.03;
      float c = 2.43;
      float d = 0.59;
      float e = 0.14;
      return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
  }
  ```

- [ ] **Output Transform**
  - Convert AP1 to sRGB via matrix
  - Apply sRGB transfer function (gamma)
  - Clamp to [0, 1]

- [ ] **Auto-Exposure Integration**
  - Tone mapping receives metered exposure
  - Scale HDR color: `color *= pow(2, exposure_compensation)`
  - Then apply tone curve

### Reference Files

- `Engine/Shaders/Private/ACES/ACESCommon.ush` — Matrix definitions
- `Engine/Shaders/Private/ACES/ACESTonescale.ush` — Tone curve
- `Engine/Shaders/Private/PostProcessTonemap.usf` — Integration

### Expected Result

Output matches ACES reference images.  
Smooth tone curve (no banding).  
Correct color preservation (reds stay red).

---

## 5. Bloom

### Checklist

- [ ] **Bright Pass Extraction**
  - Threshold: Default 1.0 HDR (configurable)
  - Knee: Soft threshold (smooth falloff)
  - Output: Bright pixels extracted to render target

- [ ] **Downsampling + Blur** (8 mips)
  - Mip 0: Full res (from extraction)
  - Mip 1: 1/2 res, Gaussian blur
  - Mip 2: 1/4 res, Gaussian blur
  - ... up to 1/256 res
  - Each mip: 5×5 or 7×7 blur kernel

- [ ] **Upsampling + Composite**
  - Tent filter (2×2 weighted) upsample
  - Blend each mip with intensity scaling
  - Higher mips = softer bloom, lower contribution

- [ ] **Final Composite**
  - `output = original + bloom * bloom_intensity`
  - Bloom intensity: 0.5-1.5 typical (configurable)

### Reference Files

- `Engine/Shaders/Private/Bloom/BloomCommon.ush` — Bloom implementation
- `Engine/Shaders/Private/PostProcessBloom.usf` — Integration

### Expected Result

Glowing light sources (halos around bright pixels).  
Soft falloff (not hard edges).  
Configurable intensity.

---

## 6. TAA (Temporal Anti-Aliasing)

### Checklist

- [ ] **Jitter Pattern**
  - Halton sequence (low-discrepancy, 16-frame cycle)
  - Offset render target by ±0.5 pixels per frame
  - Rotate through 16 positions, then repeat

- [ ] **Motion Vector Encoding**
  - GBuffer velocity channel: `velocity = (currPos - prevPos) / screenSize`
  - Per-pixel world position delta
  - Handle both camera + object motion

- [ ] **History Reprojection**
  - Reproject previous frame using motion vectors
  - Bilinear sample at reprojected location
  - Handle boundaries (clamp to valid region)

- [ ] **Color Clipping**
  - Compute 3×3 neighborhood current colors
  - Find bounding box (min/max per channel)
  - Clip history to this box: `history = clamp(history, colorMin, colorMax)`
  - Prevents ghosting artifacts

- [ ] **Temporal Blending**
  - Blend history + current: `output = lerp(history, current, 0.12)`
  - 0.88 history weight (12% new data per frame)
  - Accumulates antialiasing over 8-16 frames

### Reference Files

- `Engine/Shaders/Private/TemporalAA.usf` — TAA shader
- `Engine/Shaders/Private/TemporalCommon.ush` — Temporal utilities

### Expected Result

Smooth anti-aliasing (4-16× effective MSAA).  
Minimal ghosting (proper clipping).  
No jitter visible to user (accumulation smooth).

---

## 7. Sky Atmosphere

### Checklist

- [ ] **Rayleigh Scattering**
  - Proportional to `1 / wavelength⁴`
  - Blue light scattered more (blue sky)
  - Use 3-component wavelength: [650nm, 550nm, 450nm]

- [ ] **Mie Scattering**
  - Aerosol particles (dust, haze)
  - Longer wavelengths scattered (yellow/orange haze)
  - Less wavelength-dependent than Rayleigh

- [ ] **Ozone Absorption**
  - UV absorption (reduce blue in upper atmosphere)
  - Affect indirect lighting (sky color)

- [ ] **Analytic Solution or LUT**
  - Option A: Per-pixel integration (ray march 32 samples)
  - Option B: Precomputed LUT (transmittance, inscattering)
  - Start with Option A (simpler, per-frame flexible)

- [ ] **Directional Light (Sun)**
  - Sun direction (euler angles)
  - Sun intensity (lux)
  - Sun disk size (angular radius)

- [ ] **Ground Albedo**
  - Affects multiple scattering
  - Typical: 0.2-0.3 (gray ground)

- [ ] **Sky to Fog Injection**
  - Sky color fed to volumetric fog
  - Sky contribution to IBL
  - Ambient color from sky direction

### Reference Files

- `Engine/Shaders/Private/Atmosphere/` — Sky shaders
- Paper: Bruneton 2008, Hillaire 2020

### Expected Result

Blue sky during day.  
Orange/red atmosphere at sunset.  
Realistic ambient color.

---

## 8. Volumetric Fog

### Checklist

- [ ] **Froxel Grid Construction**
  - Resolution: 160×90×64 (view-aligned)
  - Per-froxel: world-space bounds
  - Depth: linear or logarithmic distribution

- [ ] **Light Injection**
  - Directional light: ray march to light, accumulate scattering
  - Point lights: evaluate at froxel center, distance falloff
  - Compute pass: per-froxel lighting calculation

- [ ] **Fog Volume Parameters**
  - Fog density (alpha absorption)
  - Light scattering (inscattering color)
  - Directional light phase function (Henyey-Greenstein)

- [ ] **Rendering (Screen-Space)**
  - Per-pixel: ray-march through froxel grid (8-16 steps)
  - Bilinear sample fog color + density from froxels
  - Blend with scene color: `output = lerp(scene, fog_color, fog_alpha)`

- [ ] **Temporal Stability**
  - Reprojection: history accumulation reduces noise
  - Jitter: randomize ray start per frame
  - No flickering (accumulation over 4-8 frames)

### Reference Files

- `Engine/Shaders/Private/VolumetricFog*.usf` — Volumetric fog shaders

### Expected Result

Realistic fog penetration (distant objects fade).  
Soft light scattering (rays visible).  
No temporal flicker.

---

## 9. Material System

### Checklist

- [ ] **Material Graph Compilation**
  - Parse graph: nodes + edges
  - DFS traversal to emit HLSL code
  - Inject into base pass shader template

- [ ] **Permutation Key Generation**
  - Bitmask of enabled features
  - Shading model (default, subsurface, clear coat, etc.)
  - Quality level (full, reduced, mobile)

- [ ] **Base Material Properties**
  - Base color (albedo)
  - Roughness (GGX alpha)
  - Metallic (metal/dielectric selector)
  - Normal map (tangent space)
  - Emissive (light emission)

- [ ] **Optional Properties**
  - Ambient occlusion (AO multiplier)
  - Displacement (parallax occlusion)
  - Custom data (per-material parameters)

- [ ] **Material Instance System**
  - Runtime override of scalar/vector/texture parameters
  - No recompilation (PSO cached)
  - Dynamic instances for one-off changes

- [ ] **Multi-Layer Support**
  - Clear coat layer (dielectric + base)
  - Layer thickness + IOR
  - Per-layer property override

### Reference Files

- `Engine/Source/Runtime/Engine/Classes/Materials/Material.h` — Material class
- `Engine/Source/Runtime/Renderer/Public/MaterialShader.h` — Shader generation

### Expected Result

Complex materials compile correctly.  
Material instances override parameters.  
No shader recompilation overhead.

---

## 10. GPU-Driven Rendering & Culling

### Checklist

- [ ] **Instance Culling Compute**
  - Input: Instance bounds (AABB), camera frustum
  - Output: Visibility bitmask (one bit per instance)
  - Frustum plane tests: 6 dot products per instance

- [ ] **HZB (Hierarchical Z-Buffer)**
  - Construct pyramid: Mip 0 (full res) → Mip N (1×1)
  - Each level: max-depth reduction (2×2 → 1 texel)
  - Query: Conservative occlusion test (may over-cull)

- [ ] **Occlusion Culling Integration**
  - HZB query per instance (in compute)
  - Instance bounds vs depth pyramid
  - Conservative: if definitely occluded, mark invisible

- [ ] **Indirect Draw Buffers**
  - Build GPU command buffer (draw arguments)
  - Per-visible instance: one draw call (or batch)
  - ExecuteIndirect: GPU reads command buffer, draws

- [ ] **LOD Selection**
  - Distance-based LOD bucketing
  - Per-instance LOD level (computed on GPU or CPU)
  - Bind correct mesh LOD

### Reference Files

- `Engine/Source/Runtime/Renderer/Private/InstanceCulling/` — Instance culling
- `Engine/Source/Runtime/Renderer/Private/HZBOcclusionTesting.cpp` — HZB

### Expected Result

100K+ instances culled per-frame.  
No CPU-GPU sync point (full GPU-driven).  
Correct visibility (no popping).

---

## Quick Integration Checklist

### Phase 1: Core PBR (Week 1-2)

- [ ] BRDF functions (D, Vis, F, diffuse)
- [ ] Material system (graph compilation)
- [ ] Constant buffer layout (material CB)
- [ ] Base pass shader integration

**Target**: Basic PBR sphere rendered correctly

### Phase 2: Lighting (Week 2-3)

- [ ] IBL pipeline (BRDF LUT + prefiltered env)
- [ ] Reflection probes
- [ ] CSM + PCF (no PCSS first)
- [ ] Deferred light pass

**Target**: Lit sphere with shadows + reflections

### Phase 3: Post-Processing (Week 3-4)

- [ ] Tone mapping (ACES)
- [ ] Bloom
- [ ] Auto-exposure
- [ ] Temporal AA

**Target**: Full viewport chain with post-FX

### Phase 4: Advanced Effects (Week 5-6)

- [ ] PCSS (upgrade from PCF)
- [ ] Contact shadows
- [ ] GTAO
- [ ] Volumetric fog

**Target**: AAA visual quality match

### Phase 5: Performance (Week 6-7)

- [ ] GPU culling
- [ ] HZB occlusion
- [ ] LOD system
- [ ] Profile + optimize

**Target**: 60 fps @ 1440p, 100K instances

---

## Validation Checklist

- [ ] PSNR ≥ 35 dB vs UE5 reference (all features)
- [ ] No temporal artifacts (TAA ghosting, flicker)
- [ ] Shadows don't leak under overhangs
- [ ] Bloom glow spreads correctly
- [ ] Metallic surfaces reflect environment
- [ ] Fog fades distant objects smoothly
- [ ] Materials compile and load in < 100ms
- [ ] GPU memory < 2GB (1440p)
- [ ] 60 fps @ 1440p on target hardware

