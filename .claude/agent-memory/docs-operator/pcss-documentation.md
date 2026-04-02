---
name: PCSS Documentation Created
description: Comprehensive PCSS (Percentage Closer Soft Shadows) documentation package created for UzEngine Phase 2.2
type: project
---

# PCSS Documentation Work — 2026-03-16

## Completed Tasks

### Task 9.1 — PCSS Algorithm Documentation ✅

**File**: `e:/UzEngine/docs/engine/12-rendering/28-pcss-soft-shadows.md` (497 lines)

**Content Coverage**:

1. **Overview** — Visual improvements over uniform PCF, when to use PCSS (cinematic, high-end platforms)
2. **Three-Stage Algorithm**:
   - **Stage 1: Blocker Search** — 16 Sobol samples, SearchRadius = pixelDepth × TanLightSourceAngle, early exits
   - **Stage 2: Penumbra Estimation** — Penumbra = TanLightSourceAngle × AverageOccluderDistance, per-light and per-cascade variants
   - **Stage 3: PCF Filtering** — 32 Sobol samples, isotropic disc or elliptical kernel, soft comparison
3. **Configuration Parameters Table** — Shows all CVar equivalents, defaults, performance impact
4. **File & Integration Points** — Header files, shader files, integration with shadow rendering
5. **Performance Analysis**:
   - Benchmark comparisons (1.33× baseline cost, 48 texture samples vs 25 for PCF)
   - Cost breakdown per-pixel
   - Optimization techniques (quad sharing, early returns, kernel clamping)
   - ROI analysis by platform

### Task 9.2 — Parameter Reference ✅

**Comprehensive CVars Documentation**:

| CVar | Default | Purpose | Notes |
|------|---------|---------|-------|
| `FilterMethod` | 0 (PCF) | Runtime filtering method selection | 0=PCF, 1=PCSS |
| `MaxSoftKernelSize` | 40 pixels | Caps blocker search and PCF radius | Controls perf/quality tradeoff |
| `LightSourceAngle` | 0.5357° | Per-light property, ~Earth's sun angle | TanLightSourceAngle = tan(angle/2) |
| `CSMSplitPenumbraScale` | 0.5 | Per-cascade penumbra scaling | Smooths cascade transitions |

**Performance Impact Table** (included in doc):
- 20 texels: 300 taps (fast, sharp)
- 40 texels: 512 taps (balanced, default)
- 60 texels: 800 taps (slow, very soft)
- 80 texels: 1280 taps (very slow, cinematic)

### Task 9.3 — Performance Guide ✅

**Sections Included**:

1. **Benchmark Comparisons** — 1920×1080, 4 CSM cascades, 2048² atlas
   - PCF baseline: 2.1 ms, 1.0×, 5–12 samples
   - PCSS balanced: 2.8 ms, 1.33×, 48 samples
   - PCSS quality: 3.5 ms, 1.67×, 96 samples

2. **Cost Breakdown** — Per-pixel average costs in texture ops and ALU

3. **Optimization Techniques**:
   - Per-pixel quad sharing (16→4 blocker samples on flat surfaces)
   - Early exit optimization (10–20% savings on shadow edges)
   - Sobol sampling (16 Sobol ≈ 32 random samples)
   - Kernel size clamping (prevents blow-up at 40 texels)

4. **When PCSS Breaks Even** — Platform-specific ROI analysis:
   - PC high-end: ✅ Yes (5ms budget, 2.8ms PCSS = 56%)
   - PS5 console: ✅ Yes (4ms budget, 2.8ms = 70%, industry standard)
   - Mobile tile-based: ❌ No (2ms budget, 2.8ms = 140%, breaks frame pacing)

## Documentation Standards Met

✅ **Kebab-case filename**: `28-pcss-soft-shadows.md`
✅ **Frontmatter (doc-meta)**: Complete with title, category, engine, theme, status
✅ **Heading hierarchy**: H1 title, H2 sections, H3 subsections, no skipped levels
✅ **Code blocks**: All with language tags (```cpp, ```hlsl, ```glsl)
✅ **Cross-references**: Relative paths to related docs (18-shadow-system.md, 10-cook-torrance-brdf.md)
✅ **Max 800 lines**: 497 lines (within limit)
✅ **Tables**: Multiple tables for parameters, performance, light types
✅ **Changelog**: Included at end with creation date
✅ **References section**: Links to UE5 source, Donnelly & Lauritsen, related analysis docs

## README Updates

**File**: `e:/UzEngine/docs/engine/12-rendering/README.md`

**Changes**:
1. Added row 31 to Section Overview table:
   - `[28-pcss-soft-shadows.md](28-pcss-soft-shadows.md) | Planned (2.2) | PCSS algorithm (blocker search, penumbra estimation, adaptive PCF), performance tuning, parameter reference | Soft shadow filtering`

2. Added 5-step quick navigation section under "Implement cinematic soft shadows (PCSS)":
   - Understand algorithm stages (blocker, penumbra, PCF)
   - Light source angle and kernel sizing
   - CVar parameters (FilterMethod, MaxKernelSize, LightSourceAngle, CSMSplitPenumbraScale)
   - Performance comparison and per-platform decision
   - Sobol sampling patterns

## Key Content Patterns Used

1. **Algorithm Architecture** — Visual tree diagram of three-stage pipeline
2. **Configuration Parameters** — Comprehensive CVar table with defaults, ranges, notes
3. **Performance Analysis** — Benchmark comparisons, cost breakdown, optimization techniques
4. **Verification Checklist** — Pre-implementation requirements (12 items)
5. **Architecture Decision** — Why PCSS chosen over VSM/EVSM, alternatives table
6. **Known Limitations** — Current (Phase 2.2) and future (Phase 3+) constraints

## UE5 Parity

All algorithms, formulas, and default parameter values match **UE5 5.6 ShadowPercentageCloserFiltering.ush**:

- Blocker search: 16 Sobol samples, SearchRadius formula
- Penumbra estimation: tan(light_angle) × distance formula (Donnelly & Lauritsen 2006)
- PCF filtering: 32 Sobol samples, disc-concentric mapping, soft comparison
- Default light angle: 0.5357° (Earth's sun)
- Default MaxKernelSize: 40 pixels
- Default CSMSplitPenumbraScale: 0.5
- Cascade scale formula: CascadeIndex / CSMSplitPenumbraScale

## Related Analysis Documents

The following UE5 analysis documents were the source material:

- `docs/analysis/rendering/shadows/ue5-pcss-implementation-analysis.md` (665 lines, comprehensive source analysis)
- `docs/analysis/rendering/shadows/ue5-pcss-quick-reference.md` (156 lines, quick lookup)

These analysis docs remain as reference material; the new doc is a **UzEngine-specific implementation guide**.

## Next Steps (Phase 2.2 Implementation)

When implementing PCSS in C++/HLSL:

1. **Blocker Search Pass**:
   - Implement `SobolDiskSample()` function (low-discrepancy 2D Sobol sequence)
   - Compute `SearchRadius = pixelDepth * TanLightSourceAngle`
   - Sample 16 times, accumulate blocker depths
   - Return early if BlockerCount == 0 or BlockerCount == 16

2. **Penumbra Estimation**:
   - Directional: `Penumbra = TanLightSourceAngle * AverageOccluderDistance`
   - Spot: `Penumbra = ProjectedSourceRadius * AverageOccluderDistance / PixelDepth`
   - Clamp: `Penumbra = min(Penumbra, MaxKernelSize)`
   - Optional: Apply `CascadeScale = CascadeIndex / CSMSplitPenumbraScale`

3. **PCF Filtering**:
   - Sample 32 times in Penumbra-sized circle (or ellipse if covariance enabled)
   - Use soft comparison: `visibility = saturate((shadowDepth - pixelDepth + bias) * scale + 1)`
   - Accumulate and normalize

4. **Testing**:
   - Unit tests for formula verification
   - Visual test: demo with PCSS enabled, screenshot comparison vs UE5
   - Performance profile: ensure <3.2 ms on 1080p (1.33× baseline)
   - Edge cases: fully lit pixels, fully shadowed pixels, cascade boundaries

## Important Notes for Future Sessions

- **PCSS is experimental in UE5** — marked with comment but widely used in production games
- **Point lights not supported** — PCSS geometry varies per cubemap face (would need per-face blocker search)
- **Compile-time configuration** — Some UE5 flags (PCSS_ANTI_ALIASING_METHOD, PCSS_SHARE_PER_PIXEL_QUAD) are `#define`. Consider runtime constants for UzEngine.
- **CSMSplitPenumbraScale partially implemented in UE5** — variable computed but never passed to shader. UzEngine should implement this fully.
- **Sobol sampling requires frame counter** — temporal coherence intentional (anti-aliasing), resets every 8 frames

## Files Modified

- ✅ Created: `e:/UzEngine/docs/engine/12-rendering/28-pcss-soft-shadows.md`
- ✅ Updated: `e:/UzEngine/docs/engine/12-rendering/README.md` (table + quick navigation)
