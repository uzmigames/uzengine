# UE5.6 GTAO Algorithm — Complete Specification

**Analysis Date:** 2026-04-08  
**Source:** W:\UE_5.6_Source (Engine/Shaders/Private/PostProcessAmbientOcclusion.usf + .cpp)

---

## 1. Screen Radius Derivation (CRITICAL BUG FIX)

**The exact formula UE5 uses (PostProcessAmbientOcclusion.usf:870-876):**

```hlsl
const float WorldRadius = GTAOParams[3].y;	

float InvTanHalfFov = ScreenSpaceAOParams[3].w;
float FOVScale = AOSceneViewport_Extent.y * InvTanHalfFov;

// Get Radius in ScreenSpace (in pixels)
float WorldRadiusAdj = WorldRadius * FOVScale;	
float PixelRadius = max(min(WorldRadiusAdj / ViewSpacePos.z, GTAO_MAX_PIXEL_SCREEN_RADIUS), (float)GTAO_NUMTAPS);
```

**Breaking this down:**

- `FOVScale = viewport_height_px * InvTanHalfFov` — converts world-space radius to screen radius
  - For a 1080p viewport: `FOVScale = 1080 * tan(fov/2)^-1`
  - For a 60° FOV: `InvTanHalfFov ≈ 1.732`, so `FOVScale ≈ 1871`
  
- `WorldRadiusAdj = WorldRadius * FOVScale` — projects world radius into screen space
  - If `WorldRadius = 4.0cm` (GTAO param, NOT the AO radius in UProperties), then `WorldRadiusAdj ≈ 7484`

- `PixelRadius = WorldRadiusAdj / ViewSpacePos.z` — divides by view-space depth to get pixels
  - If `ViewSpacePos.z = 500cm`, then `PixelRadius ≈ 14.97` pixels
  - **This matches UE5 behavior exactly.**

**Why UzEngine gets flat 0.51-0.60 with 400cm radius:**

If UzEngine uses: `PixelRadius = (400 / 400) = 1.0`, then step count is clamped to `GTAO_NUMTAPS` (default 8 for medium quality), producing uniform coverage and flat AO values. The formula **must divide by view-space depth, not world-space radius.**

**C++ setup (PostProcessAmbientOcclusion.cpp):**

```cpp
// GTAOParams[3].y is set from the cvar
// r.GTAO.SearchRadius or similar (not directly visible, inferred from GTAO shader constants)
```

---

## 2. Horizon Search Loop (Dual-Direction)

**Location:** PostProcessAmbientOcclusion.usf:707-768 (`SearchForLargestAngleDual_HZB`)

**Parameters:**
- `NumSteps = GTAO_NUMTAPS` (4, 6, 8, 12, or 20 depending on SHADER_QUALITY)
- `ScreenDir` — randomized per-pixel direction (2D normalized vector)
- `SearchRadius` — pixel radius divided by (NumSteps+1)
- `InitialOffset` — temporal jitter (0-1, varies per frame if TemporalAA enabled)

**Loop structure:**

```hlsl
for(uint i=1; i<NumSteps+1; i++)  // 1-indexed!
{
    float fi = (float)i;
    
    // Step distance grows: radius*(i+offset), at least (i+1) pixels
    float2 UVOffset = ScreenDir * max(SearchRadius * (fi + InitialOffset), (fi+1));
    UVOffset.y *= -1;  // flip Y for screen coords
    
    // Sample both +offset and -offset directions
    float4 UV2 = BaseUV.xyxy + float4(UVOffset.xy, -UVOffset.xy);
    
    // HZB sampling with mip level escalation:
    // i=1: mip 0-1 (depends on GTAO_BIASMIPLEVEL)
    // i=2: mip +1
    // i>3: mip +2 (progressive coarsening for far samples)
    
    // For each direction (positive and negative):
    // 1. Reconstruct view-space position: ScreenToViewPos(UV, depth)
    // 2. Compute vector to sample: V = SamplePos - ViewPos
    // 3. Compute horizon angle: Ang = dot(V, ViewDir) * rsqrt(dot(V,V))
    // 4. Attenuation falloff: saturate(LenSq * AttenFactor)
    //    where AttenFactor = 2.0 / (WorldRadius * WorldRadius)
    // 5. Thickness heuristic: lerp(Ang, PrevAng, Thickness)
    
    BestAng.x = (Ang > BestAng.x) ? Ang : lerp(Ang, BestAng.x, Thickness);
}

// Convert dot product back to angle
BestAng = acosFast(clamp(BestAng, -1.0, 1.0));  // now in radians [0, π]
```

**Key insight:** The loop samples **two opposite directions per iteration** (dual-direction), doubling sample count without extra loop iterations.

---

## 3. Depth Reconstruction

**Formula (PostProcessAmbientOcclusionCommon.ush:54-57):**

```hlsl
float3 ReconstructCSPos(float SceneDepth, float2 ScreenPos)
{
    return float3(ScreenPos * SceneDepth, SceneDepth);
}
```

**Context:**
- `SceneDepth` is **reversed-Z** (close=1, far=0), converted from `DeviceZ` via `ConvertFromDeviceZ()`.
- `ScreenPos` is NDC space (-1 to 1), pre-scaled by `InvTanHalfFov` to account for FOV.
- The result is **view-space position** (camera-relative, Z-up).

**HZB access (CommonOcclusionCommon.ush:34-38):**

```hlsl
float GetHZBDepth(float2 ScreenPos, float MipLevel)
{
    float2 HZBUV = ApplyScreenTransform(ScreenPos, ScreenPosToHZBUVScaleBias);
    float HZBDepth = Texture2DSampleLevel(HZBTexture, HZBSampler, HZBUV, MipLevel).r;
    return ConvertFromDeviceZ(HZBDepth);
}
```

**Mip level progression (line 723-729):**

```hlsl
MipLevel = GTAO_BIASMIPLEVEL;  // 0, 1, or 2 depending on quality
if(i==2) MipLevel++;           // step 2 → mip+1
if(i>3)  MipLevel+=2;          // steps 4+ → mip+2 (coarser = faster, lower quality)
```

---

## 4. Horizon Angle Integral (ComputeInnerIntegral)

**Location:** PostProcessAmbientOcclusion.usf:771-795

**Input:** Two horizon angles (left, right) in radians, normal, view direction.

**Algorithm:**

```hlsl
float ComputeInnerIntegral(float2 UV, float2 Angles, float2 ScreenDir, float3 ViewDir, float3 ViewSpaceNormal, float SceneDepth)
{
    // Project normal onto the plane perpendicular to the search axis
    float3 PlaneNormal = normalize(cross(float3(ScreenDir.xy, 0), ViewDir));
    float3 Perp = cross(ViewDir, PlaneNormal);
    float3 ProjNormal = ViewSpaceNormal - PlaneNormal * dot(ViewSpaceNormal, PlaneNormal);
    
    float LenProjNormal = length(ProjNormal) + 0.000001f;
    float RecipMag = 1.0f / LenProjNormal;
    
    float CosAng = dot(ProjNormal, Perp) * RecipMag;
    float Gamma = acosFast(CosAng) - PI_HALF;       // clamp normal to hemisphere
    float CosGamma = dot(ProjNormal, ViewDir) * RecipMag;
    float SinGamma = CosAng * -2.0f;
    
    // Clamp angles to normal hemisphere (avoid occlusion from behind the surface)
    Angles.x = Gamma + max(-Angles.x - Gamma, -PI_HALF);
    Angles.y = Gamma + min(Angles.y - Gamma, PI_HALF);
    
    // Integrate: 0.25 * ((Θ₁*sin(γ) + cos(γ) - cos(2Θ₁ - γ)) + (Θ₂*sin(γ) + cos(γ) - cos(2Θ₂ - γ)))
    float AO = (LenProjNormal) * 0.25 * 
        ((Angles.x * SinGamma + CosGamma - cos(2.0 * Angles.x - Gamma)) +
         (Angles.y * SinGamma + CosGamma - cos(2.0 * Angles.y - Gamma)));
    
    return AO;
}
```

**This is the Zafar & Olsson 2014 formula** (projected occlusion integral over a wedge). The `0.25` factor normalizes the result.

---

## 5. Thickness Heuristic (False Occlusion Fix)

**Location:** PostProcessAmbientOcclusion.usf:676, 687, 705, 746, 760

```hlsl
float Thickness = GTAOParams[1].y;  // Read from C++ (r.GTAO.ThicknessBlend cvar, default 0.5)

// When updating horizon angle with new sample:
BestAng = (Ang > BestAng) ? Ang : lerp(Ang, BestAng, Thickness);
```

**Mechanism:**

- If new angle is **larger** (farther horizon), take it immediately: `Ang > BestAng`.
- Otherwise, **blend** between the new (closer) angle and the best (farther) angle using `Thickness`.
  - `Thickness = 0.0` → always keep best (no blending, thicker walls)
  - `Thickness = 0.5` → blend equally (default, good compromise)
  - `Thickness = 1.0` → always take new (thin walls, noisy)

**Why this works:** Prevents thin walls (false occlusion) by assuming near samples are part of the same surface.

**CVar setup (PostProcessAmbientOcclusion.cpp:113-120):**

```cpp
static TAutoConsoleVariable<float> CVarGTAOThicknessBlend(
    TEXT("r.GTAO.ThicknessBlend"),
    0.5f,
    TEXT("A heuristic to bias occlusion for thin or thick objects. \n")
    TEXT("0  : Off \n")
    TEXT(">0 : On - Bigger values lead to reduced occlusion \n")
    TEXT("0.5: On (default)\n")
);
```

---

## 6. Power & Intensity Application

**Location:** PostProcessAmbientOcclusion.usf:897-909 (GTAO) and 397 (SSAO)

**GTAO:**

```hlsl
float AO = Sum / ((float)NumAngles);
AO *= 2.0/PI;  // Normalize by angle count and π (mathematical normalization)

// Fade out based on distance
float Mul = ScreenSpaceAOParams[4].x;  // 1 / FadeRadius
float Add = ScreenSpaceAOParams[4].y;  // -(FadeDistance - FadeRadius) / FadeRadius
AO = lerp(AO, 1, saturate(SceneDepth * Mul + Add));

OutColor = AO;  // Range [0, 1], 1 = no occlusion
```

**SSAO (older path, line 397):**

```hlsl
OutColor.r = 1 - (1 - pow(abs(OutColor.r), AmbientOcclusionPower)) * AmbientOcclusionIntensity;
```

**Difference:** GTAO outputs raw AO; SSAO applies `pow()` and `intensity` blend.

**C++ parameters (PostProcessAmbientOcclusion.cpp:396):**

```cpp
Result.ScreenSpaceAOParams[0] = FVector4f(
    Settings.AmbientOcclusionPower,      // Power for SSAO only
    Settings.AmbientOcclusionBias / 1000.0f,
    InvAmbientOcclusionDistance,
    Settings.AmbientOcclusionIntensity   // Intensity for SSAO only
);
```

---

## 7. Hi-Z Dependency

**YES, GTAO requires Hi-Z.**

- Line 731-732: `GetHZBDepth(UV2.xy, MipLevel)` 
- Line 697: Entire `SearchForLargestAngleDual_HZB` function built around HZB sampling.
- Non-HZB fallback: `SearchForLargestAngleDual()` (line 648) exists but is less efficient (samples full-res depth).

**HZB creation:** Automatic, built before GTAO pass. See `ComputeHZB()` in RenderCore.

---

## 8. Critical Gotchas & Known Issues

### 8.1 Reversed-Z Convention
- UE5 uses **reversed-Z** (closer objects = 1, farther = 0).
- `ConvertFromDeviceZ()` linearizes it back to linear view-space depth.
- **UzEngine gotcha:** If you linearize twice, horizon angles become inverted.

### 8.2 ViewSpacePos.z Must Be Positive
- Line 875: `PixelRadius = WorldRadiusAdj / ViewSpacePos.z`
- If `ViewSpacePos.z` is treated as negative (Y-up vs Z-up mismatch), all radii become negative.
- **Fix:** Verify your coordinate system in `ReconstructCSPos()`.

### 8.3 ScreenDir Normalization
- Line 874: `ScreenDir` is already normalized (from `GetRandomVector()`).
- Line 894: Scaled by `View.BufferSizeAndInvSize.zw` (UV-space scale).
- If you scale again, step sizes become wrong.

### 8.4 Mip Level Escalation Logic is Subtle
- Line 716-729: Mip level depends on step iteration **i**, not sample count.
- Lower mips = faster but noisier. Higher mips = smoother but misses fine detail.
- Default progression: mip0 → mip1 (step 2) → mip3+ (step 4+).

### 8.5 AttenFactor Falloff
- Line 740: `FallOff = saturate(LenSq * AttenFactor)` where `AttenFactor = 2.0 / (WorldRadius²)`
- Samples beyond `WorldRadius` have `FallOff > 1.0` and get ignored.
- **If WorldRadius is 4cm but you're sampling 400cm away, all samples are attenuated to 1.0 (no contribution).**

### 8.6 Inner Integral Assumes Normal Validity
- Line 778: `LenProjNormal + 0.000001f` prevents division by zero, but doesn't handle degenerate normals.
- If normal is perpendicular to view direction, `ProjNormal ≈ 0` and AO value becomes very small (black).

### 8.7 Temporal Jitter Direction is Per-Angle
- Line 903: `Offset = frac(Offset + 0.617)` — offset changes per search direction.
- This prevents banding when TemporalAA is disabled.

---

## 9. Parameter Summary

### Shader Constants (GTAOParams buffer)

```hlsl
// [0] - { cos(TemporalAngle), sin(TemporalAngle), TemporalOffset, FrameTemporalOffset}
// [1] - { FrameNumber, Thicknessblend, unused, unused}
// [2] - { TargetSizeX, TargetSizeY, 1.0/TargetSizeX, 1.0f/TargetSizeY}
// [3] - { FallOffStart, WorldRadius, unused, unused}
// [4] - { Temporal Blend Weight, NumAngles, SinDeltaAngle, CosDeltaAngle}
```

### ScreenSpaceAOParams (shared with SSAO)

```hlsl
// [0] - { AmbientOcclusionPower, AmbientOcclusionBias/1000, 1/AmbientOcclusionDistance, AmbientOcclusionIntensity}
// [1] - { ViewportUVToRandomUV.x, ViewportUVToRandomUV.y, AORadiusInShader, Ratio}
// [2] - { ScaleFactor, AmbientOcclusionMipThreshold/ScaleFactor, ScaleRadiusInWorldSpace, AmbientOcclusionMipBlend}
// [3] - { TemporalAARandomOffset.x, TemporalAARandomOffset.y, StaticFraction, InvTanHalfFov}
// [4] - { InvFadeRadius, -(FadeDistance-FadeRadius)/InvFadeRadius, HzbStepMipLevelFactor, unused}
```

---

## 10. Quality Settings

| SHADER_QUALITY | GTAO_NUMTAPS | GTAO_BIASMIPLEVEL | Max Pixel Screen Radius |
|---|---|---|---|
| 0 (very low) | 4 | 2 | 256 |
| 1 (low) | 6 | 1 | 256 |
| 2 (medium) | 8 | 0 | 256 |
| 3 (high) | 12 | 0 | 256 |
| 4 (very high) | 20 | 0 | 256 |

---

## Summary: Fix for UzEngine's Flat AO

**Root cause:** Screen radius not divided by view-space depth.

**Correct formula:**
```
FOVScale = ViewportHeightPixels * InvTanHalfFov
WorldRadiusAdj = WorldRadius * FOVScale
PixelRadius = WorldRadiusAdj / ViewSpacePos.z  // ← CRITICAL: divide by depth
StepRadius = PixelRadius / (NumTaps + 1)
```

**Verify:**
1. Is `ViewSpacePos.z` always positive and > 0?
2. Is `WorldRadius` set to ~4cm (UE5 default)?
3. Is `PixelRadius` varying per-pixel based on depth?

If all three are yes, the flat 0.51-0.60 value indicates an issue in the inner integral or angle computation, not the search radius.
