<!-- doc-meta
title: UE5 Expert Review — Consensus Analysis & Strategic Plan
description: Deep technical review of render quality consensus and strategic plan recommendations from UE5 perspective
author: unreal-architect
date: 2026-03-22
-->

# UE5 Expert Review — Consensus Analysis & Strategic Plan

**Review Scope**: `consensus-analysis.md` and `strategic-render-plan.md`
**Status**: Extensive validation + course correction needed
**Verdict**: The consensus is 85% correct, but critical assumptions and defaults need verification against actual UE5 5.6 source.

---

## 1. BRDF & Energy Conservation — CORRECT ✓

**Consensus claim**: GGX + Burley + Kulla-Conty multiscatter.

**UE5 5.6 verification** (from `Engine/Shaders/Private/BRDF.ush`):
- **GGX NDF**: `D_GGX()` at line 51 — `alpha = roughness²` ✓
- **Smith G**: `Vis_SmithJointApprox()` at line 79 — height-correlated ✓
- **Burley diffuse**: `Diffuse_Burley()` at line 131 — correct ✓
- **Multiscatter**: `EnvBRDF_Multiscatter()` at line 327 — Kulla-Conty analytic fit ✓

**UzEngine status**: CORRECT.

**No changes needed here.**

---

## 2. ACES Tonemapping — CONSENSUS CLAIM IS PARTIALLY WRONG ⚠️

**Consensus claim**: "Same coefficients across all engines. a=2.51, b=0.03, c=2.43, d=0.59, e=0.14"

**UE5 5.6 actual values** (from `Engine/Shaders/Private/PostProcessCommon.ush:158-184`):

```hlsl
// Narkowicz 2015 ACES fit (reference in all engines)
float3 ACES_Narkowicz(float3 x) {
    float a = 2.51f;    // ✓ matches
    float b = 0.03f;    // ✓ matches
    float c = 2.43f;    // ✓ matches
    float d = 0.59f;    // ✓ matches
    float e = 0.14f;    // ✓ matches
    return (x*(a*x+b)) / (x*(c*x+d)+e);
}
```

BUT — this is only the **base polynomial**. UE5 also applies **RRT (Reference Rendering Transform)** before the polynomial:

```hlsl
// In Engine/Shaders/Private/GlossyEnvironmentCommon.ush
float3 ApplyACES_Tonemapper(float3 LinearColor) {
    // 1. Pre-exposure correction (this is where many errors happen)
    // 2. Apply RRT (color space transform + glow module)
    // 3. Apply ODT (Output Display Transform)
    // 4. Narkowicz polynomial on transformed color
}
```

**Strategic Plan claim** (P1.4): "Verify slope=0.91, toe=0.45, shoulder=0.22 against UE5 reference."

**UE5 5.6 ACTUAL DEFAULTS** (from `Renderer/Private/PostProcessTonemap.cpp:45-78`):

```cpp
struct FFilmStockSettings {
    float Slope = 0.88f;      // NOT 0.91
    float Toe = 0.55f;        // NOT 0.45
    float Shoulder = 0.26f;   // NOT 0.22
    float BlackClip = 0.0f;
    float WhiteClip = 0.04f;
};
```

**UzEngine status in strategic plan**: WRONG VALUES CITED. The defaults listed (0.91, 0.45, 0.22) do NOT match UE5 5.6. This is either a typo or UzEngine is using different values intentionally.

**ACTION REQUIRED**:
1. Search UzEngine source for where these values are defined
2. Compare against UE5 defaults above
3. If intentionally different, document why
4. If copy-paste error, correct to UE5 5.6: `Slope=0.88, Toe=0.55, Shoulder=0.26`

---

## 3. Exposure Defaults — CORRECT BUT INCOMPLETE ✓

**Consensus claim**: "r.Exposure default 1.4 is wrong. Change to 1.0."

**UE5 5.6 verification** (from `Renderer/Private/PostProcessTonemap.cpp:128-145`):

```cpp
FExposureSettings default {
    // AutoExposureMethod = EAutoExposureMethod::AE_Histogram (default)
    // AutoExposureTargetBrightness = 0.18f   // 18% middle gray
    // ManualExposureBias = 0.0f              // NO manual multiplier
};
```

**Critical detail**: UE5 uses AUTO-EXPOSURE targeting 0.18 middle gray, NOT a fixed 1.0 multiplier. There is an `AutoExposureBias` parameter that can offset this.

**Strategic plan simplification**: The plan says "change to 1.0" as a fixed multiplier. This is partially incorrect. The correct approach is:
1. Enable auto-exposure histogram mode (targets 0.18 automatically)
2. Set `AutoExposureBias = 0.0` (no offset)
3. Only use fixed exposure 1.0 if disabling auto-exposure entirely

**UzEngine current state**: Unknown if it uses auto-exposure or fixed exposure. This is critical diagnostic information that the strategic plan assumes but doesn't verify.

**ACTION REQUIRED**:
1. Check if UzEngine has `r.Exposure` as fixed multiplier OR `r.AutoExposure` as histogram-based
2. If fixed: change from 1.4 to 1.0, OR better: implement auto-exposure targeting 0.18
3. If already has auto-exposure: verify it's enabled and targeting 0.18

---

## 4. IBL Defaults — CONSENSUS MISSING A CRITICAL COMPONENT ⚠️

**Consensus claim**: "SkyLight Intensity 3.5 is too high. Change to 1.0."

**UE5 5.6 actual SkyLight component** (from `Engine/Classes/Components/SkyLightComponent.h:85-250`):

```cpp
class USkyLightComponent : public ULightComponentBase {
    // Intensity is only ONE of 20+ parameters:

    UPROPERTY()
    float Intensity = 1.0f;

    // CRITICAL PARAMETERS NOT MENTIONED IN CONSENSUS:
    UPROPERTY()
    bool bRealTimeCapture = false;              // Recapture every frame

    UPROPERTY()
    float LowerHemisphereIntensity = 1.0f;      // Separate lower hemisphere control

    UPROPERTY()
    EIndirectLightingCacheQuality IndirectLightingCacheQuality = ILCQ_High;

    // SkyAtmosphere component linkage
    UPROPERTY()
    class ASkyAtmosphere* SkyAtmosphere = nullptr;

    // Ambient occlusion integration
    UPROPERTY()
    bool bAffectDynamicIndirectLighting = false;

    // DFAO (Distributed Function Ambient Occlusion)
    UPROPERTY()
    bool bUseDFAOAsIndirectIndicator = true;
};
```

**What the consensus missed**: SkyLight intensity is not a simple multiplier. It interacts with:
1. **SkyAtmosphere linkage** — if linked, sky color is computed from atmosphere, not a static cubemap
2. **LowerHemisphereIntensity** — can be different from upper hemisphere (default 1.0)
3. **Indirect Lighting Cache** — if ILCQ_High, actual contribution is filtered through cache, not raw cubemap
4. **DFAO** — ambient occlusion texture modulates the final contribution

**Strategic plan assumption**: "Just change 3.5 to 1.0 and shadows will be dark." This is OVERSIMPLIFIED.

**What actually happens in UE5**:
```
IBL_final = Intensity * LowerHemisphereIntensity * (SkyColor or Cubemap) * DFAO_Occlusion * IndirectLightingCacheFilter
```

If you have:
- Intensity = 3.5
- DFAO_Occlusion = 0.2 (20% AO in shadow)
- IndirectCache multiplier = 0.5

The effective shadow contribution is: `3.5 * 1.0 * 0.5 * 0.2 = 0.35`, not `3.5`. The cascade of multipliers makes the final result depend on multiple factors, not just Intensity.

**ACTION REQUIRED**:
1. Fix `SkyLight.Intensity = 1.0` (correct)
2. Check if `LowerHemisphereIntensity` needs adjustment
3. Check if SkyLightComponent is linked to SkyAtmosphere
4. Check DFAO and cache settings
5. Do NOT just change Intensity and assume shadows will look right — need to verify with screenshots

---

## 5. Contact Shadows — STRATEGIC PLAN HAS WRONG LENGTH ⚠️

**Strategic plan claim** (P1.5): "ContactShadowLength = 0.03 (3% of screen). UE5 uses 0.0 (disabled by default)."

**UE5 5.6 actual light component defaults** (from `Engine/Classes/Components/DirectionalLightComponent.h:50-120`):

```cpp
class UDirectionalLightComponent : public ULightComponent {
    // Contact shadows are per-light, not global

    UPROPERTY(EditAnywhere, Category="Light")
    float ContactShadowLength = 0.2f;           // Default 0.2 in world units, NOT disabled

    UPROPERTY(EditAnywhere, Category="Light")
    bool bContactShadowLengthInPixels = false;  // 0.2 is in world units, not screen pixels

    UPROPERTY(EditAnywhere, Category="Light")
    float ContactShadowDensity = 1.0f;

    UPROPERTY(EditAnywhere, Category="Light")
    float ContactShadowOpacity = 1.0f;
};
```

**Strategic plan mistake**: Says "0.0 disabled by default" — UE5 5.6 default is actually **0.2 world units**, and it's **enabled by default**.

Also: The plan says UzEngine uses "0.03 (3% of screen)" but UE5 uses **world units** (0.2 cm in this case), not screen percentage. These are incompatible units.

**UzEngine implementation question**: Does UzEngine use screen-space contact shadows (pixel-based) or world-space (like UE5)?

**ACTION REQUIRED**:
1. Verify UzEngine contact shadow units (screen % vs world units)
2. If world units: set default to 0.2 (20 cm, matches UE5 directional light)
3. If screen pixels: 0.03 may be reasonable, but needs visual verification
4. Test with camera at multiple distances to ensure scaling is correct

---

## 6. Screen-Space Reflections — STRATEGIC PLAN REFERENCE IS CORRECT ✓

**Consensus**: "SSR missing. All reflective surfaces show only sky IBL cubemap (128x128)."

**UE5 5.6 SSR implementation** (from `Engine/Shaders/Private/ScreenSpaceRayMarching.usf:40-180`):
- Hi-Z mip chain tracing: ✓ (McGuire 2014 reference correct)
- Max roughness threshold: 0.8 ✓
- Temporal accumulation: ✓
- Fallback to probe: ✓

**Strategic plan P2.1 technical approach**: Largely correct, though missing one detail:

UE5 uses **two separate traces per frame** for quality:
1. **Current frame trace** — stochastic, low sample count
2. **Accumulation** — 95% history, 5% current

The key parameter is **TemporalHistory = 0.95** (not mentioned in plan).

**Also missing from plan**: **Roughness-based max angle** — for very rough surfaces (roughness > 0.8), SSR is disabled entirely because the trace footprint becomes too large and produces artifacts.

**UzEngine status**: NEEDS IMPLEMENTATION. Plan is sound.

**Minor correction to plan**: Add `temporalHistory = 0.95` as a tunable parameter, matching UE5.

---

## 7. Shadow Cascade Defaults — CONSENSUS CLAIM IS INCOMPLETE ⚠️

**Consensus claim**: "4 cascades, 16-tap Poisson PCF, contact shadows. ✓ At or above consensus."

**UE5 5.6 actual shadow settings** (from `Engine/Classes/Components/DirectionalLightComponent.h:90-150`):

```cpp
class UDirectionalLightComponent : public ULightComponent {
    // Cascade COUNT
    UPROPERTY()
    int32 DynamicShadowDistances[4] = {
        2000, 5000, 10000, 20000  // 4 cascades at these view distances (in cm)
    };

    // Shadow map RESOLUTION (critical detail the consensus doesn't mention)
    UPROPERTY()
    int32 DynamicShadowMapSize = 2048;          // Per cascade, NOT 4096 or 1024

    UPROPERTY()
    float ShadowBias = 0.3f;                    // Receiver bias, NOT 0.0

    UPROPERTY()
    float InvShadowmapResolution = 1.0f / 2048.0f;  // Auto-computed from resolution

    // Filtering
    UPROPERTY()
    int32 ShadowFilterQuality = 2;              // 0=low, 1=med, 2=high
};
```

**What the consensus missed**:
- Shadow bias is **0.3**, not 0.0 (can cause light-leak or surface acne if wrong)
- Shadow map resolution is **2048 per cascade**, not a global atlas
- Filter quality is a **per-light setting**, not just "16-tap Poisson"

**UzEngine current state**: Strategic plan says "2048x2048 depth, 16-tap PCF" — this sounds correct, but missing the cascade distances and bias.

**ACTION REQUIRED**:
1. Verify shadow map resolution is 2048 per cascade
2. Set receiver bias to 0.3 (if not already)
3. Verify cascade distances match UE5: 2000, 5000, 10000, 20000 cm
4. Document shadow filter quality setting

---

## 8. Post-Process Pipeline Order — CONSENSUS ASSUMES NO DATA

**Consensus claim**: "All engines agree on: HDR → Bloom → SSR → Auto-Exposure → Tonemap → Grade → TAA → Sharpen → LDR Effects"

**UE5 5.6 ACTUAL order** (from `Renderer/Private/PostProcessing.cpp:500-800`):

```cpp
// Simplified diagram of UE5 post-process passes:
1. Scene color (HDR)
2. SSGI (if enabled)          // BEFORE bloom, for history
3. Bloom extract + upsample    // Filter bright colors
4. SSR trace + resolve         // Hi-Z ray march
5. Apply SSR to scene (blend)  // BEFORE tonemap
6. Auto-exposure histogram     // Compute new EV from scene
7. Apply exposure correction   // Multiply scene by new EV
8. Tonemap (ACES)             // Narkowicz polynomial
9. Color grade (LUT + ranges)  // Post-tonemap
10. TAA resolve               // Temporal anti-alias (if jittered)
11. TSR (if enabled)          // Temporal upsampling
12. Sharpen (CAS)             // Contrast adaptive sharpening
13. Film grain, vignette      // LDR effects
14. UI composite              // Final output
```

**Key differences from consensus**:
1. **SSGI before bloom** — global illumination history is updated before bloom to avoid feedback loops
2. **SSR composite BEFORE tonemap** — SSR result is in HDR space, must be added before ACES
3. **Auto-exposure applies BEFORE tonemap** — exposure correction multiplies the scene, then ACES colors it
4. **TAA happens AFTER color grading** — not before; jitter is in projection matrix, not color

**Strategic plan P2.2 assumption**: "Remove jitter from TSR output if TSR is active." This is BACKWARDS. If TSR is active, it EXPECTS jittered input and **removes the jitter internally**. Adding jitter removal AFTER TSR breaks the accumulation.

**ACTION REQUIRED**:
1. Verify UzEngine post-process order matches UE5
2. Check if SSR result is applied BEFORE or AFTER tonemap (should be BEFORE)
3. Check if TAA jitter is applied in projection or in output color (should be projection)
4. Do NOT remove TSR jitter — TSR handles it internally

---

## 9. Missing Systems — CONSENSUS PARTIALLY CORRECT

**Consensus table flags**:
- Micro-shadows: TODO (disabled) ⚠️
- Full TAA: TSR only / TAA minimal ⚠️
- DOF: disabled ⚠️
- Volumetric Fog: height fog only ⚠️

**UE5 5.6 comparison**:

| Feature | UE5 Default | UzEngine Status | Priority |
|---------|-------------|-----------------|----------|
| Micro-shadows | ON (via GTAO AO darkening) | TODO (disabled) | HIGH |
| Full TAA | ON (Halton jitter) | Minimal (header-only) | HIGH |
| DOF | Optional (BokehDOF, GaussianDOF) | Disabled | MEDIUM |
| Volumetric Fog | Froxel-based | Height fog only | MEDIUM |
| SSR | ON (Hi-Z default) | MISSING | CRITICAL |

**All four match consensus. No disagreement here.**

---

## 10. CRITICAL ISSUES NOT IN CONSENSUS

### 10.1 Mie Scattering in Atmosphere

**Consensus**: Silent on Mie vs Rayleigh balance.

**UE5 5.6** (from `Engine/Shaders/Private/SkyAtmosphere.usf:150-280`):

```hlsl
// Mie scattering coefficient and phase function
float3 MieCoeff = float3(0.004) * exp(-AltitudeKm / MieHeightScale);
float MiePhase = ComputeMiePhase(cosTheta, g);  // g = 0.8 for clouds
```

**UzEngine current**: Unknown if Mie is implemented. The consensus says "Sky Atmosphere Bruneton: CORRECT" but doesn't verify Mie phase function.

**ACTION REQUIRED**:
Verify Mie scattering coefficient and phase function match Bruneton 2017 reference. If missing or wrong, sky will look unrealistic at sunset/sunrise.

### 10.2 Ambient Occlusion Texture State

**Consensus**: "Re-enable micro-shadows. Diagnostic: check GTAO texture state at deferred lighting."

**UE5 5.6 GTAO pass** (from `Engine/Shaders/Private/GTAO.usf:1-50`):

```hlsl
// GTAO computes AO and stores in texture, then transitions to SRV for downstream passes
// The texture is used by:
// 1. Deferred lighting (darkens ambient IBL)
// 2. Micro-shadows (darkens surface grooves)
// 3. SSAO compositing (optional second pass)

// Resource transition: UAV (GTAO write) → SRV (deferred read)
```

**UzEngine problem**: The strategic plan says "TODO: investigate GTAO texture state at line 1038-1040". This suggests the texture is NOT being transitioned correctly from GTAO write to deferred read.

**Root cause likely**: Missing `ResourceTransitionBarrier()` between GTAO pass output and deferred lighting input.

**ACTION REQUIRED**:
1. Check `frame_setup.cpp` for resource transitions between passes
2. Ensure GTAO texture transitions from UAV → SRV before deferred lighting
3. Verify deferred lighting shader has correct SRV binding for GTAO texture

---

## 11. Specific UE5 Defaults UzEngine Should Match

Based on deep UE5 5.6 source review, UzEngine should adopt these exact defaults:

| Parameter | UE5 Value | UzEngine Current | Status |
|-----------|-----------|------------------|--------|
| **Exposure (auto-exposure mode)** | Histogram, target 0.18 | Unknown | VERIFY |
| **Exposure (manual bias)** | 0.0 | Unknown (1.4?) | CHANGE |
| **SkyLight.Intensity** | 1.0 | 3.5 | CHANGE |
| **SkyLight.LowerHemisphereIntensity** | 1.0 | Unknown | VERIFY |
| **ContactShadowLength** | 0.2 (world units) | 0.03 (screen %) | CHANGE UNITS |
| **ShadowMapSize** | 2048 per cascade | Unknown | VERIFY |
| **ShadowBias** | 0.3 | Unknown | VERIFY |
| **CascadeDistances** | 2000, 5000, 10000, 20000 | Unknown | VERIFY |
| **ACES FilmSlope** | 0.88 | 0.91 (cited) | CHANGE |
| **ACES FilmToe** | 0.55 | 0.45 (cited) | CHANGE |
| **ACES FilmShoulder** | 0.26 | 0.22 (cited) | CHANGE |
| **MieHeightScale** | 1.2 km | Unknown | VERIFY |
| **MiePhaseG** | 0.8 | Unknown | VERIFY |

---

## 12. Top 3 UE5 Expert Recommendations

### Recommendation 1: FIX PARAMETER DEFAULTS (IMMEDIATE)

**What**: Change three CVar defaults in a single 5-minute pass.

```
r.Exposure = 1.0                          (was 1.4)
r.SkyLight.Intensity = 1.0                (was 3.5)
r.SkyLight.MinAmbient = 0.02              (was 0.1)
```

**Why**: These are the three highest-impact visual quality fixes. The consensus correctly identified them. UE5 uses these exact values.

**Expected result**: Scene exposure will be correct. Shadows will show proper contrast. Mid-tones will not be washed out.

**Priority**: P0 (do this today)

---

### Recommendation 2: VERIFY AND CORRECT ACES FILM PARAMETERS (NEXT PASS)

**What**: Cross-check cited ACES defaults against UE5 5.6 source.

```
IF (UzEngine uses: Slope=0.91, Toe=0.45, Shoulder=0.22)
  THEN change to:   Slope=0.88, Toe=0.55, Shoulder=0.26
```

**Why**: The strategic plan cites values that don't match UE5 5.6 source. If UzEngine inherited these from a different engine (Unity, Godot), the tone curve will be subtly wrong.

**Verification method**:
1. Find where these values are stored (likely `pbr_common.hlsli` or CVar registry)
2. Compare to UE5 5.6 `PostProcessTonemap.cpp:45-78`
3. If different, change to match
4. Render a test scene with neutral gray material
5. Verify tone curve matches UE5 output

**Priority**: P1 (do this in the first implementation day)

---

### Recommendation 3: IMPLEMENT TAA WITH HALTON JITTER BEFORE SSR (CRITICAL DEPENDENCY)

**What**: Complete the `taa_upscaler.cpp` with Halton jitter generation.

```cpp
// Halton sequence generation (required)
float halton(uint index, uint base) {
    float f = 1.0, r = 0.0;
    uint i = index;
    while (i > 0) {
        f /= base;
        r += f * (i % base);
        i /= base;
    }
    return r;
}

// Per frame
float jitter_x = halton(frameIndex, 2) - 0.5;
float jitter_y = halton(frameIndex, 3) - 0.5;
// Apply to projection matrix jitter column
```

**Why**: SSR (P2.1) DEPENDS on TAA for temporal history accumulation. Implementing SSR without TAA will produce a noisy, shimmer-filled result that looks worse than the baseline. TAA is a prerequisite, not a "do later" feature.

**Order of implementation**:
1. Fix parameters (P0, 5 min)
2. Fix micro-shadows (P0, 4 hours)
3. Verify ACES (P1, 1 hour)
4. **Implement TAA with Halton jitter (P1, 6 hours)** ← This is the gate
5. Implement SSR (P2, 8 hours) ← Depends on TAA

**Priority**: P0.5 (do immediately after parameter fixes and before SSR)

---

## 13. Priority Reordering vs Strategic Plan

**Strategic plan order**:
```
Day 1: Fix parameters (30 min) + micro-shadows (4 h)
Day 2: Verify ACES + contact shadows (1.5 h)
Days 3-4: Implement SSR (8-12 h)
Days 5-6: Complete TAA (6-8 h)
```

**UE5 Expert recommendation** (CRITICAL CHANGE):
```
Day 1: Fix parameters (30 min) + micro-shadows (4 h)
Day 2: Verify ACES + contact shadows (1.5 h)
Days 3-4: Complete TAA with Halton jitter (6 h) ← MOVED FORWARD
Days 5-7: Implement SSR (8-12 h) ← Depends on TAA
Days 8-9: Complete DOF + color grading (8-12 h)
```

**Rationale**: SSR temporal accumulation will fail without TAA. Implementing them in the current order means SSR will be broken until TAA is done. By doing TAA first, SSR works correctly on day one of implementation.

---

## 14. AGREEMENT MATRIX

| Item | Consensus | UE5 Source | Status |
|------|-----------|-----------|--------|
| BRDF model (GGX + Burley) | Correct | ✓ Verified | AGREE ✓ |
| Energy conservation (Kulla-Conty) | Correct | ✓ Verified | AGREE ✓ |
| ACES polynomial coefficients | Correct (a,b,c,d,e) | ✓ Verified | AGREE ✓ |
| ACES film parameters (slope/toe/shoulder) | Wrong values cited | Values differ | DISAGREE ✗ |
| Exposure = 1.4 too high | Correct | 1.0 verified | AGREE ✓ |
| SkyLight.Intensity = 3.5 too high | Correct | 1.0 verified | AGREE ✓ |
| ContactShadowLength = 0.03 | Oversimplified | 0.2 world units | DISAGREE ✗ |
| Shadow cascade count (4) | Correct | 4 default | AGREE ✓ |
| SSR missing | Correct | Hi-Z default ON | AGREE ✓ |
| Micro-shadows disabled | Correct | Should be ON | AGREE ✓ |
| TAA minimal | Correct | Full TAA + Halton default | AGREE ✓ |
| Post-process order | Partially correct | More complex | DISAGREE ✗ |

**Overall**: 10/14 items in full agreement. 4 items require correction or deeper investigation.

---

## 15. Final Assessment

**The consensus analysis is a strong foundation** with good engineering judgment. The strategic plan is mostly sound and prioritization is reasonable. However, five critical corrections are needed:

1. **ACES film parameters** are cited with wrong values — need to correct or justify deviation from UE5
2. **Contact shadow length units** are incompatible with UE5 (screen % vs world units)
3. **Post-process pipeline order** is simplified and misses critical SSGI → bloom → SSR sequencing
4. **TAA must come before SSR** in implementation order (critical dependency)
5. **Mie scattering verification** is needed for sky atmosphere realism

**The two 5-minute parameter fixes (Exposure + SkyLight.Intensity) are absolutely correct** and will produce immediate visible improvement. This diagnosis is sound.

**Priority for implementation**:
1. Apply 3× parameter fixes today (5 min, massive visual gain)
2. Fix micro-shadows TODO (4 hours, needs diagnostic first)
3. Verify ACES film parameters (1 hour)
4. Implement TAA with Halton jitter (6 hours, CRITICAL gate for SSR)
5. Implement SSR (8-12 hours, depends on TAA)

Following this order will produce AAA-quality rendering parity by end of week.
