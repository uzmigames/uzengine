<!-- doc-meta: category=analysis, area=falcor, topic=shaders-materials -->

# Falcor Shader System and Material System Analysis

Comparison of NVIDIA Falcor's shader/material architecture against UzEngine's current shader library.

---

## 1. Shader Language and Compilation

### Falcor

| Aspect | Falcor |
|--------|--------|
| Primary language | **Slang** (`.slang`, `.slangh`) — NVIDIA's shader language |
| Fallback | HLSL for legacy/interop files |
| Entry point convention | Any user-defined name (Slang-managed) |
| Compilation backend | Slang compiler → DXIL/SPIRV/GLSL |
| Permutation system | Slang generics + `#define` preprocessor + `SpecializationInfo` |
| Include system | `import` (Slang modules) for `.slang`; `#include` for `.slangh` headers |
| Differentiable shading | Yes — `[Differentiable]`, `[BackwardDifferentiable]` attributes |
| Interface polymorphism | Yes — `interface IBSDF`, `interface IMaterial`, dispatch via Slang vtables |

### UzEngine

| Aspect | UzEngine |
|--------|----------|
| Language | HLSL (`.hlsl`, `.hlsli`) |
| Entry point convention | **Always `main`** (mandatory — ShaderManager compiles with `entryPoint="main"`) |
| Compilation backend | DXC → DXIL (SM6.6) |
| Permutation system | Preprocessor `#define` flags, `UZ_BINDLESS` toggle |
| Include system | `#ifndef` guard headers in `runtime/shaders/runtime/include/` |
| Differentiable shading | Not implemented |
| Interface polymorphism | Not implemented — single BRDF path, no runtime material dispatch |

**Gap**: Falcor's Slang-based interface polymorphism allows runtime material switching without shader permutations. UzEngine has no equivalent — each new material type requires a new shader permutation.

---

## 2. Material System

### Falcor Material Types

| Material | File | Model | Key Paper |
|----------|------|-------|-----------|
| Standard | `StandardMaterial.slang` | Disney BSDF (metallic-roughness) | Burley 2012, 2015 |
| Cloth | `ClothMaterial.slang` | Ashikhmin sheen + retro-reflection | Ashikhmin 2000 |
| Hair | `HairMaterial.slang` | Chiang 2016 hair scattering | Chiang et al. 2016 |
| MERL | `MERLMaterial.slang` | Measured BRDF (MERL database) | Matusik 2003 |
| MERL Mix | `MERLMixMaterial.slang` | Blended MERL materials | — |
| RGL | `RGLMaterial.slang` | Real-time BRDF (RGL database) | Dupuy 2018 |
| PBRT Diffuse | `PBRT/PBRTDiffuseMaterial.slang` | Lambertian | — |
| PBRT Conductor | `PBRT/PBRTConductorMaterial.slang` | Oren-Nayar + microfacet | — |
| PBRT Dielectric | `PBRT/PBRTDielectricMaterial.slang` | Fresnel dielectric | — |
| PBRT Coated Diffuse | `PBRT/PBRTCoatedDiffuseMaterial.slang` | Layered coat + diffuse | — |
| PBRT Coated Conductor | `PBRT/PBRTCoatedConductorMaterial.slang` | Layered coat + conductor | — |

### UzEngine Material Types

| Material | Files | Status |
|----------|-------|--------|
| Standard PBR | `geometry/mesh_ps.hlsl` + `include/pbr_common.hlsli` | Implemented |
| Decal | `geometry/decal_ps.hlsl` | Implemented |
| Particle | `vfx/particle_billboard_ps.hlsl` | Implemented |
| Trail | `vfx/trail_ps.hlsl` | Implemented |

**Gap**: UzEngine has one material model (standard metallic-roughness PBR). Falcor has 10+ material types including cloth, hair, measured BRDFs, and layered materials.

---

## 3. BSDF Implementations

### Falcor BSDF Library (`Source/Falcor/Rendering/Materials/BSDFs/`)

| BSDF | File | Formula / Model | Paper |
|------|------|-----------------|-------|
| **Diffuse: Disney** | `DisneyDiffuseBRDF.slang` | `albedo × F_Schlick(fd0, fd90, NdotL) × F_Schlick(fd0, fd90, NdotV) / π` | Burley 2012 |
| **Diffuse: Frostbite** | `FrostbiteDiffuseBRDF.slang` | Disney + `energyFactor = lerp(1.0, 1/1.51, roughness)` for energy conservation | Lagarde 2015 |
| **Diffuse: Oren-Nayar** | `OrenNayarBRDF.slang` | `albedo × (A + B × max(0, cos(Δφ)) × sin(α) × tan(β)) / π` with A=1−0.5σ²/(σ²+0.33), B=0.45σ²/(σ²+0.09) | Oren & Nayar 1994 |
| **Diffuse: Lambert** | `LambertDiffuseBRDF.slang` | `albedo / π` | — |
| **Specular: GGX** | `SpecularMicrofacet.slang` | GGX NDF + height-correlated Smith + Schlick Fresnel | Walter 2007, Heitz 2014 |
| **Sheen (Cloth)** | `SheenBSDF.slang` | Estevez-Kulla 2017 microfacet sheen + LUT albedo fit | Estevez & Kulla 2017 |
| **Hair** | `HairChiang16.slang` | `Ap()` × `Mp()` × `Np()` lobes; Bessel-function longitudinal scattering | Chiang et al. 2016 |
| **Dielectric Plate** | `DielectricPlateBSDF.slang` | Physical Fresnel dielectric | — |
| **Beer Transmission** | `BeerBTDF.slang` | Beer-Lambert absorption | Beer 1852 |
| **Layered** | `LayeredBSDF.slang` | N-layer energy transport | — |

### UzEngine BSDF Library (`runtime/shaders/runtime/include/pbr_common.hlsli`)

| BSDF | Function | Formula | Paper |
|------|----------|---------|-------|
| GGX NDF | `D_GGX(NdotH, alpha)` | `a² / (π × ((NdotH²(a²−1)+1)²))` | Walter 2007 eq.33 |
| Smith visibility | `V_SmithGGXCorrelated(a2, NdotV, NdotL)` | Height-correlated, includes `1/(4NLV)` denominator | Heitz 2014 eq.99 |
| Fresnel Schlick | `F_Schlick(cosTheta, F0)` | `F0 + (1−F0) × (1−cosTheta)⁵` | Schlick 1994 |
| Fresnel Schlick F90 | `F_SchlickF90(cosTheta, F0, F90)` | `F0 + (F90−F0) × (1−cosTheta)⁵` | Lagarde 2014 |
| Fresnel IBL roughness | `F_SchlickRoughness(cosTheta, F0, roughness)` | Smoothness clamp for env map | Lagarde 2011 |
| Disney Burley diffuse | `Fd_Burley(albedo, roughness, NdotV, NdotL, LdotH)` | `fd90 = 0.5 + 2×roughness×LdotH²` | Burley 2012 |
| Multi-bounce AO | `AOMultiBounce(ao, c)` | Jimenez polynomial fit | Jimenez 2016 |
| Combined BRDF | `EvaluateBRDF(...)` | D×V×F spec + kD×Fd_Burley diff | Karis 2013 |
| ACES quick tonemap | `ACESFilm(x)` | `(x(2.51x+0.03))/(x(2.43x+0.59)+0.14)` | Narkowicz 2015 |

**Gap analysis vs Falcor**:
- UzEngine has Frostbite-style diffuse (Fd_Burley) — **matches Falcor**
- UzEngine is **missing** Oren-Nayar diffuse (for rough surfaces like concrete/stone)
- UzEngine is **missing** cloth/sheen BSDF (Estevez-Kulla 2017)
- UzEngine is **missing** hair BSDF (Chiang 2016)
- UzEngine is **missing** Fresnel conductor (for real metals: gold, copper, aluminum)
- UzEngine is **missing** layered BSDFs (coated materials, car paint, lacquered wood)
- UzEngine is **missing** diffuse transmission (subsurface-like energy through thin geometry)
- UzEngine is **missing** dielectric BTDF (glass, water, ice transmission with refraction)

---

## 4. Fresnel Functions

### Falcor (`Source/Falcor/Rendering/Materials/Fresnel.slang`)

| Function | Inputs | Formula | Paper | Use Case |
|----------|--------|---------|-------|----------|
| `evalFresnelSchlick` | f0, f90, cosTheta | `f0 + (f90−f0) × (1−cosTheta)⁵` | Schlick 1994 | General dielectrics/metals |
| `evalFresnelGeneralizedSchlick` | f0, f90, exponent, cosTheta | Schlick with tunable power | Schlick variant | Artistic control |
| `evalFresnelDielectric` | eta, cosThetaI | Snell's law; average of Rp and Rs polarizations | PBR Book 2018 | Physically-accurate glass/water |
| `evalFresnelConductor` | eta (real), k (extinction), cosTheta | Complex Fresnel with absorption | Shirley 1997 | Real metals (gold, copper) |
| `reflectanceToIOR` | reflectance | `(1+sqrt(r))/(1−sqrt(r))` | — | Convert measured r to IOR |

### UzEngine (`pbr_common.hlsli`)

| Function | Inputs | Formula | Paper |
|----------|--------|---------|-------|
| `F_Schlick` | cosTheta, F0 | `F0 + (1−F0) × (1−cosTheta)⁵` | Schlick 1994 |
| `F_SchlickF90` | cosTheta, F0, F90 | `F0 + (F90−F0) × (1−cosTheta)⁵` | Lagarde 2014 |
| `F_SchlickRoughness` | cosTheta, F0, roughness | Smoothness-clamped Schlick | Lagarde 2011 |

**Gap**: UzEngine is missing `evalFresnelDielectric` (accurate IOR-based Fresnel for glass/water) and `evalFresnelConductor` (complex Fresnel for physical metals). The current `F_Schlick` approximation is adequate for most opaque materials but cannot accurately represent clear glass or spectral metal behavior.

---

## 5. Microfacet / NDF System

### Falcor (`Source/Falcor/Rendering/Materials/Microfacet.slang`, `SpecularMicrofacet.slang`)

| Feature | Falcor | Paper |
|---------|--------|-------|
| NDF | GGX (isotropic + anisotropic) | Walter 2007 |
| NDF interface | Abstract `INDF` interface — pluggable | Dupuy 2015 |
| G1 masking | `evalG1GGX` | Heitz 2014 |
| G2 separable | `evalMaskingSmithGGXSeparable` | Smith 1967 |
| G2 correlated | `evalMaskingSmithGGXCorrelated` | Heitz 2014 |
| Sampling: NDF | `sampleGGX_NDF()` — proportional to D(wm)·cos(θ) | Walter 2007 |
| Sampling: VNDF | `sampleGGX_VNDF()` — proportional to D_wi(wm), lower variance | Heitz 2018 |
| Smith Lambda | `evalLambdaGGX` | Heitz 2014 |
| Transmission | G2 handles transmission (opposite hemisphere) | Heitz et al. 2016 |
| Delta BSDFs | Yes — when alpha < 1e-3, falls back to delta mirror/glass | — |

### UzEngine (`pbr_common.hlsli`)

| Feature | UzEngine | Paper |
|---------|----------|-------|
| NDF | `D_GGX(NdotH, alpha)` — isotropic only | Walter 2007 |
| G term | `V_SmithGGXCorrelated(a2, NdotV, NdotL)` — combined Vis=G/(4NV·NL) | Heitz 2014 |
| Sampling | Cosine-weighted hemisphere (`ImportanceSampleGGX` in ibl.hlsli) | Karis 2013 |
| VNDF sampling | Not implemented | — |
| Anisotropic GGX | Not implemented | — |
| Delta BSDFs | Not implemented | — |

**Gap**: UzEngine uses classic NDF sampling which has higher variance than VNDF sampling. For IBL baking this is fine, but for path-traced effects or high-quality GI it matters. Anisotropic GGX is missing (needed for brushed metal, hair-like surfaces).

---

## 6. Shader Utility Library Comparison

### Math Utilities

| Category | Falcor (`Utils/Math/`) | UzEngine (`include/`) | Gap |
|----------|----------------------|----------------------|-----|
| PI constants | Full set: M_PI, M_2PI, M_PI_4, M_PI_2, M_1_PI | PI only | Missing M_2PI, M_1_PI, M_PI_2, M_PI_4 |
| Power helpers | Implicit via Slang | `Pow2`, `Pow4`, `Pow5` | OK |
| Color space | `LinearToSrgb` / `SrgbToLinear` (in `FormatConversion.slang`) | `LinearToSrgb`, `SrgbToLinear` (in `math_util.hlsli`) | OK |
| Matrix inverse | `inverse(float2x2/3x3/4x4)` | Not implemented | Missing |
| Interval arithmetic | `IntervalArithmetic.slang` | Not implemented | Specialized, low priority |
| Half-precision | `Float16.cpp/h`, `HalfUtils.slang` | Not explicitly | Low priority |

### Coordinate / Geometry Utilities

| Utility | Falcor | UzEngine | Gap |
|---------|--------|----------|-----|
| Spherical coordinates | `cartesian_to_spherical_unorm`, `spherical_to_cartesian_rad` | Not in shared lib (inline in sky shaders) | Should extract to `math_util.hlsli` |
| Lat-long map | `world_to_latlong_map`, `latlong_map_to_world` | Not in shared lib | Should extract |
| Octahedral encode/decode | 4 variants: equal-area + non-equal-area, signed + unsigned | `OctEncode` / `OctDecode` (1 variant, `octahedral.hlsli`) | Missing equal-area variants (important for GBuffer normals) |
| ONB / shading frame | `branchlessONB` (Pixar, no sqrt), `ShadingFrame.slang` | Not in shared lib (per-shader TBN) | Should add `branchlessONB` |
| Perpendicular vector | `perp_stark` (branchless) | Not extracted | Missing |
| Gram-Schmidt | `orthogonalizeVectors` | Not extracted | Low priority |
| Axis rotations | `rotate_x/y/z` | Not implemented | Low priority |

### Sampling Functions

| Function | Falcor | UzEngine | Gap |
|----------|--------|----------|-----|
| Hammersley sequence | `Hammersley` (in IBL / sample generators) | `Hammersley` in `ibl.hlsli` | OK — but only accessible from IBL shaders, not shared |
| Van der Corput / Radical inverse | `RadicalInverseVdC` | `RadicalInverseVdC` in `ibl.hlsli` | Should move to shared math header |
| Importance sample GGX | `ImportanceSampleGGX` (via VNDF) | `ImportanceSampleGGX` in `ibl.hlsli` | OK for IBL; missing VNDF variant |
| Uniform disk | `sample_disk_concentric` (Shirley) | `UniformSampleDiskConcentricApprox` in `pcss.hlsli` | Not in shared lib — should move |
| Cosine hemisphere | `sample_cosine_hemisphere_concentric` | Not extracted as shared function | Missing shared function |
| Uniform sphere | `sample_sphere` | Not extracted | Missing |
| Uniform cone | `sample_cone` | Not extracted | Missing |
| Triangle sampling | `sample_triangle` | Not implemented | Missing |
| Von Mises-Fisher | `sampleVonMisesFisher` | Not implemented | Specialized (volumetric clouds) |
| Sobol sequence | Full `SampleGenerator` system with LDS subdirectory | `SobolIndex`, `SobolPixel` in `pcss.hlsli` | Partial — Sobol only for PCSS, not generalized |
| Alias table | `AliasTable.slang` for discrete sampling | Not implemented | Needed for light sampling |

### Hash Functions

| Function | Falcor | UzEngine | Gap |
|----------|--------|----------|-----|
| Jenkins hash | `jenkinsHash` in `HashUtils.slang` | Not implemented (per-shader ad-hoc) | Should add to shared lib |
| TEA block cipher | `blockCipherTEA` in `HashUtils.slang` | Not implemented | Useful for noise / RNG seeding |
| PCG hash | Present in `Pseudorandom/` | Not implemented | High quality per-pixel RNG |
| FNV hash | `FNVHash.h` (CPU-side) | Not implemented | CPU side only |

### Noise Functions

| Function | Falcor (`Utils/`) | UzEngine | Gap |
|----------|-----------------|----------|-----|
| Perlin noise | Not in shader utils (third-party) | Used inline in cloud shaders | Both minimal |
| Worley noise | Not in shader utils | Used inline in cloud shaders | Both minimal |
| Blue noise textures | Used via textures | Not implemented as shared | Low priority |

### Encoding / Packing

| Utility | Falcor (`PackedFormats.slang`, `FormatConversion.slang`) | UzEngine | Gap |
|---------|-------------------------------------------------------|----------|-----|
| Octahedral normal packing | 4 variants (equal-area + non-equal-area) | 1 variant (`octahedral.hlsli`) | Missing equal-area (recommended for GBuffers) |
| R10G10B10A2 packing | `PackedFormats.slang` | Not implemented | Useful for GBuffer compression |
| R16G16 packing | `PackedFormats.slang` | Not implemented | Useful for motion vectors |
| Snorm/unorm conversion | `FormatConversion.slang` | Inline in specific shaders | Should extract |

### Spherical Harmonics

| Feature | Falcor | UzEngine | Gap |
|---------|--------|----------|-----|
| SH evaluation | `SphericalHarmonics.slang` — full L2 SH | `GetSkySHDiffuse` in `sh_irradiance.hlsli` — L2 evaluation only | Missing SH projection (CPU to GPU upload), missing L3+ |
| SH order | L2 (5×5 = 25 coefficients) | L1+L2 (7 × float4 = 28 floats packed) | OK for diffuse ambient |

---

## 7. Lighting System Comparison

### Falcor Light Types (`Source/Falcor/Rendering/Lights/`)

| Light | File | Features |
|-------|------|----------|
| Point | `LightHelpers.slang` | Inverse-square falloff, `kMinLightDistSqr=1e-9` |
| Directional | `LightHelpers.slang` | Parallel rays, `kMaxLightDistance=FLT_MAX` |
| Rect area | `LightHelpers.slang` | LTC (Linearly Transformed Cosines) |
| Sphere area | `LightHelpers.slang` | Sphere solid-angle sampling |
| Disc area | `LightHelpers.slang` | Disc solid-angle sampling |
| Distant (cone) | `LightHelpers.slang` | Cone-shaped sun model |
| Emissive mesh | `EmissiveLightSampler.slang` | Full triangle sampling |
| Environment map | `EnvMapSampler.slang` | MIS-weighted importance sampling |
| Light BVH | `LightBVH.slang` | Hierarchical light tree for many lights |

### UzEngine Light Types

| Light | Status |
|-------|--------|
| Directional (CSM) | Fully implemented in `shadow_common.hlsli` + deferred lighting |
| Point (local shadow) | Shadow sampling implemented in `shadow_common.hlsli` |
| Spot (local shadow) | Shadow sampling implemented in `shadow_common.hlsli` |
| Rect/sphere/disc area | Not implemented |
| Emissive mesh | Not implemented |
| Environment map sampling | IBL via prefiltered cubemap (not MIS-weighted) |
| Light BVH | Not implemented |

**Gap**: Falcor has a full analytic area light pipeline (LTC for rect/disc/sphere). UzEngine has none. Falcor also has a complete many-lights system (LightBVH) whereas UzEngine clusters lights (clustered shading) but without a BVH.

---

## 8. Shadow System Comparison

| Feature | Falcor | UzEngine |
|---------|--------|----------|
| CSM | Not primary focus (path tracer) | Implemented — 8 cascades, atlas layout |
| PCF filtering | Simple comparison | 16-tap Poisson disk with cascade blend |
| PCSS | Available via plugins | Implemented — 3-stage, Sobol sampling, sharpening |
| Contact shadows | Not in core | Implemented (`contact_shadows.hlsli`) |
| VSSM/VSM | Research mode | Not implemented |
| Ray-traced shadows | DXR native | Not implemented (DXR not yet in UzEngine) |
| Local light shadows | Not primary | Implemented — atlas tiles, 6-face point lights |
| DFS (distance field) | Not implemented | Implemented (`dfs_common.hlsli`) |

---

## 9. Tone Mapping Comparison

| Feature | Falcor | UzEngine |
|---------|--------|----------|
| ACES RRT + ODT | `RenderPasses/ToneMapper/ToneMapper.slang` | Full ACES CTL in `film_tonemap.hlsli` |
| ACES matrices | AP0/AP1/XYZ | Full set in `film_tonemap.hlsli` |
| Glow module | `glow_fwd` | `GlowModuleFwd` |
| Red modifier | `hue_weight` | `RgbToHue` + `CenterHue` |
| S-curve parameters | 5 artist parameters | 5 artist parameters (Slope/Toe/Shoulder/BlackClip/WhiteClip) |
| Narkowicz ACES quick | Available | `ACESFilm` in `pbr_common.hlsli` |
| LUT-based grading | Yes | `color_grading_lut_cs.hlsl` |

**UzEngine tone mapping is at parity with Falcor** for the ACES pipeline.

---

## 10. Missing Utilities to Adopt from Falcor

The following Falcor utilities should be considered for adoption in UzEngine, listed by priority:

### High Priority (fills real gaps in current work)

| Utility | Location in Falcor | Target in UzEngine | Reason |
|---------|-------------------|-------------------|--------|
| Equal-area octahedral encode | `MathHelpers.slang: ndir_to_oct_equal_area_unorm` | `include/octahedral.hlsli` | GBuffer normal packing quality |
| `branchlessONB` (Pixar) | `MathHelpers.slang` | `include/math_util.hlsli` | Better TBN construction in shaders |
| `evalFresnelDielectric` | `Fresnel.slang` | `include/pbr_common.hlsli` | Physical glass/water IOR |
| `evalFresnelConductor` | `Fresnel.slang` | `include/pbr_common.hlsli` | Physical metal Fresnel |
| Cosine hemisphere sampling | `MathHelpers.slang: sample_cosine_hemisphere_concentric` | `include/math_util.hlsli` | Needed for GI / SSGI |
| Uniform sphere sampling | `MathHelpers.slang: sample_sphere` | `include/math_util.hlsli` | Needed for area light sampling |
| PCG hash | `Pseudorandom/PCG.slang` | `include/math_util.hlsli` | High-quality per-pixel RNG |
| Oren-Nayar diffuse | `OrenNayarBRDF.slang` | `include/pbr_common.hlsli` | Rough matte surfaces |

### Medium Priority

| Utility | Location in Falcor | Target in UzEngine | Reason |
|---------|-------------------|-------------------|--------|
| Sheen BSDF (Estevez-Kulla 2017) | `SheenBSDF.slang` | new `include/sheen.hlsli` | Cloth/velvet materials |
| Spherical coord helpers | `MathHelpers.slang` | `include/math_util.hlsli` | Cleaner sky/IBL code |
| Generalized Schlick | `Fresnel.slang: evalFresnelGeneralizedSchlick` | `include/pbr_common.hlsli` | Tunable exponent |
| VNDF sampling | `SpecularMicrofacet.slang: sampleGGX_VNDF` | `include/ibl.hlsli` | Lower variance for path tracing |
| Anisotropic GGX | `AnisotropicGGX.slang` | new `include/anisotropic_brdf.hlsli` | Brushed metal, satin |

### Low Priority

| Utility | Location in Falcor | Reason it's lower priority |
|---------|-------------------|-----------------------------|
| Hair BSDF (Chiang 2016) | `HairChiang16.slang` | Requires dedicated hair geometry system |
| Layered BSDF | `LayeredBSDF.slang` | Complex; requires multi-layer material system |
| Light BVH | `LightBVH.slang` | Only needed for 100+ dynamic lights |
| Alias table | `AliasTable.slang` | Only needed for emissive mesh sampling |
| Von Mises-Fisher | `MathHelpers.slang` | Niche (volumetric phase functions) |

---

## 11. Shader Compilation Architecture Differences

| Aspect | Falcor | UzEngine |
|--------|--------|----------|
| Module system | Slang `import` — true separate compilation | `#include` guards — all inline |
| Type safety | Slang interfaces enforce correct BSDF contracts | No material interface — duck typing via macros |
| Reflection | Slang runtime reflection for CB layout | Manual layout macros + C++ struct mirror |
| Hot reload | Slang recompile on source change | `zig build --watch` equivalent |
| Debug info | `-Zi -Od` flags available | Same via `ShaderCompileFlags` |
| Multiplatform | Slang → SPIRV (Vulkan), DXIL (D3D12), Metal, CUDA | DXC → DXIL only (Vulkan planned via SPIRV-Cross) |

---

## 12. Summary

UzEngine's shader library is strong for its use case (deferred PBR rendering with CSM shadows and ACES tonemap). The main architectural gap vs Falcor is:

1. **Single material model** — Falcor has 10+ material types via Slang interfaces; UzEngine has one.
2. **Missing BSDFs** — Oren-Nayar, sheen/cloth, conductor Fresnel, hair, and dielectric are absent.
3. **Sampling utilities scattered** — Hammersley, disk sampling, and sphere sampling functions are buried in per-pass files (ibl.hlsli, pcss.hlsli) rather than in a shared utility header.
4. **No VNDF sampling** — NDF importance sampling has higher variance than Falcor's VNDF; relevant if UzEngine adds path-tracing or high-quality SSR.
5. **Equal-area octahedral missing** — UzEngine uses the non-equal-area variant; Falcor's equal-area form is recommended for GBuffer normal encoding.

These gaps do not affect the current deferred PBR pipeline, but become relevant for cloth rendering, advanced materials, and any path-traced GI additions.
