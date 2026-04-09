<!-- doc-meta: threejs-falcor-analysis, Three.js and NVIDIA Falcor rendering analysis for UzEngine reference, 2026-04-04 -->

# Three.js & NVIDIA Falcor — Rendering Analysis

**Date**: 2026-04-04
**Three.js Version**: r169+ (WebGL2 / WebGPU)
**Falcor Version**: 7.0+ (DXR path tracer)
**Purpose**: Reference analysis for UzEngine rendering parity

---

## Part 1: Three.js (WebGL/WebGPU Renderer)

### 1. PBR / BRDF — GOOD

**MeshStandardMaterial** (core PBR):
- **Specular D**: GGX/Trowbridge-Reitz
- **Specular G**: Smith-GGX height-correlated (`V_GGX_SmithCorrelated`)
- **Fresnel**: Schlick with `f0` from IOR (default 1.5)
- **Diffuse**: Lambertian `(1/PI)` — NOT Burley

**MeshPhysicalMaterial** (extended):
- **Clearcoat**: Separate GGX lobe with own roughness + normal map
- **Sheen**: Charlie distribution (Estevez & Kulla 2017)
- **Transmission**: Screen-space refraction with `thickness`, `attenuationDistance`, `attenuationColor`
- **Iridescence**: Thin-film interference (Belcour & Barla 2017)
- **Anisotropy**: Anisotropic GGX with KHR_materials_anisotropy
- **Specular**: KHR_materials_specular override of f0
- **Dispersion**: Chromatic dispersion via wavelength-dependent IOR

### 2. IBL Pipeline — GOOD

**PMREMGenerator**: GPU prefiltered environment maps, GGX importance sampling, equirectangular octahedral storage, 256x256 base, ~10 mip levels.

**Irradiance**: SH order 3 (9 coefficients) via `LightProbe`.

**Multi-Scattering** (Fdez-Aguera 2019):
```hlsl
singleScattering = F * envBRDF.x
multiScattering = F * envBRDF.y
totalScattering = singleScattering + multiScattering
diffuse *= 1.0 - totalScattering  // energy conservation
```

### 3. Shadows — BASIC
- PCF (4-tap, default), PCFSoft (Poisson, variable penumbra)
- No CSM in core (community addon `three-csm`)
- No contact shadows, no DF shadows

### 4. Post-Processing — GOOD
- **Bloom**: UnrealBloomPass (progressive down/up, 5 mips)
- **Tone mapping**: ACES (Narkowicz), AgX (Troy Sobotka), Neutral (Khronos PBR), Reinhard, Cineon, Custom
- **SSAO**: SSAOPass + GTAOPass (r162+)
- **Motion blur**: Community only (velocity-buffer based)

### 5. Atmosphere — BASIC
- Linear fog + exponential fog only
- Sky: Community Preetham 1999 shader (not Bruneton)

### 6. Anti-Aliasing — BASIC
- MSAA (browser-controlled), FXAA 3.11, SMAA 3-pass
- No TAA in core

### 7. Performance — GOOD
- InstancedMesh, BatchedMesh (r159+), frustum culling, LOD object
- No GPU culling, no occlusion culling

---

## Part 2: NVIDIA Falcor (Reference Renderer)

### 1. StandardBSDF — REFERENCE (ground truth)

- **Specular D**: GGX, anisotropic when anisotropy > 0
- **Specular G**: Smith height-correlated
- **Fresnel**: Full Fresnel equations (dielectric), Schlick (conductor)
- **Diffuse**: Disney/Burley 2012 with roughness retroreflection
- **Energy conservation**: Kulla & Conty 2017 (precomputed directional albedo LUT)
- Full parameter set: baseColor, roughness, metallic, specularTransmission, diffuseTransmission, IoR, volumeAbsorption, subsurface, specularTint, anisotropy, clearcoat, sheen

### 2. Rendering Modes
- **Path tracing**: Unbiased MC, MIS, Russian roulette, NEE, configurable bounces
- **RTXDI**: ReSTIR DI for many-light direct illumination
- **ReSTIR GI**: Reservoir-based spatiotemporal GI resampling
- **Raster GBuffer**: Hybrid raster + RT lighting for pipeline comparison
- **SVGF / NRD**: Production denoisers for 1-spp path tracing

### 3. Tone Mapping — PRODUCTION
- Full ACES RRT+ODT (not Narkowicz fit)
- Reinhard, Hable, Photographic
- Auto-exposure via histogram, white balance

### 4. Why Falcor is Useful as Reference
- Ground truth path tracing with validated BRDF
- Same glTF scene format — identical camera/lights/materials
- Deterministic output (fixed seed + frame count)
- BSD licensed, NVIDIA-backed, SIGGRAPH-published
- Render graph allows mixing raster GBuffer + path-traced lighting

### 5. Limitations
- No game engine features (LOD, culling, streaming, ECS)
- Windows + NVIDIA RTX GPU only
- Slang shading language (not HLSL) — compare output, not code
- No atmosphere, no shadow maps, no post-processing pipeline

---

## Key Learnings for UzEngine

### From Three.js
1. **Fdez-Aguera multi-scattering** — Already implemented in UzEngine. Maintain parity.
2. **AgX tone mapping** — Worth adding as alternative to ACES. Better hue preservation in saturated highlights.
3. **Extended material features** — Sheen, clearcoat, transmission, iridescence, anisotropy are glTF-standard.
4. **Lambertian vs Burley** — UzEngine correctly uses Burley. Three.js uses Lambert for performance.

### From Falcor
1. **BRDF validation** — Use converged path-traced output as ground truth for GGX + Fresnel + Smith.
2. **Kulla-Conty vs Fdez-Aguera** — Compare multi-scattering results to quantify approximation error.
3. **Full ACES** — Validate UzEngine's Narkowicz fit against Falcor's full RRT+ODT. Stephen Hill's fit is closer.
4. **Material defaults** — Falcor's IOR 1.5, roughness 0.3 are well-validated references.
