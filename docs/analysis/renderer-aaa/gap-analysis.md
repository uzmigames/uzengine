<!-- doc-meta: gap-analysis, Prioritized gaps between UzEngine and AAA rendering quality, 2026-04-04 -->

# Rendering Gap Analysis — Prioritized Improvements

**Date**: 2026-04-04
**Baseline**: UzEngine at ~80% UE5 rendering parity
**Goal**: Identify and prioritize the remaining ~20% gap

---

## Methodology

Each gap is rated by:
- **Visual Impact** (1-5): How much does fixing this improve perceived quality?
- **Effort** (days): Estimated implementation time
- **ROI** = Visual Impact / Effort (higher = do first)

Priorities:
- **P0** — Critical gaps visible in every frame (ROI > 1.0)
- **P1** — Important gaps visible in specific scenarios (ROI 0.3-1.0)
- **P2** — Polish gaps, advanced features (ROI < 0.3)
- **P3** — Aspirational (RT, Nanite-class features)

---

## P0 — Critical Gaps (Do Next)

These are visible in every rendered frame and have high ROI.

### P0.1: Froxel Volumetric Fog
| Metric | Value |
|--------|-------|
| Visual Impact | 5/5 — transforms scene depth, atmosphere, light shafts |
| Effort | 5-7 days |
| ROI | ~0.8 |
| Reference | UE5 `VolumetricFog.usf`, Hillaire 2015 "Physically Based & Unified Volumetric Rendering" |
| Current State | Height fog (analytical) only. No 3D froxel grid, no volumetric light scattering. |
| What's Needed | 3D froxel volume (160x90x64), light injection, temporal reprojection, composite |
| Why P0 | Every modern AAA title uses froxel fog. Without it, scenes feel flat. UE5 and Godot both have it. |

### P0.2: Anisotropic GGX BRDF
| Metric | Value |
|--------|-------|
| Visual Impact | 3/5 — brushed metal, fabric, hair look wrong without it |
| Effort | 2-3 days |
| ROI | ~1.2 |
| Reference | Burley 2012 "Physically Based Shading at Disney", UE5 `BRDF.ush:292` |
| Current State | **MISSING**. Only isotropic GGX. |
| What's Needed | `D_GGXAniso(ax, ay, NoH, XoH, YoH)`, `V_SmithGGXCorrelated_Aniso`, tangent map support in material |
| Why P0 | glTF KHR_materials_anisotropy is standard. Three.js, Godot, UE5 all support it. |

### P0.3: Aerial Perspective Completion
| Metric | Value |
|--------|-------|
| Visual Impact | 4/5 — distant objects blend with sky incorrectly without it |
| Effort | 2-3 days |
| ROI | ~1.5 |
| Reference | Hillaire 2020 "A Scalable and Production Ready Sky and Atmosphere Rendering Technique" |
| Current State | **PARTIAL** — 3D volume pass exists but not fully integrated into lighting composite. |
| What's Needed | Sample aerial perspective volume in deferred lighting, blend per-pixel based on depth |
| Why P0 | Already partially implemented. High impact for minimal effort. |

### P0.4: Stephen Hill ACES Fit (upgrade Narkowicz)
| Metric | Value |
|--------|-------|
| Visual Impact | 2/5 — subtle color shift improvement in bright highlights |
| Effort | 0.5 days |
| ROI | ~4.0 |
| Reference | Hill 2016 "ACES Fitted Curves", UE5 `PostProcessTonemap.usf` |
| Current State | Narkowicz 2015 single-curve fit. Works but loses hue in saturated highlights. |
| What's Needed | Replace with Hill's RRT+ODT fitted matrices (better hue preservation) |
| Why P0 | Trivial effort, measurable improvement. Falcor uses full ACES — Hill fit is closest real-time match. |

### P0.5: Specular Occlusion
| Metric | Value |
|--------|-------|
| Visual Impact | 3/5 — prevents IBL specular light leaking through occluded surfaces |
| Effort | 1-2 days |
| ROI | ~2.0 |
| Reference | Lagarde 2014 "Moving Frostbite to PBR", Filament specular occlusion model |
| Current State | **MISSING**. AO only affects diffuse, not specular. Specular IBL leaks at corners/crevices. |
| What's Needed | `specularOcclusion = saturate(pow(NdotV + AO, exp2(-16.0 * roughness - 1.0)) - 1.0 + AO)` in IBL evaluation |
| Why P0 | Simple formula, significant visual improvement in indoor/cavity scenes. |

---

## P1 — Important Gaps (Plan for next milestone)

### P1.1: FSR 2.0 Full Integration
| Metric | Value |
|--------|-------|
| Visual Impact | 4/5 — enables 1080p→4K upscaling with quality close to native |
| Effort | 3-5 days |
| ROI | ~0.9 |
| Current State | EASU + RCAS (FSR 1) implemented. FSR 2 upscaler class exists but not fully wired. |
| What's Needed | Wire AMD FidelityFX FSR 2 SDK, motion vectors input, reactive mask, exposure |

### P1.2: Baked Lightmap GI
| Metric | Value |
|--------|-------|
| Visual Impact | 4/5 — static scenes (architectural viz, interiors) need baked GI |
| Effort | 7-10 days |
| ROI | ~0.45 |
| Current State | **MISSING**. Only real-time GI (radiance cache, SSGI). |
| What's Needed | Lightmap UV generation, GPU lightmap baker (compute), atlas packing, lightmap CB in shader |

### P1.3: Material Graph / Shader Permutation System
| Metric | Value |
|--------|-------|
| Visual Impact | 3/5 — enables visual material authoring, reduces uber-shader cost |
| Effort | 10-14 days |
| ROI | ~0.25 |
| Current State | Single uber-shader with feature flags. No per-material code generation. |
| What's Needed | Node graph editor, HLSL code generation from graph, PSO cache per-material |

### P1.4: Irradiance Volume / VoxelGI (baked)
| Metric | Value |
|--------|-------|
| Visual Impact | 3/5 — cheap GI for static environments |
| Effort | 5-7 days |
| ROI | ~0.5 |
| Current State | Real-time radiance cache only. No baked volume probes. |
| What's Needed | Voxelization, SH storage, GPU probe interpolation |

### P1.5: HDRI Skybox Completion
| Metric | Value |
|--------|-------|
| Visual Impact | 2/5 — needed for static environment scenes |
| Effort | 1-2 days |
| ROI | ~1.3 |
| Current State | **STUB** — HDRISkyboxPass exists but not functional. |
| What's Needed | Wire cubemap loading, skybox rendering for non-procedural sky scenarios |

### P1.6: AgX Tone Mapping (alternative operator)
| Metric | Value |
|--------|-------|
| Visual Impact | 2/5 — better hue preservation than ACES for some content |
| Effort | 1-2 days |
| ROI | ~1.3 |
| Reference | Troy Sobotka's AgX, Three.js implementation (r160+) |
| Current State | Only ACES + Filmic + Reinhard available. |
| What's Needed | AgX transfer function + look transforms in tonemap_ps.hlsl |

---

## P2 — Polish Gaps (Nice-to-have)

### P2.1: Iridescence (thin-film interference)
| Metric | Value |
|--------|-------|
| Visual Impact | 2/5 — soap bubbles, oil slicks, car paint |
| Effort | 3-4 days |
| ROI | ~0.6 |
| Reference | Belcour & Barla 2017, Three.js KHR_materials_iridescence |

### P2.2: DLSS Integration
| Metric | Value |
|--------|-------|
| Visual Impact | 4/5 — best temporal upscaler on NVIDIA hardware |
| Effort | 3-5 days (SDK integration) |
| ROI | ~0.9 |
| Note | Requires NVIDIA SDK license, proprietary DLL |

### P2.3: Omnidirectional PCSS (point/spot soft shadows)
| Metric | Value |
|--------|-------|
| Visual Impact | 2/5 — soft shadows from point/spot lights |
| Effort | 2-3 days |
| ROI | ~0.8 |
| Current State | Point/spot shadows use PCF only. Directional has PCSS. |

### P2.4: Advanced SSS (Burley profile / separable)
| Metric | Value |
|--------|-------|
| Visual Impact | 3/5 — realistic skin rendering |
| Effort | 5-7 days |
| ROI | ~0.5 |
| Current State | Wrap diffuse SSS only. No screen-space diffusion profiles. |
| Reference | Burley 2015 "Extending Disney BRDF to a BSDF with Integrated Subsurface Scattering" |

### P2.5: Volumetric Fog Light Injection Quality
| Metric | Value |
|--------|-------|
| Visual Impact | 2/5 — once froxel fog exists, quality of light injection matters |
| Effort | 2-3 days |
| ROI | ~0.8 |
| What's Needed | Temporal super-sampling of froxel fog, phase function quality, multiple scattering approx |

### P2.6: F82 Extended Fresnel (Adobe Standard Material)
| Metric | Value |
|--------|-------|
| Visual Impact | 1/5 — subtle improvement in mid-angle reflectance for metals |
| Effort | 0.5 days |
| ROI | ~2.0 |
| Reference | UE5 `BRDF.ush:391`, Adobe Standard Material spec |
| What's Needed | Add `F_AdobeF82()` alongside `F_Schlick()` |

---

## P3 — Aspirational (Long-term roadmap)

| Feature | Visual Impact | Effort | Notes |
|---------|--------------|--------|-------|
| RT Shadows (DXR) | 4/5 | 7-14 days | BVH stub exists, needs shader table + RT pipeline |
| RT Reflections (DXR) | 4/5 | 7-14 days | Replaces SSR for off-screen reflections |
| Virtual Shadow Maps | 4/5 | 14-21 days | Clipmap-based, replaces CSM for large worlds |
| Nanite-class Virtual Geometry | 5/5 | 30+ days | Mesh shaders, cluster culling, software rasterizer |
| Lumen-class HW RT GI | 5/5 | 21-30 days | Full path-traced diffuse + specular GI |
| ReSTIR (many-light sampling) | 3/5 | 14-21 days | Enables 10K+ light scenes |
| Mesh Shaders | 2/5 | 7-10 days | Performance improvement for GPU-driven |

---

## Implementation Roadmap

### Sprint 1 (1 week): Quick Wins — P0 items with ROI > 1.0
1. **P0.4** Stephen Hill ACES fit (0.5 day)
2. **P0.5** Specular occlusion (1-2 days)
3. **P0.2** Anisotropic GGX (2-3 days)
4. **P0.3** Aerial perspective completion (2-3 days)
5. **P1.5** HDRI skybox completion (1-2 days)

### Sprint 2 (1-2 weeks): Froxel Fog + Upscaling
1. **P0.1** Froxel volumetric fog (5-7 days)
2. **P1.1** FSR 2.0 integration (3-5 days)
3. **P1.6** AgX tone mapping (1-2 days)

### Sprint 3 (2-3 weeks): Content Pipeline
1. **P1.3** Material graph / shader permutations (10-14 days)
2. **P1.2** Baked lightmap GI (7-10 days)

### Sprint 4 (1-2 weeks): Polish
1. **P2.1** Iridescence (3-4 days)
2. **P2.3** Omni PCSS (2-3 days)
3. **P2.4** Advanced SSS (5-7 days)
4. **P2.6** F82 Fresnel (0.5 day)

---

## Current Strengths (maintain and improve)

These features are already at or above Godot/Three.js quality — continue refining:

1. **Screen-space effects** (SSGI Q1-Q4, SSR, GTAO, contact shadows) — 95% UE5 parity
2. **Atmosphere** (Hillaire 2020 + volumetric clouds) — 85% UE5 parity, far ahead of competition
3. **Shadow system** (CSM + PCSS + contact + DF + static cache) — 85% UE5 parity
4. **IBL pipeline** (split-sum + Fdez-Aguera + env MIS + radiance cache) — 90% UE5 parity
5. **Post-processing** (bloom + ACES + auto-exposure + LUT + motion blur + DOF) — 90% UE5 parity
6. **VFX/Particles** (GPU compute, 8+ force modules, trails, distortion) — 85% UE5 parity
7. **Debug diagnostics** (--diag, --pixel-debug, --pass-dump, 13 view modes) — 95% UE5 parity
8. **GPU-driven** (compute culling, HiZ, indirect draw, bindless SM6.6) — 80% UE5 parity

---

## Known Issues to Fix (from current branch diff)

Based on the current `refactor/render-falcor-parity` branch changes:

1. **IBL baker** — files modified, verify multi-scattering energy conservation still correct
2. **Reflection probe renderer** — modified, verify parallax correction and probe blending
3. **Atmosphere renderer** — modified, verify aerial perspective integration
4. **Scene upscaler** — modified, verify temporal stability
5. **Scene renderer init** — modified, verify all passes correctly registered

Run `--pass-dump` + `--diag` after branch merge to validate no regressions.
