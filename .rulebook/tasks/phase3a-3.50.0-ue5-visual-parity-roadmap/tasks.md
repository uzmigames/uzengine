## Current State: UzEngine vs UE5 Rendering

### What WORKS today (completed/archived)

| Feature | UE5 | UzEngine | Status |
|---------|-----|----------|--------|
| Deferred PBR (GGX + Burley + SmithJoint) | Yes | Yes | **Parity** |
| Cascaded Shadow Maps (4 cascades, PCF) | Yes | Yes | **Parity** |
| IBL (irradiance + prefiltered + BRDF LUT) | Yes | Yes | **Parity** |
| Bloom (6-mip pyramid + Karis average) | Yes | Yes | **Parity** |
| FilmToneMap (ACES 3-segment, 5 artist params) | Yes | Yes | **Parity** |
| Sky Atmosphere (Bruneton, transmittance LUT) | Yes | Yes | ~90% |
| Height Fog (aerial perspective) | Yes | Yes | Basic |
| POM (steep parallax + self-shadow) | Yes | Yes | **Parity** |
| Distance Field Shadows (atlas, cone trace) | Yes | Yes | 90% |
| Point Lights (deferred, no shadows) | Yes | Yes | Basic |
| GTAO (compute, denoise, temporal, multi-bounce) | Yes | Yes | **Code exists, GPU hang** |
| Multiscatter Energy Compensation | Yes | Yes | **Parity** |
| Per-cascade PCF radius | Yes | Yes | **Parity** |
| BRDF debug split-screen mode | N/A | Yes | Dev tool |

### What's MISSING — Full Gap Table (sorted by ROI)

| # | Feature | UE5 | UzEngine | Visual Impact | Complexity | Days | ROI | Existing Task |
|---|---------|-----|----------|---------------|-----------|------|-----|---------------|
| 1 | Fix GTAO GPU hang | Full pipeline | Code exists, disabled | 4/5 | S | 1 | **4.0x** | NEW (this task) |
| 2 | Emissive Materials | HDR emission map + bloom | None | 3/5 | S | 1 | **3.0x** | NEW (this task) |
| 3 | Film Grain | Per-pixel noise overlay | None | 3/5 | S | 1 | **3.0x** | 3.21.0 (15 items) |
| 4 | Vignette | Screen-edge darkening | None | 2/5 | S | 0.5 | **4.0x** | 3.22.0 (9 items) |
| 5 | Chromatic Aberration | Lateral color shift | None | 2/5 | S | 0.5 | **4.0x** | 3.23.0 (10 items) |
| 6 | ~~Complete PCSS~~ DONE | Blocker search + penumbra | UE5-parity PCSS | 3/5 | S | 1 | **3.0x** | DONE — pcss.hlsli rewritten |
| 7 | Contact Shadows | Screen-space ray march | None | 4/5 | S | 2 | **2.0x** | 3.29.0 (15 items) |
| 8 | Exponential Height Fog | 2 layers + directional inscattering | Basic aerial perspective | 3/5 | M | 2 | **1.5x** | 3.35.0 |
| 9 | SSR (Screen-Space Reflections) | Hi-Z ray march + temporal | None | 5/5 | L | 4 | **1.25x** | 3.16.0 (20 items) |
| 10 | TAA (Temporal Anti-Aliasing) | Jitter + history + variance clip | None | 5/5 | L | 5 | **1.0x** | 3.14.0 (20 items) |
| 11 | Color Grading LUT | 32x32x32 3D LUT + exposure | None | 3/5 | M | 2 | **1.5x** | 3.20.0 (20 items) |
| 12 | Auto Exposure (Histogram) | Histogram-based + adaptation | None | 3/5 | M | 3 | **1.0x** | 3.19.0 (20 items) |
| 13 | Spot Lights + Shadows | Full deferred + perspective SM | None | 4/5 | M | 3 | **1.3x** | 3.30.0 |
| 14 | Motion Blur | Per-object + camera velocity | None | 3/5 | M | 3 | **1.0x** | 3.18.0 |
| 15 | DOF (Depth of Field) | Bokeh + Gaussian + Circle | None | 3/5 | M | 3 | **1.0x** | 3.17.0 |
| 16 | GPU Instancing (wire up) | Automatic batching | Buffer exists, not wired | 2/5 | M | 2 | **1.0x** | 3.39.0 |
| 17 | HiZ Occlusion Culling | GPU-driven cull pass | HiZ exists, no cull pass | 2/5 | M | 3 | **0.7x** | NEW (this task) |
| 18 | Decals (DBuffer) | DBuffer + screen-space | None | 4/5 | L | 5 | **0.8x** | NEW (this task) |
| 19 | Reflection Probes | Baked cubemap captures | None | 3/5 | M | 3 | **1.0x** | 3.37.0 |
| 20 | LOD System | Per-mesh LOD + transition | None | 2/5 | M | 3 | **0.7x** | 3.38.0 |
| 21 | Lens Flare | Screen-space + ghosts | None | 2/5 | M | 3 | **0.7x** | 3.24.0 |
| 22 | Area Lights (LTC) | Linearly Transformed Cosines | None | 2/5 | L | 5 | **0.4x** | 3.33.0 |
| 23 | Volumetric Fog (Froxel) | 3D voxel grid + temporal | None | 3/5 | XL | 7 | **0.4x** | 3.10.0 |
| 24 | Volumetric Clouds | Ray-march + noise + lighting | None | 3/5 | XL | 10 | **0.3x** | 3.40.0 |
| 25 | Shading Models (SSS, ClearCoat, Hair) | 8+ models | DefaultLit only | 2/5 | XL | 10 | **0.2x** | 3.34.0 |
| 26 | SSGI (Screen-Space GI) | Lumen / SSGI | None | 3/5 | XL | 10 | **0.3x** | NEW (this task) |
| 27 | Clustered Forward+ | Tiled/clustered light lists | Brute force | 2/5 | L | 5 | **0.4x** | 3.32.0 |
| 28 | PostProcess Volumes | Blend between PP settings | None | 2/5 | M | 3 | **0.7x** | 3.25.0 |

---

## Phase 1: Quick Wins (Days 1-5) — ROI > 2.0x

### 1.1 Fix GTAO GPU hang (NEW — no existing task)
- [x] 1.1.1 Diagnose GTAO CreateComputePipelineState failure — was already fixed in prior session. Pipeline creates successfully (hr=OK).
- [x] 1.1.2 Root cause resolved — GTAO compute pipeline + denoise pipeline both create and execute without errors.
- [x] 1.1.3 GTAO renders correctly — verified via shadow-debug grid (AO tile shows proper contact darkening at sphere base).
- [x] 1.1.4 GTAO enabled by default — `settings.enabled = true` in GTAOSettings, no guard blocking execution.
- [x] 1.1.5 Verified via debug grid screenshot (2026-03-18).

### 1.2 Emissive Materials (NEW — no existing task)
- [x] 1.2.1 Added emissiveMap (t5 bindless), emissiveColor (float3), emissiveIntensity (float) to PerObjectCB and MaterialInterface.
- [x] 1.2.2 GBuffer pass writes emissive = emissiveMap * emissiveColor * emissiveIntensity to GBuffer RT3 (RGBA16Float).
- [x] 1.2.3 Deferred lighting adds emissive contribution after all lighting: `color = ambient + Lo + emissiveContrib`.
- [x] 1.2.4 Default emissive = black (0,0,0) = no emission. HDR values > 1.0 drive bloom.
- [x] 1.2.5 Implemented in material_system_expansion task (3.31.0). Full Material/MaterialInstance hierarchy.

### 1.3 Complete PCSS — blocker search (NEW — no existing task)
- [x] 1.3.1 Read UE5 `ShadowPercentageCloserFiltering.ush` — extracted full PCSS algorithm (blocker search, penumbra estimation, adaptive PCF, post-PCF sharpening).
- [x] 1.3.2 Rewrite `pcss.hlsli` to UE5 parity — UE5 Sobol sampling via `SobolIndex()` direction numbers, `UniformSampleDiskConcentricApprox()` disk mapping, receiver plane depth bias from shadow derivatives.
- [x] 1.3.3 Fix penumbra formula: directional light uses `TanLightSourceAngle * AverageOccluderDistance` (UE5 ShadowPercentageCloserFiltering.ush:274), not spot-light formula that divides by avgBlockerDepth.
- [x] 1.3.4 Fix C++ PCSS parameter scaling: `tan(0.5 * deg2rad(angle))` half-angle (UE5 ShadowRendering.h:1732), `maxSoftKernelSize / resolution` tile-UV conversion (UE5 ShadowRendering.h:1741).
- [x] 1.3.5 Add UE5 features: search radius = `sceneDepth * tanLightSourceAngle` (depth-dependent), early return for all-blockers (return 0), post-PCF sharpening (MaxSharpnessFactor=4, ShadowPercentageCloserFiltering.ush:437-459).
- [x] 1.3.6 Screenshot verified: soft shadows get sharper near contact point, variable penumbra visible.

### 1.4 Film Grain → task `phase3a-3.21.0-film-grain` (15 items)
- [x] 1.4.1 Core implemented — UE5 procedural GrainFromUV in tonemap_ps.hlsl with triangular dithering. Intensity via PerFrameCB.filmGrainIntensity.

### 1.5 Vignette → task `phase3a-3.22.0-vignette` (9 items)
- [x] 1.5.1 Implemented — UE5 ComputeVignetteMask (cosine-fourth-power) in tonemap_ps.hlsl. Default intensity 0.4.

### 1.6 Chromatic Aberration → task `phase3a-3.23.0-chromatic-aberration` (10 items)
- [x] 1.6.1 Implemented — UE5 wavelength-based per-channel UV offsets in tonemap_ps.hlsl. Default intensity 0.0 (off).

---

## Phase 2: Core Features (Days 6-25) — ROI 1.0x-2.0x

### 2.1 Contact Shadows → task `phase3a-3.29.0-contact-shadows` (15 items)
- [x] 2.1.1 Core implemented — CastContactShadowRay from UE5 ScreenSpaceShadowRayCast.ush, IGN dither, depth bias. Integrated in deferred lighting.

### 2.2 SSR (Screen-Space Reflections) → task `phase3a-3.16.0-screen-space-reflections` (20 items)
- [x] 2.2.1 Core implemented — Hi-Z ray march (ssr_trace_cs + ssr_resolve_cs), Fresnel-weighted IBL fallback blend. Missing: temporal filter.

### 2.3 TAA (Temporal Anti-Aliasing) → task `phase3a-3.14.0-temporal-anti-aliasing` (20 items)
- [x] 2.3.1 Core implemented — Halton jitter, YCoCg neighbourhood clamping, temporal blend, history ping-pong. Velocity buffer now wired (2026-03-18).

### 2.4 Color Grading LUT → task `phase3a-3.20.0-color-grading` (20 items)
- [x] 2.4.1 Core implemented — 32^3 3D LUT compute shader with full UE5 CombineLUTsCommon parity (white balance, 3-zone color correction, blue correction, FilmToneMap).

### 2.5 Auto Exposure (Histogram) → task `phase3a-3.19.0-auto-exposure` (20 items)
- [x] 2.5.1 Fully implemented — 64-bin histogram compute + percentile filtering + temporal adaptation + readback. Verified running (2026-03-18).

### 2.6 Spot Lights + Shadows → task `phase3a-3.30.0-point-spot-light-shadows` (existing)
- [x] 2.6.1 Spot light cone attenuation done (SpotAttenuation UE5 parity). Shadow atlas still pending.

### 2.7 Exponential Height Fog → task `phase3a-3.35.0-exponential-height-fog` (existing)
- [x] 2.7.1 Implement — 2-layer exp fog with directional inscattering, matching UE5 HeightFogComponent. Second fog layer additive combination via CalculateLineIntegralShared, CPU precomputation of secondRayOriginTerms, HeightFogContribution AP anti-double-fogging in deferred lighting.

### 2.8 Motion Blur → task `phase3a-3.18.0-motion-blur` (existing)
- [x] 2.8.1 Core implemented — velocity buffer pass wired (2026-03-18), per-pixel gather blur (motion_blur_cs), depth-aware weighting.

### 2.9 DOF → task `phase3a-3.17.0-depth-of-field` (existing)
- [x] 2.9.1 Core implemented — CoC computation (dof_setup_cs) + disk gather bokeh blur (dof_blur_cs). Missing: recombine pass, diaphragm blades.

---

## Phase 3: Scene Management (Days 26-40) — ROI 0.7x-1.0x

### 3.1 GPU Instancing (wire up) → task `phase3a-3.39.0-gpu-instancing` (existing)
- [ ] 3.1.1 Wire existing instance buffer — BatchBuilder, StructuredBuffer draw.

### 3.2 HiZ Occlusion Culling (NEW — no existing task)
- [ ] 3.2.1 Implement GPU cull pass — read HiZ pyramid, test object screen-space AABB, write visible indices.
- [ ] 3.2.2 Indirect draw from culled list — DrawIndexedInstancedIndirect from GPU-written buffer.
- [ ] 3.2.3 Two-phase occlusion — first pass: last frame's HiZ, second pass: updated HiZ for newly visible.

### 3.3 Decals — DBuffer (NEW — no existing task)
- [ ] 3.3.1 DBuffer textures — 3 render targets (albedo, normal, roughness) at GBuffer resolution.
- [ ] 3.3.2 Decal projection — OBB projector, project UVs from world position, sample decal textures.
- [ ] 3.3.3 DBuffer application — modify GBuffer before lighting: `gbuffer.albedo = lerp(gbuffer.albedo, decal.albedo, decal.alpha)`.
- [ ] 3.3.4 Decal sorting — front-to-back by priority, blend stack.
- [ ] 3.3.5 Screenshot: bullet hole decal on wall, blood splatter, weathering.

### 3.4 Reflection Probes → task `phase3a-3.37.0-reflection-probes` (existing)
- [ ] 3.4.1 Implement — baked cubemap captures, parallax correction, blending.

### 3.5 LOD System → task `phase3a-3.38.0-lod-system` (existing)
- [ ] 3.5.1 Implement — per-mesh LOD levels, screen-size switching, dithered transition.

### 3.6 Lens Flare → task `phase3a-3.24.0-lens-flare` (existing)
- [ ] 3.6.1 Implement — screen-space ghosts, starburst, threshold from bloom.

### 3.7 PostProcess Volumes → task `phase3a-3.25.0-postprocess-volumes` (existing)
- [ ] 3.7.1 Implement — AABB volumes, per-setting override + blend weight.

---

## Phase 4: Advanced (Days 41-60) — ROI < 0.7x

### 4.1 Area Lights (LTC) → task `phase3a-3.33.0-area-lights-ltc` (existing)
- [ ] 4.1.1 Implement — LTC matrix LUT, rect/disc/tube light shapes.

### 4.2 Volumetric Fog (Froxel) → task `phase3a-3.10.0-volumetric-effects` (existing)
- [ ] 4.2.1 Implement — 3D froxel grid, inscattering + extinction, temporal reprojection.

### 4.3 Volumetric Clouds → task `phase3a-3.40.0-volumetric-clouds` (existing)
- [ ] 4.3.1 Implement — ray march through cloud layer, 3D noise + weather map, temporal upscale.

### 4.4 Shading Models → task `phase3a-3.34.0-shading-models` (existing)
- [ ] 4.4.1 Implement — SSS (separable screen-space), ClearCoat, Cloth, TwoSidedFoliage.

### 4.5 SSGI / Screen-Space GI (NEW — no existing task)
- [ ] 4.5.1 Research UE5 Lumen screen trace — `LumenScreenProbeGather.usf`.
- [ ] 4.5.2 Implement simplified SSGI — reuse SSR infrastructure, trace multiple directions, accumulate indirect diffuse.
- [ ] 4.5.3 Temporal accumulation for SSGI — similar to TAA, blend history.
- [ ] 4.5.4 Screenshot: dark room with single light, visible indirect bounce on walls.

### 4.6 Clustered Forward+ → task `phase3a-3.32.0-clustered-forward-plus` (existing)
- [ ] 4.6.1 Implement — 3D cluster grid, light assignment, per-cluster light list.

---

## 5. Visual Verification (per phase)

- [ ] 5.1 Phase 1 complete screenshot — GTAO + emissive + film grain + vignette + PCSS active.
- [ ] 5.2 Phase 2 complete screenshot — TAA + SSR + contact shadows + color grading + auto exposure + motion blur.
- [ ] 5.3 Phase 3 complete screenshot — decals + reflection probes + LOD + instancing + lens flare.
- [ ] 5.4 Phase 4 complete screenshot — volumetric fog + clouds + SSS + SSGI + area lights.
- [ ] 5.5 UE5 side-by-side comparison — equivalent scene in both engines, PSNR measurement.

## 6. Documentation

- [ ] 6.1 Update rendering parity analysis in `docs/analysis/`.
- [ ] 6.2 Create visual comparison guide with before/after screenshots per feature.
- [ ] 6.3 Update ROADMAP.md with revised completion estimates.

---

## Priority Summary (implementation order)

| Order | Feature | Days | Cumulative | Visual Gain | Existing Task |
|-------|---------|------|-----------|-------------|---------------|
| 1 | Fix GTAO | 1 | 1 | +8% | **THIS** |
| 2 | Emissive Materials | 1 | 2 | +3% | **THIS** |
| 3 | Film Grain | 1 | 3 | +3% | 3.21.0 |
| 4 | Vignette + CA | 1 | 4 | +2% | 3.22.0 + 3.23.0 |
| 5 | PCSS blocker search | 1 | 5 | +3% | **THIS** |
| 6 | Contact Shadows | 2 | 7 | +5% | 3.29.0 |
| 7 | Height Fog upgrade | 2 | 9 | +3% | 3.35.0 |
| 8 | SSR | 4 | 13 | +8% | 3.16.0 |
| 9 | TAA | 5 | 18 | +8% | 3.14.0 |
| 10 | Color Grading | 2 | 20 | +3% | 3.20.0 |
| 11 | Auto Exposure | 3 | 23 | +3% | 3.19.0 |
| 12 | Spot Lights | 3 | 26 | +4% | 3.30.0 |
| 13 | Motion Blur | 3 | 29 | +3% | 3.18.0 |
| 14 | DOF | 3 | 32 | +3% | 3.17.0 |
| 15 | GPU Instancing | 2 | 34 | +2% | 3.39.0 |
| 16 | HiZ Culling | 3 | 37 | +2% | **THIS** |
| 17 | Decals | 5 | 42 | +4% | **THIS** |
| 18 | Reflection Probes | 3 | 45 | +3% | 3.37.0 |
| 19 | LOD | 3 | 48 | +2% | 3.38.0 |
| 20 | Area Lights | 5 | 53 | +2% | 3.33.0 |
| 21 | Vol. Fog | 7 | 60 | +3% | 3.10.0 |
| 22 | Vol. Clouds | 10 | 70 | +3% | 3.40.0 |
| 23 | Shading Models | 10 | 80 | +2% | 3.34.0 |
| 24 | SSGI | 10 | 90 | +3% | **THIS** |
| | **TOTAL** | **~90** | | **~80%** | |

**Milestone targets:**
- Day 5: ~50% parity (quick wins done)
- Day 18: ~65% parity (SSR + TAA done)
- Day 32: ~75% parity (full post-process stack)
- Day 60: ~80% parity (scene management + advanced)
