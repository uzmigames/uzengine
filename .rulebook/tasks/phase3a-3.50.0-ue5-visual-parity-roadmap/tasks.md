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
| 6 | Complete PCSS | Blocker search + penumbra | Poisson PCF only | 3/5 | S | 1 | **3.0x** | NEW (this task) |
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
- [ ] 1.1.1 Diagnose GTAO CreateComputePipelineState failure (hr=0x887A0005) — capture with PIX/RenderDoc, check shader bytecode validity.
- [ ] 1.1.2 Fix root cause — likely root signature mismatch or shader compile error. GTAO uses 8 root constant DWORDs at b0.
- [ ] 1.1.3 Verify GTAO renders correctly — capture AO-only debug view, compare with known-good reference.
- [ ] 1.1.4 Enable GTAO by default in SceneRenderer — remove the `if (false)` or `enabled = false` guard.
- [ ] 1.1.5 Screenshot: GTAO ON vs OFF side-by-side verification.

### 1.2 Emissive Materials (NEW — no existing task)
- [ ] 1.2.1 Add `emissiveMap` (Texture2D, t10) + `emissiveColor` (float3) + `emissiveIntensity` (float) to MaterialCB.
- [ ] 1.2.2 In GBuffer pass: write `emissive = emissiveMap.Sample() * emissiveColor * emissiveIntensity` to GBuffer emissive channel (or additive after lighting).
- [ ] 1.2.3 In deferred_lighting_ps.hlsl: add emissive contribution AFTER tonemapping input (pre-bloom) so emissive feeds into bloom.
- [ ] 1.2.4 Add white fallback texture for emissive (no emission by default).
- [ ] 1.2.5 Screenshot: glowing object with bloom halo.

### 1.3 Complete PCSS — blocker search (NEW — no existing task)
- [ ] 1.3.1 Read UE5 `ShadowFilteringCommon.ush` — extract blocker search algorithm.
- [ ] 1.3.2 Implement `BlockerSearch()` in `shadow_common.hlsli` — sample N points in search radius, accumulate blocker depth.
- [ ] 1.3.3 Compute penumbra width from `(receiverDepth - avgBlockerDepth) / avgBlockerDepth * lightSize`.
- [ ] 1.3.4 Scale PCF filter radius by penumbra width — near contact = sharp, far = soft.
- [ ] 1.3.5 Screenshot: soft shadows that get sharper near contact point.

### 1.4 Film Grain → task `phase3a-3.21.0-film-grain` (15 items)
- [ ] 1.4.1 Implement — see existing task checklist.

### 1.5 Vignette → task `phase3a-3.22.0-vignette` (9 items)
- [ ] 1.5.1 Implement — see existing task checklist.

### 1.6 Chromatic Aberration → task `phase3a-3.23.0-chromatic-aberration` (10 items)
- [ ] 1.6.1 Implement — see existing task checklist.

---

## Phase 2: Core Features (Days 6-25) — ROI 1.0x-2.0x

### 2.1 Contact Shadows → task `phase3a-3.29.0-contact-shadows` (15 items)
- [ ] 2.1.1 Implement — see existing task checklist. Screen-space ray march in light direction, 16 steps max.

### 2.2 SSR (Screen-Space Reflections) → task `phase3a-3.16.0-screen-space-reflections` (20 items)
- [ ] 2.2.1 Implement — see existing task checklist. Hi-Z ray march + temporal accumulation.

### 2.3 TAA (Temporal Anti-Aliasing) → task `phase3a-3.14.0-temporal-anti-aliasing` (20 items)
- [ ] 2.3.1 Implement — see existing task checklist. Halton jitter + neighbourhood clamp + velocity rejection.

### 2.4 Color Grading LUT → task `phase3a-3.20.0-color-grading` (20 items)
- [ ] 2.4.1 Implement — see existing task checklist. 32x32x32 3D LUT bake + application.

### 2.5 Auto Exposure (Histogram) → task `phase3a-3.19.0-auto-exposure` (20 items)
- [ ] 2.5.1 Implement — see existing task checklist. Compute histogram + percentile-based exposure.

### 2.6 Spot Lights + Shadows → task `phase3a-3.30.0-point-spot-light-shadows` (existing)
- [ ] 2.6.1 Implement — spot light cone attenuation, perspective shadow maps, deferred accumulation.

### 2.7 Exponential Height Fog → task `phase3a-3.35.0-exponential-height-fog` (existing)
- [x] 2.7.1 Implement — 2-layer exp fog with directional inscattering, matching UE5 HeightFogComponent. Second fog layer additive combination via CalculateLineIntegralShared, CPU precomputation of secondRayOriginTerms, HeightFogContribution AP anti-double-fogging in deferred lighting.

### 2.8 Motion Blur → task `phase3a-3.18.0-motion-blur` (existing)
- [ ] 2.8.1 Implement — velocity buffer (already exists) + per-pixel blur.

### 2.9 DOF → task `phase3a-3.17.0-depth-of-field` (existing)
- [ ] 2.9.1 Implement — CoC from depth, gather-based bokeh, near/far field separation.

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
