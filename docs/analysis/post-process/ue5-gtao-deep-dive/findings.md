# Findings — UE5.6 GTAO Algorithm Analysis

## F-001 — Screen Radius Must Divide by View-Space Depth (Root Cause of Flat AO)

- **Evidence**: PostProcessAmbientOcclusion.usf:870-876
- **Impact**: HIGH — this is the root cause of UzEngine's flat 0.51-0.60 AO range
- **Confidence**: HIGH — exact code from UE5.6 source
- **Discovered by**: manual (code audit)

The exact UE5 formula:
```hlsl
float FOVScale = AOSceneViewport_Extent.y * InvTanHalfFov;
float WorldRadiusAdj = WorldRadius * FOVScale;
float PixelRadius = max(min(WorldRadiusAdj / ViewSpacePos.z, GTAO_MAX_PIXEL_SCREEN_RADIUS), (float)GTAO_NUMTAPS);
```

**Critical step:** `WorldRadiusAdj / ViewSpacePos.z` — dividing by view-space depth converts world-space radius to screen-space pixels.

If UzEngine omits this division or uses world-space radius instead, `PixelRadius` becomes constant (independent of camera distance), causing all pixels to sample the same relative horizon angles → flat AO output.

**Verification test:** Log `PixelRadius` at depth=100cm, 500cm, 1000cm. If constant, the division is missing.

---

## F-002 — Dual-Direction Horizon Search (Efficiency Pattern)

- **Evidence**: PostProcessAmbientOcclusion.usf:707-768 (SearchForLargestAngleDual_HZB)
- **Impact**: MEDIUM — architectural pattern for implementers
- **Confidence**: HIGH
- **Discovered by**: manual

Single loop with samples in both +ScreenDir and -ScreenDir:

```hlsl
for(uint i=1; i<NumSteps+1; i++) {
    float2 UVOffset = ScreenDir * max(SearchRadius * (fi + InitialOffset), (fi+1));
    float4 UV2 = BaseUV.xyxy + float4(UVOffset.xy, -UVOffset.xy);  // two directions
    
    SceneDepths.x = GetHZBDepth(UV2.xy, MipLevel);   // +offset
    SceneDepths.y = GetHZBDepth(UV2.zw, MipLevel);   // -offset
}
```

Effectively 2× sample count (8 taps in loop = 16 directions sampled).

---

## F-003 — HZB Mip Escalation is Subtle

- **Evidence**: PostProcessAmbientOcclusion.usf:723-729
- **Impact**: MEDIUM — affects quality/performance tradeoff
- **Confidence**: HIGH
- **Discovered by**: manual

Mip level grows per-step iteration:
```hlsl
MipLevel = GTAO_BIASMIPLEVEL;  // 0, 1, or 2
if(i==2) MipLevel++;            // step 2 → coarser
if(i>3)  MipLevel+=2;           // step 4+ → much coarser
```

**Effect:** Near samples (fine detail) use fine mips; far samples (coarse) use coarse mips. Balances quality vs performance.

---

## F-004 — Thickness Heuristic Prevents Thin Wall False Occlusion

- **Evidence**: PostProcessAmbientOcclusion.usf:676, 687, 705, 746, 760
- **Impact**: MEDIUM — visual correctness
- **Confidence**: HIGH
- **Discovered by**: manual

When new sample has closer horizon:
```hlsl
BestAng = (Ang > BestAng) ? Ang : lerp(Ang, BestAng, Thickness);
```

Where `Thickness = GTAOParams[1].y` (r.GTAO.ThicknessBlend, default 0.5).

Blend instead of replace to avoid thin walls blocking everything. CVar-adjustable per-project.

---

## F-005 — Horizon Angle Integral is Zafar & Olsson 2014 Formula

- **Evidence**: PostProcessAmbientOcclusion.usf:771-795 (ComputeInnerIntegral)
- **Impact**: MEDIUM — mathematical correctness
- **Confidence**: HIGH
- **Discovered by**: manual

The formula integrates projected occlusion:
```hlsl
float AO = (LenProjNormal) * 0.25 * 
    ((Angles.x * SinGamma + CosGamma - cos(2.0 * Angles.x - Gamma)) +
     (Angles.y * SinGamma + CosGamma - cos(2.0 * Angles.y - Gamma)));
```

Accounts for:
1. Normal projection onto search plane
2. Clamping horizon angles to normal hemisphere
3. Integration over wedge

The `0.25` factor is empirical normalization.

---

## F-006 — Attenuation Falloff Completely Culls Distant Samples

- **Evidence**: PostProcessAmbientOcclusion.usf:740, 754
- **Impact**: HIGH — if WorldRadius is too small, no samples contribute
- **Confidence**: HIGH
- **Discovered by**: manual

```hlsl
float AttenFactor = 2.0 / (WorldRadius * WorldRadius);
float FallOff = saturate(LenSq * AttenFactor);

if(FallOff < 1.0) {
    // Apply update
} else {
    // Skip: beyond WorldRadius
}
```

Any sample with `|V| > WorldRadius` has `FallOff ≥ 1.0` and is skipped entirely.

**Gotcha:** If `WorldRadius = 4cm` but camera is 500cm away, **all samples are beyond radius → no AO**.

---

## F-007 — Hi-Z is Mandatory (Not Optional)

- **Evidence**: PostProcessAmbientOcclusion.usf:731-732, entire SearchForLargestAngleDual_HZB function
- **Impact**: HIGH — architecture requirement
- **Confidence**: HIGH
- **Discovered by**: manual

All GTAO sampling goes through HZB:
```hlsl
SceneDepths.x = GetHZBDepth(UV2.xy, MipLevel);
```

Non-HZB fallback (SearchForLargestAngleDual) exists but is slower. HZB must be pre-computed before GTAO pass.

---

## F-008 — Depth Reconstruction Assumes Reversed-Z Convention

- **Evidence**: PostProcessAmbientOcclusionCommon.ush:54-57
- **Impact**: HIGH — coordinate system correctness
- **Confidence**: HIGH
- **Discovered by**: manual

```hlsl
float3 ReconstructCSPos(float SceneDepth, float2 ScreenPos) {
    return float3(ScreenPos * SceneDepth, SceneDepth);
}
```

Assumes:
- `SceneDepth` is linearized reversed-Z (close=large, far=small)
- Result is view-space (Z-forward, right-handed in UE context)

If UzEngine uses different convention, all coordinates are wrong.

---

## F-009 — Temporal Jitter Uses Interleaved Gradient Noise

- **Evidence**: PostProcessAmbientOcclusion.usf:799-809, 803-809
- **Impact**: LOW — visual quality/temporal coherence
- **Confidence**: HIGH
- **Discovered by**: manual

```hlsl
float InterleavedGradientNoise(float2 iPos) {
    return frac(52.9829189f * frac((iPos.x * 0.06711056) + (iPos.y * 0.00583715)));
}

float Offset = (1.0/4.0) * ((iPos.y - iPos.x) & 3);  // per-pixel offset
```

Blue noise dithering for spatial/temporal anti-aliasing.

---

## F-010 — Quality Settings Define Tap Count and Mip Bias

- **Evidence**: PostProcessAmbientOcclusion.usf:538-563
- **Impact**: MEDIUM — performance tuning
- **Confidence**: HIGH
- **Discovered by**: manual

Quality mapping:
- Very Low: 4 taps, mip bias 2
- Low: 6 taps, mip bias 1
- Medium: 8 taps, mip bias 0
- High: 12 taps, mip bias 0
- Very High: 20 taps, mip bias 0

All clamp max pixel screen radius to 256px.
