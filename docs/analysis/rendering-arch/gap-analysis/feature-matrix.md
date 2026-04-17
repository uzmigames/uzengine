<!-- doc-meta: feature-matrix, Cross-engine rendering feature comparison (UzEngine vs UE5 vs Godot vs Three.js vs Falcor), 2026-04-04 -->

# Rendering Feature Matrix — UzEngine vs Industry

**Date**: 2026-04-04
**Engines Compared**: UzEngine (current), UE5 5.6, Godot 4.3, Three.js r169+, NVIDIA Falcor 7.0
**Purpose**: Identify where UzEngine stands relative to AAA and indie renderers

---

## Legend

| Symbol | Meaning |
|--------|---------|
| **AAA** | Production AAA quality (shipped titles) |
| **PROD** | Production quality (solid, complete) |
| **GOOD** | Good quality (functional, some limitations) |
| **BASIC** | Basic implementation (works but limited) |
| **PARTIAL** | Started but incomplete |
| **STUB** | Code exists but not functional |
| **MISS** | Not present |
| **REF** | Reference/ground truth quality (Falcor) |
| **N/A** | Not applicable to this engine type |

---

## 1. PBR / BRDF

| Feature | UzEngine | UE5 | Godot 4 | Three.js | Falcor |
|---------|----------|-----|---------|----------|--------|
| GGX NDF (Trowbridge-Reitz) | **PROD** | **AAA** | **PROD** | **PROD** | **REF** |
| Anisotropic GGX | **MISS** | **AAA** | **GOOD** | **GOOD** | **REF** |
| Smith Height-Correlated G | **PROD** | **AAA** | **PROD** | **PROD** | **REF** |
| Schlick Fresnel | **PROD** | **AAA** | **PROD** | **PROD** | **REF** |
| F82 Extended Fresnel (Adobe) | **MISS** | **AAA** | **MISS** | **MISS** | **MISS** |
| Burley/Disney Diffuse | **PROD** | **AAA** | **PROD** | **MISS** (Lambert) | **REF** |
| Multi-Scattering Compensation | **PROD** (Fdez-Aguera) | **AAA** (Kulla-Conty) | **GOOD** | **GOOD** (Fdez-Aguera) | **REF** (Kulla-Conty) |
| Clearcoat (dual-lobe) | **PROD** | **AAA** | **GOOD** | **GOOD** | **REF** |
| Sheen/Cloth (Charlie NDF) | **PROD** | **AAA** | **GOOD** | **GOOD** | **REF** |
| Subsurface Scattering | **PROD** (wrap) | **AAA** (Burley profile) | **GOOD** | **MISS** | **REF** |
| Thin Translucent | **PARTIAL** | **AAA** | **MISS** | **GOOD** (transmission) | **REF** |
| Iridescence (thin-film) | **MISS** | **MISS** | **MISS** | **GOOD** (Belcour) | **MISS** |
| 6 Shading Models dispatch | **PROD** | **AAA** (Substrate) | **GOOD** | **GOOD** | N/A |

**UzEngine Assessment**: Strong core PBR. Missing anisotropy is the main gap vs UE5/Godot/Three.js.

---

## 2. IBL Pipeline

| Feature | UzEngine | UE5 | Godot 4 | Three.js | Falcor |
|---------|----------|-----|---------|----------|--------|
| BRDF Integration LUT | **PROD** (512x512) | **AAA** | **PROD** | **PROD** (PMREM) | N/A (MC) |
| Irradiance (SH L2) | **PROD** (7xfloat4) | **AAA** | **PROD** | **PROD** | N/A (MC) |
| Prefiltered Specular Cubemap | **PROD** (1024, 8 mips) | **AAA** | **GOOD** | **GOOD** | N/A (MC) |
| Multi-Scattering IBL | **PROD** (Fdez-Aguera) | **AAA** | **GOOD** | **GOOD** | N/A (MC) |
| Parallax-Corrected Probes | **PROD** (Lagarde 2012) | **AAA** | **GOOD** | **MISS** | N/A |
| Dynamic Reflection Probes | **PROD** (6-face capture) | **AAA** | **MISS** | **MISS** | N/A |
| Env Map MIS (alias table) | **PROD** | **AAA** | **MISS** | **MISS** | **REF** |
| Radiance Cache (SH clipmap) | **PROD** (16^3 probes) | **AAA** (Lumen) | **MISS** | **MISS** | N/A |
| Specular Occlusion | **MISS** | **AAA** | **MISS** | **MISS** | N/A |

**UzEngine Assessment**: Excellent IBL pipeline. Env MIS + radiance cache puts us ahead of Godot/Three.js. Missing specular occlusion.

---

## 3. Shadows

| Feature | UzEngine | UE5 | Godot 4 | Three.js | Falcor |
|---------|----------|-----|---------|----------|--------|
| Cascaded Shadow Maps | **PROD** (exp splits) | **AAA** | **PROD** | **BASIC** (no CSM core) | N/A (RT) |
| PCF Filtering | **PROD** (16-tap Poisson) | **AAA** | **PROD** | **GOOD** | N/A (RT) |
| PCSS (soft shadows) | **PROD** (blocker search) | **AAA** | **GOOD** | **BASIC** | N/A (RT) |
| Contact Shadows | **PROD** (Hi-Z screen-space) | **AAA** | **MISS** | **MISS** | N/A (RT) |
| Distance Field Shadows | **PROD** (SDF cone trace) | **AAA** | **MISS** | **MISS** | N/A |
| Static Shadow Cache | **PROD** | **AAA** | **MISS** | **MISS** | N/A |
| Virtual Shadow Maps | **MISS** | **AAA** | **MISS** | **MISS** | N/A |
| Point/Spot Light Shadows | **PROD** (atlas) | **AAA** | **GOOD** | **GOOD** | N/A (RT) |
| Ray-Traced Shadows | **MISS** | **AAA** | **MISS** | **MISS** | **REF** |

**UzEngine Assessment**: Shadow system is very strong — contact shadows + DF shadows + static cache puts us well ahead of Godot/Three.js.

---

## 4. Post-Processing

| Feature | UzEngine | UE5 | Godot 4 | Three.js | Falcor |
|---------|----------|-----|---------|----------|--------|
| Bloom (progressive mip) | **PROD** (Karis 2013) | **AAA** | **BASIC** | **GOOD** | N/A |
| ACES Tone Mapping | **PROD** (Narkowicz fit) | **AAA** (fitted RRT+ODT) | **PROD** | **PROD** | **REF** (full RRT+ODT) |
| AgX Tone Mapping | **MISS** | **MISS** | **MISS** | **GOOD** | **MISS** |
| Auto-Exposure (histogram) | **PROD** (Reinhard 2002) | **AAA** | **PROD** | **MISS** | **PROD** |
| Color Grading (3D LUT) | **PROD** | **AAA** | **PROD** | **MISS** | **PROD** |
| Chromatic Aberration | **PROD** | **AAA** | **MISS** | **MISS** | N/A |
| Film Grain | **PROD** | **AAA** | **MISS** | **MISS** | N/A |
| Vignette | **PROD** | **AAA** | **MISS** | **MISS** | N/A |
| Motion Blur (velocity-based) | **PROD** (tile hierarchy) | **AAA** | **MISS** | **MISS** | N/A |
| Depth of Field | **PROD** (compute blur) | **AAA** (cinematic bokeh) | **PROD** | **BASIC** | N/A |
| Lens Flare | **PROD** | **AAA** | **MISS** | **MISS** | N/A |
| CAS Sharpening | **PROD** | **AAA** | **MISS** | **MISS** | N/A |

**UzEngine Assessment**: Post-processing is comprehensive. The ACES fit could be upgraded to Stephen Hill's fitted curve for better accuracy. AgX is a nice-to-have alternative.

---

## 5. Atmosphere / Volumetric

| Feature | UzEngine | UE5 | Godot 4 | Three.js | Falcor |
|---------|----------|-----|---------|----------|--------|
| Physical Sky (Hillaire 2020) | **PROD** | **AAA** | **MISS** | **MISS** | N/A |
| Transmittance LUT | **PROD** | **AAA** | **MISS** | **MISS** | N/A |
| Multi-Scatter LUT | **PROD** | **AAA** | **MISS** | **MISS** | N/A |
| Volumetric Clouds | **PROD** (Schneider 2015) | **AAA** | **MISS** | **MISS** | N/A |
| Height Fog (exponential) | **PROD** | **AAA** | **GOOD** | **BASIC** | N/A |
| Aerial Perspective | **PARTIAL** (3D volume) | **AAA** | **MISS** | **MISS** | N/A |
| Volumetric Fog (froxel) | **MISS** | **AAA** | **GOOD** | **MISS** | N/A |
| God Rays | **PROD** | **AAA** | **BASIC** | **MISS** | N/A |
| Sky Cubemap Capture | **PROD** (CS + PS dual) | **AAA** | **GOOD** | **MISS** | N/A |

**UzEngine Assessment**: Atmosphere is a major strength — Hillaire 2020 + volumetric clouds puts us far ahead of Godot/Three.js and near UE5. Froxel volumetric fog is the main gap.

---

## 6. Anti-Aliasing / Upscaling

| Feature | UzEngine | UE5 | Godot 4 | Three.js | Falcor |
|---------|----------|-----|---------|----------|--------|
| TAA (Karis 2014) | **PROD** | **AAA** | **PROD** | **MISS** | N/A |
| TSR (Temporal Super Res) | **PROD** | **AAA** | **MISS** | **MISS** | N/A |
| SMAA (Jimenez 2012) | **PROD** | **MISS** | **GOOD** | **GOOD** | N/A |
| FXAA | **PARTIAL** | **GOOD** | **GOOD** | **GOOD** | N/A |
| FSR 1 (EASU + RCAS) | **PARTIAL** | **GOOD** | **PROD** | **MISS** | N/A |
| FSR 2 | **PARTIAL** | **GOOD** | **PROD** | **MISS** | N/A |
| DLSS | **MISS** (enum only) | **AAA** | **MISS** | **MISS** | N/A |
| XeSS | **MISS** (enum only) | **GOOD** | **MISS** | **MISS** | N/A |
| SVGF Denoiser | **PROD** (Schied 2017) | **MISS** | **MISS** | **MISS** | **REF** (NRD) |

**UzEngine Assessment**: Excellent AA suite. TSR + SMAA + SVGF is rare outside UE5. FSR 2 integration needs completion. DLSS/XeSS are nice-to-have.

---

## 7. Screen-Space Effects

| Feature | UzEngine | UE5 | Godot 4 | Three.js | Falcor |
|---------|----------|-----|---------|----------|--------|
| GTAO (Jimenez 2016) | **PROD** (quality presets) | **AAA** | **PROD** | **GOOD** | N/A |
| SSR (Hi-Z trace + temporal) | **PROD** (3-pass) | **AAA** | **GOOD** | **BASIC** | N/A |
| SSGI (multi-ray + denoise) | **PROD** (5-pass, Q1-Q4) | **AAA** (Lumen screen) | **GOOD** (SSIL) | **MISS** | N/A |
| Screen-Space Shadows | **PROD** (DF cone trace) | **AAA** | **MISS** | **MISS** | N/A |

**UzEngine Assessment**: Full parity with UE5 in screen-space effects. Significantly ahead of Godot/Three.js.

---

## 8. Global Illumination

| Feature | UzEngine | UE5 | Godot 4 | Three.js | Falcor |
|---------|----------|-----|---------|----------|--------|
| Radiance Cache (SH clipmap) | **PROD** (McGuire 2017) | **AAA** (Lumen) | **MISS** | **MISS** | N/A |
| SSGI | **PROD** | **AAA** | **GOOD** (SSIL) | **MISS** | N/A |
| Distance Field GI | **PROD** (mesh SDF bake) | **AAA** | **GOOD** (SDFGI) | **MISS** | N/A |
| Reflection Probes | **PROD** (dynamic capture) | **AAA** | **GOOD** | **MISS** | N/A |
| Lightmap GI (baked) | **MISS** | **AAA** | **GOOD** | **MISS** | N/A |
| Irradiance Volume | **MISS** | **AAA** | **GOOD** (VoxelGI) | **MISS** | N/A |
| Lumen (SW/HW RT GI) | **MISS** | **AAA** | **MISS** | **MISS** | **REF** (path trace) |

**UzEngine Assessment**: Strong real-time GI via radiance cache + SSGI + DF. Missing baked lightmaps (useful for static scenes). Full Lumen-class RT GI is aspirational.

---

## 9. Materials & Texturing

| Feature | UzEngine | UE5 | Godot 4 | Three.js | Falcor |
|---------|----------|-----|---------|----------|--------|
| PBR Material (albedo/metal/rough/AO) | **PROD** | **AAA** | **PROD** | **PROD** | **REF** |
| Normal Mapping | **PROD** | **AAA** | **PROD** | **PROD** | **REF** |
| Emissive | **PROD** (RGBA16F RT) | **AAA** | **PROD** | **PROD** | **REF** |
| POM (Parallax Occlusion) | **PROD** (Tatarchuk 2006) | **AAA** | **MISS** | **MISS** | N/A |
| DBuffer Decals | **PROD** (Valient 2014) | **AAA** | **GOOD** | **MISS** | N/A |
| Bindless Textures (SM6.6) | **PROD** (+ SM5 fallback) | **AAA** | **MISS** | **MISS** | **PROD** |
| Material Graph/Node system | **MISS** | **AAA** | **GOOD** | **GOOD** (NodeMaterial) | **GOOD** (Slang) |
| Shader Permutations | **MISS** (single uber) | **AAA** (per-material) | **GOOD** | **GOOD** | **PROD** |

**UzEngine Assessment**: Strong material features (POM, decals, bindless). Missing material graph and per-material shader permutation system.

---

## 10. VFX / Particles

| Feature | UzEngine | UE5 | Godot 4 | Three.js | Falcor |
|---------|----------|-----|---------|----------|--------|
| GPU Particle Simulation | **PROD** (compute) | **AAA** (Niagara) | **GOOD** | **MISS** | N/A |
| Billboard Particles | **PROD** | **AAA** | **PROD** | **MISS** | N/A |
| Mesh Particles (instanced) | **PROD** | **AAA** | **GOOD** | **MISS** | N/A |
| Trail Rendering | **PROD** | **AAA** | **GOOD** | **MISS** | N/A |
| Distortion FX | **PROD** (accumulate + resolve) | **AAA** | **MISS** | **MISS** | N/A |
| Curl Noise / Attractors / etc. | **PROD** (8 force modules) | **AAA** | **BASIC** | **MISS** | N/A |

**UzEngine Assessment**: Comprehensive VFX system with 8+ simulation modules. On par with Godot, approaching UE5/Niagara feature set.

---

## 11. Performance / GPU-Driven

| Feature | UzEngine | UE5 | Godot 4 | Three.js | Falcor |
|---------|----------|-----|---------|----------|--------|
| Frustum Culling | **PROD** (GPU compute) | **AAA** | **PROD** (CPU) | **PROD** (CPU) | N/A |
| HiZ Occlusion Culling | **PROD** (12 mips) | **AAA** | **MISS** | **MISS** | N/A |
| GPU Instancing | **PROD** (16k instances) | **AAA** | **GOOD** | **GOOD** | N/A |
| LOD (screen-size) | **PROD** (8 levels) | **AAA** | **GOOD** | **BASIC** | N/A |
| Dynamic Resolution | **PROD** | **AAA** | **GOOD** | **MISS** | N/A |
| Indirect Draw | **PROD** | **AAA** (Nanite) | **MISS** | **MISS** | N/A |
| Nanite/Virtual Geometry | **MISS** | **AAA** | **MISS** | **MISS** | N/A |
| Mesh Shaders | **MISS** | **GOOD** (experimental) | **MISS** | **MISS** | N/A |

**UzEngine Assessment**: Strong GPU-driven pipeline. HiZ + GPU culling + indirect draw is ahead of Godot/Three.js.

---

## 12. Raytracing

| Feature | UzEngine | UE5 | Godot 4 | Three.js | Falcor |
|---------|----------|-----|---------|----------|--------|
| BLAS/TLAS (BVH infra) | **STUB** | **AAA** | **MISS** | **MISS** | **REF** |
| RT Shadows | **MISS** | **AAA** | **MISS** | **MISS** | **REF** |
| RT Reflections | **MISS** | **AAA** | **MISS** | **MISS** | **REF** |
| RT GI (path tracing) | **MISS** | **AAA** (Lumen HW) | **MISS** | **MISS** | **REF** |
| RTXDI / ReSTIR | **MISS** | **MISS** | **MISS** | **MISS** | **REF** |

**UzEngine Assessment**: BVH infrastructure exists as stub. RT is aspirational — focus on rasterized quality first.

---

## 13. Debug / Diagnostics

| Feature | UzEngine | UE5 | Godot 4 | Three.js | Falcor |
|---------|----------|-----|---------|----------|--------|
| GBuffer Visualization | **PROD** (13 modes) | **AAA** | **GOOD** | **MISS** | N/A |
| Pixel Debug (GPU printf) | **PROD** | **AAA** | **MISS** | **MISS** | N/A |
| Pass Dump (all targets) | **PROD** | **AAA** | **MISS** | **MISS** | N/A |
| Per-Pass Toggle | **PROD** | **AAA** | **MISS** | **MISS** | N/A |
| GPU Timestamp Profiling | **PROD** | **AAA** | **GOOD** | **MISS** | **PROD** |
| DRED Breadcrumbs | **PROD** | **AAA** | N/A | N/A | N/A |

**UzEngine Assessment**: Diagnostic tooling is excellent — on par with UE5. Major advantage over Godot/Three.js.

---

## Summary Score Card

| Category | UzEngine | UE5 | Godot 4 | Three.js | Falcor |
|----------|----------|-----|---------|----------|--------|
| PBR/BRDF | 85% | 100% | 80% | 75% | REF |
| IBL Pipeline | 90% | 100% | 65% | 60% | REF |
| Shadows | 85% | 100% | 60% | 30% | REF |
| Post-Processing | 90% | 100% | 50% | 45% | 20% |
| Atmosphere | 85% | 100% | 30% | 10% | 5% |
| Anti-Aliasing | 85% | 95% | 70% | 40% | 10% |
| Screen-Space FX | 95% | 100% | 65% | 25% | N/A |
| Global Illumination | 70% | 100% | 55% | 5% | REF |
| Materials | 70% | 100% | 70% | 65% | 80% |
| VFX/Particles | 85% | 100% | 55% | 10% | N/A |
| GPU Performance | 80% | 100% | 40% | 30% | N/A |
| Raytracing | 5% | 90% | 0% | 0% | REF |
| Debug/Diagnostics | 95% | 100% | 40% | 10% | 50% |
| **OVERALL** | **~80%** | **~99%** | **~52%** | **~31%** | REF |

**Verdict**: UzEngine is at **~80% of UE5 quality** — significantly ahead of Godot (~52%) and Three.js (~31%) in rendering capability. The remaining 20% gap is primarily: froxel volumetric fog, anisotropy, material graph, baked lightmaps, and raytracing.
