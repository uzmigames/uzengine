<!-- doc-meta
title: SSR Master Report — Industry Analysis & UzEngine Refactor Plan
created: 2026-04-05
status: complete
engines-analyzed: UE5, Frostbite, AMD SSSR, Godot, Three.js, CryEngine, id Tech, Decima, Insomniac, Lumen
papers-analyzed: 15+
-->

# SSR Master Report — Industry Analysis & UzEngine Refactor Plan

## Executive Summary

UzEngine's SSR has **wrong noise** caused by 3 critical bugs and several design gaps compared
to industry standard. This report synthesizes findings from 10 research agents analyzing
UE5, Frostbite, AMD FidelityFX SSSR, Godot, Three.js, CryEngine, id Tech (DOOM), Decima,
Insomniac, Lumen, and 15+ academic papers.

**Root cause of noise (in priority order):**

1. **CRITICAL: No Hi-Z pyramid** — `hizSrvIdx = m_gbuffer.depthSRV` binds raw depth, not the mip chain
2. **HIGH: Static IGN jitter** — no temporal frame offset, temporal accumulation cannot converge
3. **HIGH: No Karis inverse tonemap** — HDR fireflies dominate the spatial filter
4. **HIGH: RGB AABB clamping** — should be YCoCg/LCoCg for better history preservation

---

## 1. UzEngine Current SSR — Diagnosed Issues

### 1.1 Issue Summary Table

| # | Severity | Issue | Root Cause | Fix Complexity |
|---|----------|-------|------------|----------------|
| 1 | **CRITICAL** | `hizSrvIdx = depthSRV` | frame_gi_passes.cpp:227 — Hi-Z pyramid bypassed | 1 line C++ + shader refactor |
| 2 | **HIGH** | IGN without temporal offset | ssr_trace_cs.hlsl:144 — same jitter every frame | 3 lines HLSL |
| 3 | **HIGH** | No Karis inverse tonemap | ssr_resolve_cs.hlsl — HDR fireflies | 6 lines HLSL |
| 4 | **HIGH** | RGB AABB clamping | ssr_temporal_cs.hlsl — over-clamping saturated colors | 15 lines HLSL |
| 5 | **HIGH** | Resolve hardcoded intensity=1.0 | frame_gi_passes.cpp:288 — ignores CVar | 1 line C++ |
| 6 | **MEDIUM** | roughness < 0.1 passthrough seam | ssr_resolve_cs.hlsl:79 — threshold too high | 2 lines HLSL |
| 7 | **MEDIUM** | VELOCITY_THRESHOLD = 0.01 too low | ssr_temporal_cs.hlsl:45 — kills temporal at 10px/frame motion | 1 line HLSL |
| 8 | **MEDIUM** | No GGX importance sampling | ssr_trace_cs.hlsl:139 — mirror only, no stochastic | Major refactor |
| 9 | **MEDIUM** | No virtual hit reprojection | ssr_temporal_cs.hlsl — uses surface MV, not hit MV | Moderate refactor |
| 10 | **LOW** | Triple-weighting confidence | Pre-multiply in trace + squared alpha in lighting | 3 lines HLSL |
| 11 | **LOW** | Binary refinement missing bounds check | ssr_trace_cs.hlsl:211 — no OOB guard | 2 lines HLSL |

### 1.2 The #1 Root Cause: Static IGN Without Temporal Offset

Every engine that achieves noise-free SSR uses **temporally varying jitter**:

| Engine | Jitter Strategy | Temporal Offset |
|--------|----------------|-----------------|
| **UE5** | IGN + frame rotation `float2(47,17)*0.695` | `StateFrameIndexMod8` |
| **AMD SSSR** | STBN blue noise 128x128x64 | Frame index into Z slice |
| **Frostbite** | Blue noise + golden ratio temporal shift | `frame * 0.618034` |
| **Godot 4.5+** | None (no jitter) | None (relies on TAA) |
| **CryEngine** | Jitter table (disabled at shipping) | None |
| **Three.js** | None | None |
| **UzEngine** | IGN (spatial only) | **NONE — THIS IS THE BUG** |

Without temporal variation, the temporal accumulation pass receives the **same biased sample
every frame** and converges to the noisy pattern instead of averaging it away.

**Fix (3 lines in ssr_trace_cs.hlsl):**
```hlsl
// Replace: float jitter = IGN(float2(DTid.xy));
// With:
uint frameIdx = uint(timeParams.z) % 8;
float2 jitteredPos = float2(DTid.xy) + float(frameIdx) * float2(47, 17) * 0.695;
float jitter = frac(52.9829189 * frac(dot(jitteredPos, float2(0.06711056, 0.00583715))));
```

---

## 2. Engine Comparison Matrix

### 2.1 Algorithm Comparison

| Feature | UE5 | AMD SSSR | Frostbite | Godot 4.5 | CryEngine | id Tech 7 | Three.js |
|---------|-----|----------|-----------|-----------|-----------|-----------|----------|
| **Tracing** | Hi-Z mip pyramid | Hi-Z mip pyramid | Hi-Z mip pyramid | Hi-Z parametric T | Linear march | Inline forward | DDA linear |
| **Sampling** | VNDF GGX | VNDF GGX | GGX NDF | Mirror only | Mirror only | Mirror only | Mirror only |
| **Rays/pixel** | 1-12 (quality tier) | 1 per quad (wave ops) | 1 half-res | 1 | 1 | 1 | 1 |
| **Step count** | 8-12 | 64-128 | 32-64 | variable (Hi-Z) | 4-32 (dynamic) | ~32 | screen diagonal |
| **Noise seed** | IGN + frame mod 8 | STBN blue noise | Blue noise + golden | None | Disabled | None | None |
| **Spatial filter** | Ellipse kernel | Bilateral 8px | Bilateral half-res | Gaussian 15-tap | Gaussian 3-level | None | Box 5-tap |
| **Temporal** | Variance clip 1.25sigma | SVGF variance | AABB clamp | None | None | None | None |
| **Karis tonemap** | Yes (spatial) | Yes (prefilter) | Yes | Yes (Godot 4.5+) | No | N/A | No |
| **Hit reprojection** | Clip-to-prev-clip | Virtual hit point | Virtual hit point | Reproject matrix | Prev VP | N/A | None |
| **Roughness handling** | VNDF + lobe-scaled filter | VNDF + bilateral | GGX + half-res resolve | Blur mip pyramid | Blur pyramid 3-level | None | None |
| **IBL blend** | Confidence lerp | Confidence lerp | Confidence lerp | Alpha-over | Alpha-over | Probe fallback | additive |
| **Perf @ 1080p** | ~1.5ms (Quality 3) | ~1.9ms | ~1.2ms (half-res) | ~1.0ms | ~0.8ms | inline | ~2.0ms |

### 2.2 Quality Tier Comparison

| Tier | Best Example | Noise Level | Ghosting | Performance |
|------|-------------|-------------|----------|-------------|
| **S-tier** | UE5 Lumen (screen+SDF+RT) | None visible | None | ~3-5ms |
| **A-tier** | AMD SSSR, UE5 SSR Q3 | Minimal (converges in ~30 frames) | Minimal (variance clip) | ~1.5-2ms |
| **B-tier** | Frostbite, Godot 4.5+ | Low (half-res masks noise) | Low (AABB clamp) | ~1.0-1.5ms |
| **C-tier** | CryEngine, id Tech | Moderate (mirror only = sharp) | Moderate (prev-frame ghost) | ~0.5-1.0ms |
| **D-tier** | Three.js, **UzEngine current** | High (static jitter, no temporal) | High (weak temporal) | ~1.0ms |
| **F-tier** | Three.js legacy (no blur) | Very high | N/A | ~0.5ms |

**UzEngine is currently D-tier. The refactor targets A-tier.**

---

## 3. Industry Best Practices — Consensus Across All Engines

### 3.1 Universal Rules (every good SSR implementation does these)

1. **Temporal jitter is mandatory** — static spatial noise cannot be denoised temporally
2. **Hi-Z pyramid for tracing** — linear march is O(N), Hi-Z is O(log N)
3. **Karis inverse tonemap before spatial accumulation** — prevents HDR fireflies
4. **Variance-based temporal clipping** (not min/max AABB) in **YCoCg/LCoCg space**
5. **Previous frame color** for hit sampling — breaks feedback loop
6. **Screen-edge vignette** — `abs(screenPos) * 5 - 4` (UE5, UzEngine already correct)
7. **Roughness fade** — smooth cutoff at MaxRoughness (UzEngine already correct)
8. **Confidence output** — `(color, confidence)` per pixel for IBL blending

### 3.2 Advanced Practices (A-tier and above)

1. **GGX VNDF importance sampling** — Heitz 2018 or Dupuy/Benyoub 2023 bounded VNDF
2. **STBN blue noise** (Wolfe 2022) — converges 2.7x faster than white noise
3. **Virtual hit reprojection** — store hit distance, reconstruct world hit, reproject through prev VP
4. **Adaptive sample count** — blend weight `1/(N+1)` based on accumulated frame count, not fixed alpha
5. **Tile classification** — skip tiles where roughness > threshold (AMD SSSR saves ~40% dispatch)
6. **Half-res trace + bilateral upsample** — 4x cheaper, acceptable for rough surfaces
7. **Exposure correction** — `PreExposure / PrevPreExposure` on prev-frame color

### 3.3 Architectural Direction (2025+)

No AAA engine uses standalone SSR in 2025. The industry standard is a **cascade architecture**:

```
Layer 1: Hi-Z screen-space trace (fastest, limited to screen content)
Layer 2: Reflection captures / parallax-corrected cubemaps (for SSR misses, interiors)
Layer 3: IBL / prefiltered sky (distant fallback)
Layer 4: [optional] SDF trace / hardware RT (for high-quality off-screen reflections)
```

Each layer outputs `(color, confidence)`. The compositor merges by confidence weight.
UzEngine should design for this cascade from day 1 — even if Layer 4 comes later.

---

## 4. Recommended Refactor Plan

### Phase 1: Fix Critical Bugs (1-2 hours, D-tier -> C-tier)

These are surgical fixes to the existing 3-pass pipeline:

| # | File | Change | Lines |
|---|------|--------|-------|
| 1 | `frame_gi_passes.cpp:227` | Bind actual Hi-Z SRV: `rc.hizSrvIdx = m_hizMipLevels[0].srvIndex` | 1 |
| 2 | `ssr_trace_cs.hlsl:144` | Add temporal IGN: `float2(47,17)*0.695 * (frameIdx%8)` | 3 |
| 3 | `frame_gi_passes.cpp:288` | Use CVar: `rc.intensity = cv_SSRIntensity->value` | 1 |
| 4 | `ssr_trace_cs.hlsl:211` | Add bounds check in binary refinement | 2 |

### Phase 2: Denoise Quality (2-4 hours, C-tier -> B-tier)

| # | File | Change |
|---|------|--------|
| 5 | `ssr_resolve_cs.hlsl` | Add Karis inverse tonemap: `color *= rcp(1 + Luminance(color))` before accumulation, inverse after |
| 6 | `ssr_temporal_cs.hlsl` | Replace min/max AABB with variance clip (mean +/- 1.25 sigma) in YCoCg space |
| 7 | `ssr_temporal_cs.hlsl:45` | Increase `VELOCITY_THRESHOLD` from 0.01 to 0.05 |
| 8 | `ssr_temporal_cs.hlsl:44` | Adaptive blend: `alpha = 1.0 / min(targetSamples, accum + 1)` instead of fixed 0.05 |
| 9 | `ssr_resolve_cs.hlsl:79` | Lower passthrough threshold from 0.1 to 0.04 |

### Phase 3: Stochastic SSR (1-2 days, B-tier -> A-tier)

| # | Change |
|---|--------|
| 10 | Implement GGX VNDF sampling in trace pass (Heitz 2018 or bounded VNDF 2023) |
| 11 | Implement STBN blue noise texture (128x128x64, from NVIDIA open-source) |
| 12 | Virtual hit reprojection: store hit distance, reconstruct world hit, reproject |
| 13 | Tile classification: skip tiles with roughness > MaxRoughness |
| 14 | Roughness-adaptive spatial kernel (ellipse aligned to specular lobe) |
| 15 | Exposure correction on prev-frame color |

### Phase 4: Cascade Architecture (future, A-tier -> S-tier)

| # | Change |
|---|--------|
| 16 | Output `(color, confidence, hitDistance, roughness)` from SSR |
| 17 | Add parallax-corrected reflection capture probes as Layer 2 |
| 18 | Design hybrid SSR+RT dispatch (future DXR integration) |
| 19 | World-space SDF probe cache (AMD Brixelizer GI pattern) |

---

## 5. Key Constants Reference (from published sources)

| Constant | Value | Source |
|----------|-------|--------|
| IGN frame rotation | `float2(47, 17) * 0.695` | UE5 `RandomInterleavedGradientNoise.ush` |
| IGN frame cycle | 8 frames (mod 8) | UE5 `StateFrameIndexMod8` |
| IGN constants | `{0.06711056, 0.00583715, 52.9829189}` | Jimenez 2014 |
| Step offset bias | `-0.5` (centered jitter) | UE5 `SSRTReflections.usf:248` |
| STBN texture size | 128x128x64 | Wolfe 2022 |
| Temporal variance multiplier | 1.25 sigma | UE5 reflections, AMD SSSR uses 1.5 |
| Karis tonemap | `color / (1 + Luminance(color))` | Karis 2013 |
| Karis inverse | `color / (1 - Luminance(color))` | Karis 2013 |
| Screen vignette | `abs(screenPos) * 5 - 4` | UE5, Frostbite (already in UzEngine) |
| Roughness fade | `min(r * (-2/MaxR) + 2, 1)` | UE5 (already in UzEngine) |
| Default MaxRoughness | 0.6 (UE5), 0.7 (Godot), 0.35 (UzEngine CVar) | Various |
| Hi-Z start mip | 1 (UE5), 0 (AMD SSSR) | Various |
| Hi-Z mip increase | `(8/NumSteps) * Roughness` per 2 steps | UE5 |
| Spatial filter — normal weight | `pow(max(dot(Na,Nb), 0), 32-128)` | SVGF: 128, UzEngine: 32 |
| Spatial filter — depth weight | `exp(-|za-zb| * sigma_z)` | SVGF: sigma_z=1.0 |
| SSSR depth_buffer_thickness | 0.015-0.05 | AMD SSSR |
| SSSR max_traversal_intersections | 64-128 | AMD SSSR |
| Velocity threshold (temporal) | 0.01-0.05 UV space | Various |
| Temporal blend (fixed) | 0.05 | UzEngine current, UE5 uses adaptive |
| Temporal max samples | 32 (AMD SSSR), 1024 (UE5) | Various |
| GGX sample bias | 0.1 (discard 10% tail) | UE5 `GGX_IMPORTANT_SAMPLE_BIAS` |
| Bilateral edge-stop (normals) | pow 32-128 | SVGF, UE5 |
| Bilateral edge-stop (depth) | exp factor 100-2048 | Godot uses 2048 |

---

## 6. Paper References (BibTeX)

```bibtex
@article{McGuire2014,
  author = {McGuire, Morgan and Mara, Michael},
  title = {Efficient GPU Screen-Space Ray Tracing},
  journal = {JCGT}, year = {2014}, volume = {3}, number = {4}}

@inproceedings{Stachowiak2015,
  author = {Stachowiak, Tomasz},
  title = {Stochastic Screen-Space Reflections},
  booktitle = {SIGGRAPH Advances}, year = {2015}}

@article{Heitz2018,
  author = {Heitz, Eric},
  title = {Sampling the GGX Distribution of Visible Normals},
  journal = {JCGT}, year = {2018}, volume = {7}, number = {4}}

@inproceedings{Karis2014,
  author = {Karis, Brian},
  title = {High Quality Temporal Supersampling},
  booktitle = {SIGGRAPH Advances}, year = {2014}}

@inproceedings{Schied2017,
  author = {Schied, Christoph and others},
  title = {Spatiotemporal Variance-Guided Filtering},
  booktitle = {HPG}, year = {2017}}

@inproceedings{Wolfe2022,
  author = {Wolfe, Alan and others},
  title = {Spatiotemporal Blue Noise Masks},
  booktitle = {SIGGRAPH}, year = {2022}}

@article{DupuyBenyoub2023,
  author = {Dupuy, Jonathan and Benyoub, Anis},
  title = {Bounded VNDF Sampling for Smith-GGX Reflections},
  journal = {SIGGRAPH Asia}, year = {2023}}

@inproceedings{Uludag2014,
  author = {Uludag, Yasin},
  title = {Hi-Z Screen-Space Cone-Traced Reflections},
  booktitle = {GPU Pro 5}, year = {2014}}
```

---

## 7. File Index — All Analysis Documents

| File | Content |
|------|---------|
| `docs/analysis/ssr/ssr-master-report.md` | This document — synthesis of all findings |
| `docs/analysis/ssr/README.md` | Index of all SSR analysis files |
| `docs/analysis/rendering/ue5-ssr-architecture.md` | UE5 SSR deep dive (Hi-Z, VNDF, denoiser, all constants) |
| `docs/analysis/rendering/frostbite-stochastic-ssr-analysis.md` | Frostbite/DICE (Stachowiak 2015, quad-ray, Hi-Z, VNDF) |
| `docs/analysis/rendering/ssr-state-of-the-art-2025.md` | AMD SSSR, Unity HDRP, STBN, modern techniques |
| `docs/analysis/rendering/next-gen-reflections-analysis-2025.md` | UE5 Lumen, cascade architecture, future of SSR |
| `docs/analysis/godot4-ssr-technical-analysis.md` | Godot 4.2 + 4.5 (parametric T Hi-Z, Karis tonemap) |
| `docs/analysis/rendering/three-js-ssr-technical-analysis.md` | Three.js (DDA, no temporal, reference for what NOT to do) |
| `docs/analysis/rendering/cryengine-ssr-technical-analysis.md` | CryEngine V (linear march, blur pyramid, no temporal) |
| `docs/analysis/rendering/id-tech-ssr-industry-analysis.md` | id Tech / DOOM, Decima, Naughty Dog, Insomniac, Snowdrop |
| `docs/analysis/rendering/ssr-academic-survey-part1.md` | Papers: McGuire, Stachowiak, Heitz VNDF, Karis, SVGF |
| `docs/analysis/rendering/ssr-academic-survey-part2.md` | Papers: STBN, AMD SSSR passes, NRD, comparison matrix |
