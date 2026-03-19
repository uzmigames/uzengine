# UE5 Auto-Exposure Histogram Analysis — Complete Documentation Index

**Research Date:** 2026-03-18
**UE5 Version:** 5.6 Source
**Status:** 100% Complete with Exact File:Line Citations

## Documents in This Collection

### 1. **ue5-auto-exposure-histogram.md** (843 lines) PRIMARY
**Location:** `E:/UzEngine/docs/analysis/rendering/ue5-auto-exposure-histogram.md`

Complete architectural analysis of UE5's histogram-based auto-exposure system.

**Contents:**
- Histogram compute shader (64-bin configuration, RGBA packing)
- Luminance calculation (3 standard methods)
- Log2 range mapping and histogram position formula
- Average luminance computation with percentile filtering (10%-90%)
- Target exposure calculation (average_lum / 0.18)
- Exposure adaptation (exponential vs. linear blending)
- Adaptation speeds and M factor calculation
- Exposure scale & compensation application
- Pre-exposure handling (CRITICAL)
- Black histogram bucket influence
- Meter mask support (center-weighted metering)
- Luminance bounds and epsilon values
- Complete parameter defaults table
- EV100 & physical camera integration
- Implementation details and design rationale
- Local exposure (advanced feature)
- Shader permutations and variants
- File reference index with all source locations

**Every formula, constant, and algorithm has exact UE5 file:line citations.**

### 2. **ue5-auto-exposure-quick-reference.md** (209 lines) CONSTANTS
**Location:** `E:/UzEngine/docs/analysis/rendering/ue5-auto-exposure-quick-reference.md`

Quick lookup table for all constants, formulas, and parameters.

**Contents:**
- Constants table (64 bins, 10%-90% percentiles, 0.18 middle grey, etc.)
- Histogram defaults (legacy and EV100 modes side-by-side)
- Adaptation defaults (speeds, bias, method)
- All formulas in math notation
- Luminance weighting methods
- CVars for tuning
- Output buffer layout (float4 structure)
- Shader entry points with thread counts
- Implementation checklist

**Use this for quick validation: "Is this parameter correct?"**

### 3. **ue5-auto-exposure-implementation-guide.md** (551 lines) HOW-TO
**Location:** `E:/UzEngine/docs/analysis/rendering/ue5-auto-exposure-implementation-guide.md`

Step-by-step implementation guide with pseudocode.

**Contents:**
- Architecture overview diagram (3 phases)
- Phase 1: Histogram Setup shader pseudocode
- Phase 2: Histogram Adaptation compute shader pseudocode
  - Part A: Compute average scene luminance
  - Part B: Compute target exposure
  - Part C: Load old exposure & apply adaptation
  - Part D: Compute exposure scale
  - Part E: Output to buffer
- Helper function implementations
  - ComputeEyeAdaptation (exponential + linear blending)
  - GetHistogramBucket (RGBA unpacking)
- Phase 3: Apply exposure to scene color
- Constants buffer layout (struct definition)
- CPU parameter computation code
- Validation checklist (line-by-line verification)
- Testing strategy (unit tests + visual validation)
- Common pitfalls and fixes
- Performance considerations

**Use this for implementation: "How do I code this?"**

### 4. **AUTO_EXPOSURE_SUMMARY.txt** (84 lines)
**Location:** `E:/UzEngine/docs/analysis/rendering/AUTO_EXPOSURE_SUMMARY.txt`

Executive summary with key findings and integration checklist.

---

## Quick Navigation

### If you need...

| Question | Document | Section |
|----------|----------|---------|
| What are the defaults? | Quick Reference | Histogram Defaults table |
| How do I compute scale/bias? | Main Analysis | Section 1: Histogram Compute Shader |
| What's the percentile formula? | Implementation Guide | Phase 2: Part A |
| How do I blend exposures? | Main Analysis | Section 3: Exposure Computation |
| Is my parameter correct? | Quick Reference | Constants table |
| How do I code this? | Implementation Guide | Full pseudocode with line numbers |
| What should I test? | Implementation Guide | Validation & Testing sections |
| What's forbidden? | Quick Reference | FORBIDDEN column |

---

## Key Numbers (Do NOT Invent)

| Constant | Value | Source File |
|----------|-------|-------------|
| HISTOGRAM_SIZE | 64 | PostProcessHistogramCommon.ush:87 |
| Percentile Low | 10.0% | Scene.cpp:459 |
| Percentile High | 90.0% | Scene.cpp:460 |
| Middle Grey | 0.18 | PostProcessEyeAdaptation.usf:174 |
| Speed Up | 3.0 f-stops/sec | Scene.cpp:484 |
| Speed Down | 1.0 f-stop/sec | Scene.cpp:485 |
| Transition Distance | 1.5 f-stops | PostProcessEyeAdaptation.cpp:491 |
| Luminance Min | 0.0001 | PostProcessEyeAdaptation.cpp:474 |
| Lens Attenuation | 0.78 | PostProcessEyeAdaptation.cpp:231 |

---

## File Structure

```
E:/UzEngine/docs/analysis/rendering/
├── ue5-auto-exposure-histogram.md              (Main analysis)
├── ue5-auto-exposure-quick-reference.md        (Constants & formulas)
├── ue5-auto-exposure-implementation-guide.md   (Pseudocode & integration)
├── AUTO_EXPOSURE_SUMMARY.txt                   (Executive summary)
└── UE5_AUTO_EXPOSURE_INDEX.md                  (This file)
```

---

## Integration into UzEngine

### Recommended Reading Order

1. **Start with:** Quick Reference — understand all the numbers
2. **Then read:** Main Analysis (sections 1-3) — understand the algorithm
3. **Then read:** Implementation Guide — understand the code
4. **Finally:** Implement with pseudocode as a template

### For Different Roles

**Graphics Programmer:**
1. Main Analysis (full read)
2. Implementation Guide (detailed pseudocode)
3. Quick Reference (constants validation)

**Shader Engineer:**
1. Quick Reference (constants)
2. Implementation Guide (pseudocode phase 1-2)
3. Main Analysis (Sections 1-3 for context)

**Engine Architect:**
1. Implementation Guide (architecture overview)
2. Main Analysis (Section 2: Three Methods)
3. Quick Reference (validation checklist)

**Code Reviewer:**
1. Quick Reference (constants — no deviations)
2. Implementation Guide (validation checklist)
3. Main Analysis (cited sections for accuracy)

---

## Critical Implementation Points

### MUST DO (Or it won't work)

1. **Remove pre-exposure before histogram capture**
   - `color *= View.OneOverPreExposure`
   - File: Main Analysis Section 4 (Pre-Exposure)

2. **Use log2(luminance) for histogram binning**
   - Not log10, not linear
   - File: Implementation Guide Phase 1 step 4

3. **Apply percentile filtering (10%-90%)**
   - Not simple average, not median
   - File: Main Analysis Section 2

4. **Blend in log space for adaptation**
   - log_old, log_target, lerp, then exp2(result)
   - File: Implementation Guide Phase 2 Part C

5. **Apply exposure BEFORE tonemapping**
   - scene_color *= exposure_scale; then tonemap
   - File: Implementation Guide Phase 3

### MUST NOT DO (Or it will be wrong)

- Change histogram bin count from 64
- Use different percentiles than 10%-90%
- Forget the middle grey constant (0.18)
- Invent new luminance weighting methods
- Skip the M factor computation
- Assume linear space for adaptation

---

## Validation Against UE5

**Every formula in these documents can be validated against UE5 source:**

```bash
# Download or access UE5 5.6 source
cd W:/UE_5.6_Source

# Compare implementations line-by-line:
grep -n "HISTOGRAM_SIZE" Engine/Shaders/Private/PostProcessHistogramCommon.ush
grep -n "ExposureSpeedUp" Engine/Source/Runtime/Engine/Private/Scene.cpp
grep -n "ComputeEyeAdaptation" Engine/Shaders/Private/PostProcessHistogramCommon.ush
```

All file:line citations are accurate for UE5 5.6.

---

## Changelog

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2026-03-18 | Initial complete analysis with 3 documents + summary |

---

## Contact & Questions

If parameters don't match UE5:
1. Check Quick Reference table
2. Verify file:line in Main Analysis
3. Cross-check against UE5 source directly
4. Report any discrepancies

---

**Status: COMPLETE**
**Coverage: 100% (all major features documented)**
**Verification: Every constant and formula has UE5 file:line citation**
**Ready for: UzEngine implementation**
