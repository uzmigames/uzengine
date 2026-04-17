# UE5.6 GTAO — Quick Reference for Implementers

**Source:** W:\UE_5.6_Source\Engine\Shaders\Private\PostProcessAmbientOcclusion.usf  
**Analysis Date:** 2026-04-08

---

## 🔴 CRITICAL BUG IN UZENGINE: Screen Radius Formula Missing Depth Division

### The Exact UE5 Formula (line 870-876)

```hlsl
float WorldRadius = GTAOParams[3].y;
float InvTanHalfFov = ScreenSpaceAOParams[3].w;
float FOVScale = AOSceneViewport_Extent.y * InvTanHalfFov;

float WorldRadiusAdj = WorldRadius * FOVScale;
float PixelRadius = max(min(WorldRadiusAdj / ViewSpacePos.z, GTAO_MAX_PIXEL_SCREEN_RADIUS), (float)GTAO_NUMTAPS);
float StepRadius = PixelRadius / ((float)GTAO_NUMTAPS + 1);
```

### Why Flat 0.51-0.60 AO

If UzEngine uses `PixelRadius = WorldRadiusAdj` (without depth division):
- Same pixel radius everywhere → constant horizon search radius
- Constant search radius → uniform AO coverage → flat output values

### Fix

Ensure `PixelRadius = (WorldRadius * FOVScale) / ViewSpacePos.z` where ViewSpacePos.z > 0.

---

## 🎯 Algorithm Overview

| Step | Location | Input | Output |
|------|----------|-------|--------|
| 1. Screen Radius | line 870-876 | WorldRadius, ViewSpacePos.z, InvTanHalfFov | PixelRadius (pixels) |
| 2. Horizon Search | line 707-768 | PixelRadius, ScreenDir, HZB | BestAng (radians) |
| 3. Inner Integral | line 771-795 | BestAng, Normal, ViewDir | AO [0,1] |
| 4. Normalize | line 908-909 | AO, NumAngles | Final AO |
| 5. Fade Distance | line 912-915 | Final AO, SceneDepth | FadedAO |

---

## 📊 Loop Structure

```hlsl
for(uint Angle = 0; Angle < NumAngles; Angle++) {
    // Line 894: Search for horizon angles along one direction
    float2 Angles = SearchForLargestAngleDual_HZB(GTAO_NUMTAPS, TexUV, ScreenDir, StepRadius, Offset, ViewPos, ViewDir, AttenFactor);
    
    // Line 897: Compute occlusion integral for this angle
    Sum += ComputeInnerIntegral(TexUV, Angles, ScreenDir, ViewDir, ViewSpaceNormal, SceneDepth);
    
    // Line 900-902: Rotate ScreenDir for next iteration
    ScreenDir = RotateVector(ScreenDir, CosDeltaAngle, SinDeltaAngle);
    Offset = frac(Offset + 0.617);  // Golden ratio jitter
}

AO = Sum / NumAngles * 2.0 / PI;  // Normalize by angle count and π
```

---

## 🔍 Horizon Search Dual-Direction

```hlsl
for(uint i = 1; i < NumSteps+1; i++) {
    // Step distance grows with iteration
    float2 UVOffset = ScreenDir * max(SearchRadius * (i + InitialOffset), (i+1));
    
    // Sample BOTH +offset and -offset (single loop, double coverage)
    float4 UV2 = BaseUV.xyxy + float4(UVOffset.xy, -UVOffset.xy);
    
    // HZB with mip escalation
    float MipLevel = GTAO_BIASMIPLEVEL;
    if(i==2) MipLevel++;     // step 2: coarser
    if(i>3)  MipLevel += 2;  // step 4+: much coarser
    
    // Reconstruct, compute angles, apply thickness heuristic
}
```

**Key:** Single loop samples **two opposite directions** per iteration. 8 taps in loop = 16 directions.

---

## 🎨 Thickness Heuristic (Thin Wall Fix)

```hlsl
float Thickness = GTAOParams[1].y;  // r.GTAO.ThicknessBlend (default 0.5)

// When new angle is CLOSER than best:
BestAng = (Ang > BestAng) ? Ang : lerp(Ang, BestAng, Thickness);
                                          // ↑ blend, don't replace
```

- `Thickness = 0.0` → always keep best (thick wall assumption)
- `Thickness = 0.5` → blend equally (default)
- `Thickness = 1.0` → always take new (thin wall assumption, noisier)

---

## ∫ Inner Integral Formula (Zafar & Olsson 2014)

```hlsl
// Integrate projected occlusion over a wedge
float AO = (LenProjNormal) * 0.25 * 
    ((Angles.x * SinGamma + CosGamma - cos(2*Angles.x - Gamma)) +
     (Angles.y * SinGamma + CosGamma - cos(2*Angles.y - Gamma)));
```

Steps:
1. Project normal onto plane perpendicular to search axis
2. Compute gamma (normal angle relative to horizon plane)
3. Clamp horizon angles to normal hemisphere
4. Apply double-integral formula (accounts for both horizon angles)
5. Multiply by 0.25 (empirical normalization)

---

## ⚠️ Gotchas

| Gotcha | Impact | Fix |
|--------|--------|-----|
| Missing `/ ViewSpacePos.z` | Flat AO (root cause) | Add depth division |
| ViewSpacePos.z negative | All coordinates inverted | Check coord system |
| ScreenDir not pre-normalized | Wrong step sizes | Verify RandomVector() |
| WorldRadius too small (4cm) with large depth | All samples attenuated out | Increase WorldRadius or reduce max depth |
| Normal perpendicular to view | Black AO | Check normal encoding |
| Mip bias wrong direction | Noisier or blurry AO | Match quality table |
| Reversed-Z not linearized | NaN/Inf in math | Verify ConvertFromDeviceZ() |

---

## 📈 Quality Settings

| Quality | NumTaps | Mip Bias | Effect |
|---------|---------|----------|--------|
| 0 (Very Low) | 4 | 2 | Fast, noisy |
| 1 (Low) | 6 | 1 | Balanced, noisy |
| 2 (Medium) | 8 | 0 | Sweet spot (default) |
| 3 (High) | 12 | 0 | Slower, smooth |
| 4 (Very High) | 20 | 0 | Slow, highest quality |

All clamp max pixel radius to 256px.

---

## 🧪 Verification Checklist

- [ ] PixelRadius varies inversely with depth (log it at 100cm, 500cm, 1000cm)
- [ ] Horizon angles are in [-1, 1] before `acos()`
- [ ] Inner integral output is non-zero (log before normalization)
- [ ] Hi-Z texture is created before GTAO pass
- [ ] ViewSpacePos.z is positive and in correct range
- [ ] Normal encoding matches GBuffer convention (world space or view space?)
- [ ] Attenuation falloff culls samples beyond WorldRadius correctly
- [ ] PSNR vs UE5 reference >40dB on same scene/camera

---

## 📂 File Reference

- **Main shader:** PostProcessAmbientOcclusion.usf
- **Common functions:** PostProcessAmbientOcclusionCommon.ush
- **C++ setup:** PostProcessAmbientOcclusion.cpp (lines 157-400)
- **Shader parameters:** Line 566-571 (GTAOParams array)
- **Screen radius:** Line 870-876
- **Horizon search:** Line 697-768 (with HZB), 648-694 (without HZB)
- **Inner integral:** Line 771-795
- **Main GTAO:** Line 845-922 (GTAOCombinedPSandCS)
- **Quality defines:** Line 538-563

---

## 🚀 Implementation Roadmap

1. **Audit:** Compare UzEngine's screen radius to UE5 line-by-line
2. **Fix:** Add depth division if missing
3. **Validate:** Log horizon angles and inner integral values
4. **Cross-check:** PSNR comparison with UE5 reference
5. **Optimize:** Profile and tune mip bias, thickness blend
6. **Document:** Update rendering architecture docs

**Estimated effort:** 2-4 hours to root cause + fix, 1-2 hours to validate.