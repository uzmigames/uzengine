<!-- doc-meta version="1" -->

# UE5 Temporal Super Resolution (TSR) Analysis

This folder contains a comprehensive analysis of UE5 5.6's Temporal Super Resolution system, extracted from source code at `W:\UE_5.6_Source`.

## Document Structure

### 1. **ue5-tsr-architecture.md** (Primary Reference)

**Length**: ~3,000 lines | **Audience**: Architects, lead engineers

Comprehensive overview of TSR system:
- **ITemporalUpscaler interface** (abstract class contract)
- **11 compute shader passes** (purpose, inputs/outputs, permutations)
- **Key CVars** (complete reference table)
- **Velocity dilation algorithm** (spatial anti-aliasing, reprojection fields)
- **History rejection mechanics** (disocclusion, shading changes, flickering)
- **Spatial anti-aliasing** (when applied, quality tiers)
- **Multi-frame history resurrection** (experimental feature)
- **Architecture adaptation recommendations** (how to implement for UzEngine)

**Best for**:
- Understanding the overall pipeline
- Identifying which passes to implement first
- Determining architectural patterns to replicate
- Planning phase schedule

### 2. **ue5-tsr-blend-factors.md** (Temporal Accumulation Deep-Dive)

**Length**: ~1,500 lines | **Audience**: Render engineers, shader writers

Detailed temporal blending strategy:
- **Three-layer blend factor system** (histeresis + velocity clamping + rejection)
- **Blend factor computation algorithm** (pseudo-code walkthrough)
- **CVar tuning guide** (cinematic vs competitive profiles)
- **Metadata encoding** (sample count tracking)
- **Velocity clamping mechanics** (why clamp, non-linear transition)
- **Flickering detection impact** (frame rate adjustment)
- **Thin geometry relaxation** (sub-pixel geometry handling)
- **Quality tiers** (Low/Medium/High/Epic permutations)
- **Anti-aliasing interaction** (when spatial AA applies)

**Best for**:
- Implementing the `UpdateHistory` pass
- Tuning temporal stability vs responsiveness
- Understanding per-game CVars
- Debugging ghosting/flickering artifacts

### 3. **ue5-tsr-rejection-shading.md** (History Rejection Deep-Dive)

**Length**: ~1,200 lines | **Audience**: Shader engineers, algorithm specialists

Detailed history rejection pass:
- **Rejection criteria** (disocclusion, shading changes, flickering, material changes)
- **Neural convolution network** (why "neural", LDS tiling, wave optimizations)
- **Input textures** (complete table, format, purpose)
- **Output channels** (HistoryRejectionOutput semantics)
- **Per-pixel rejection algorithm** (pseudo-code with all criteria)
- **Permutation logic** (16-bit VALU, flickering, resurrection dependencies)
- **Compilation priority** (why 40+ seconds)
- **Translucency integration**
- **Decision logic summary** (when to reject)

**Best for**:
- Implementing the `RejectShading` pass
- Understanding neural convolution architecture
- Designing rejection heuristics (if not using neural approach)
- Debugging false rejections/ghosts

## Quick Navigation

| Question | See Document | Section |
|----------|--------------|---------|
| "What are all 11 TSR passes?" | architecture.md | Section 2 |
| "How does velocity clamping work?" | blend-factors.md | Layer 2 |
| "What makes history invalid?" | rejection-shading.md | Rejection Criteria |
| "When does spatial AA apply?" | architecture.md | Section 6 |
| "What are the recommended CVars?" | blend-factors.md | CVar Tuning Guide |
| "How do I implement LDS tiling?" | rejection-shading.md | Tiling Strategy |
| "What's the sample count formula?" | blend-factors.md | Metadata Encoding |
| "How to detect flickering?" | rejection-shading.md | Step 3 |

## Key Statistics

| Metric | Value |
|--------|-------|
| Total analysis lines | ~5,700 |
| UE5 files referenced | 20+ |
| CVars documented | 30+ |
| Shader passes analyzed | 11 |
| Shader classes (C++) | 11 |
| Permutation dimensions | 15+ |
| Key algorithms (pseudo-code) | 5 |
| Tuning profiles | 3 |

## Critical Implementation Order

For UzEngine adaptation, implement in this order:

1. **Phase 1: Velocity & Reprojection**
   - `DilateVelocity` pass (spatial dilation with butterfly sampling)
   - `DecimateHistory` pass (bilinear reprojection)
   - See: architecture.md §2.2–2.3

2. **Phase 2: Basic Rejection**
   - Simple rejection heuristics (depth, luma, motion)
   - `SpatialAntiAliasing` pass (3-iteration edge detection)
   - See: rejection-shading.md, architecture.md §6

3. **Phase 3: Temporal Accumulation**
   - `UpdateHistory` pass (velocity-clamped blending)
   - Metadata encoding (sample count)
   - See: blend-factors.md

4. **Phase 4: Neural Rejection (Optional)**
   - Implement convolution network if needed
   - Otherwise, hand-code rejection heuristics
   - See: rejection-shading.md §Neural Convolution

5. **Phase 5: Advanced Features**
   - Flickering detection (`MeasureFlickeringLuma`)
   - Thin geometry detection
   - Multi-frame resurrection
   - See: architecture.md §8–9

## CVar Reference

**Essential CVars** (implement first):
- `r.TSR.History.SampleCount` [8–32, default 16]
- `r.TSR.History.ScreenPercentage` [100–200, default 100]
- `r.TSR.Velocity.WeightClampingPixelSpeed` [float, default 1.0]
- `r.TSR.Velocity.WeightClampingSampleCount` [float, default 4.0]
- `r.TSR.RejectionAntiAliasingQuality` [0–3, default 3]

**Advanced CVars** (later phases):
- `r.TSR.ShadingRejection.Flickering` [bool, default 1]
- `r.TSR.ShadingRejection.Flickering.Period` [float, default 2.0]
- `r.TSR.ThinGeometryDetection` [bool, default 0]
- `r.TSR.ReprojectionField` [bool, default 0]
- `r.TSR.Resurrection` [bool, default 0]

See: blend-factors.md §CVar Tuning Guide

## Formula Reference

### Base Alpha (Histeresis)
```
Alpha = 1.0 / HistorySampleCount
```
At sample count 16: Alpha = 0.0625 (93.75% history, 6.25% new)

### Velocity Clamping
```
if (VelocityMagnitude > WeightClampingPixelSpeed):
    ClampAlpha = 1.0 / WeightClampingSampleCount  // 1/4 = 0.25
    BlendFactor = saturate((Velocity - Threshold) / Threshold)
    Alpha = lerp(BaseAlpha, ClampAlpha, BlendFactor)
```

### Flickering Period (Frame Rate Adjusted)
```
RefreshRateToFrameRateCap = DeltaTime * FrameRateCap
AdjustedPeriod = Period / RefreshRateToFrameRateCap
```
At 30 FPS with FrameRateCap=60: AdjustedPeriod = 2.0 * (30/60) = 1.0 frame

See: blend-factors.md §Blend Factor Computation

## Shader Compilation Notes

| Pass | Compilation Time | Why | Mitigation |
|------|------------------|-----|-----------|
| RejectShading | 40+ sec | Massive unrolled convolutions | ExtraHigh priority |
| UpdateHistory | 10 sec | 4 quality tiers, 16-bit VALU | Precache common |
| SpatialAntiAliasing | 5 sec | Edge-following recursion | Wave optimized |
| Others | < 2 sec | Simple kernels | Standard |

**Total TSR compile**: ~60 seconds (can overlap with other shaders)

## Performance Characteristics

| Metric | Value | Notes |
|--------|-------|-------|
| Typical overhead | 1–3 ms | At 1440p, depends on scene complexity |
| Memory bandwidth | 2–4 GB/s | History buffer reads + writes |
| LDS usage | ~12 KB | Per work group (32×32 + overscan) |
| Register usage | High | Wave operations reduce pressure |
| AsyncCompute | Yes | 6 of 11 passes can overlap |

---

## Cross-References to UE5 Source

All line numbers reference `W:\UE_5.6_Source`:

**Header**:
- `Renderer/Public/TemporalUpscaler.h` — Interface definition

**C++**:
- `Renderer/Private/PostProcess/TemporalSuperResolution.cpp` — All shader classes, CVars, pass setup

**Shaders** (all in `Engine/Shaders/Private/TemporalSuperResolution/`):
- `TSRDilateVelocity.usf` — Velocity dilation with reprojection field
- `TSRDecimateHistory.usf` — History reprojection
- `TSRRejectShading.usf` — Neural convolution rejection
- `TSRSpatialAntiAliasing.usf` — Edge-based blur
- `TSRUpdateHistory.usf` — Temporal blending
- `TSRResolveHistory.usf` — 200% → 100% downsampling
- `TSRMeasureFlickeringLuma.usf` — Luminance recording
- Plus 6 include files (TSRCommon.ush, TSRShadingAnalysis.ush, etc.)

---

## Questions for Implementers

### On Velocity Dilation
- How precise must the reprojection field jacobian be? (3-bit? 4-bit?)
- Should we implement full butterfly sampling or simplified 5-tap?
- Is edge-following necessary, or can we use max-depth dilation?

### On Rejection
- Should we implement neural convolution or hand-coded heuristics?
- What's the minimum rejection criteria (depth + luma only)?
- How aggressive should flickering detection be?

### On Tuning
- For UzEngine's default demo, which game profile? (Cinematic or Competitive)
- Should we expose all 30+ CVars or a curated subset?
- What's the target quality tier? (Low/Medium/High/Epic)

---

## Related Documents

- **UE5 RHI Analysis**: `docs/analysis/ue5-rhi-architecture.md` (for RHI abstraction patterns)
- **UzEngine Rendering Spec**: `docs/specs/renderer.md` (target integration point)
- **Temporal Upsampling Spec**: `docs/specs/temporal-upsampling.md` (UzEngine-specific requirements)

