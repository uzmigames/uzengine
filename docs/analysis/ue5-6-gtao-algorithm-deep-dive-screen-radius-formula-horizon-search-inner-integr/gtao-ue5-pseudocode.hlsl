// UE5.6 GTAO Algorithm — Complete Pseudocode with Line References
// Source: PostProcessAmbientOcclusion.usf (Engine/Shaders/Private/)

// ============================================================================
// MAIN ENTRY POINT: GTAOCombinedPSandCS (line 845)
// ============================================================================

void GTAO_Main(float2 UV, uint2 PixelPos, out float AO) {
    // Setup
    float DeviceZ = LookupDeviceZ(UV);
    float SceneDepth = ConvertFromDeviceZ(DeviceZ);  // Linearize reversed-Z

    float3 ViewSpacePos = ScreenToViewPos(UV, SceneDepth);      // View-space position
    float3 ViewSpaceNormal = GetNormal(UV, ViewSpacePos);       // From GBuffer or depth
    float3 ViewDir = normalize(-ViewSpacePos);                  // Direction to camera

    // ────────────────────────────────────────────────────────────────
    // STEP 1: SCREEN RADIUS CALCULATION (line 870-876) ← ROOT CAUSE BUG
    // ────────────────────────────────────────────────────────────────

    const float WorldRadius = GTAOParams[3].y;                  // e.g., 4.0 cm
    float InvTanHalfFov = ScreenSpaceAOParams[3].w;             // FOV parameterization
    float FOVScale = ViewportHeightPixels * InvTanHalfFov;      // e.g., 1080 * 0.5773 ≈ 623

    float WorldRadiusAdj = WorldRadius * FOVScale;              // e.g., 4.0 * 623 = 2492

    // ⚠️ CRITICAL: Must divide by view-space depth
    float PixelRadius = WorldRadiusAdj / ViewSpacePos.z;        // e.g., 2492 / 500 = 4.98 pixels
    PixelRadius = max(min(PixelRadius, GTAO_MAX_PIXEL_SCREEN_RADIUS), (float)GTAO_NUMTAPS);

    float StepRadius = PixelRadius / ((float)GTAO_NUMTAPS + 1); // e.g., 4.98 / 9 ≈ 0.55 pixels
    float AttenFactor = 2.0 / (WorldRadius * WorldRadius);      // Falloff: 2.0 / 16 = 0.125

    // Get random direction and temporal offset
    float3 RandomAndOffset = GetRandomVector(PixelPos);         // Interleaved gradient noise
    float2 RandomVec = RandomAndOffset.xy;
    float Offset = RandomAndOffset.z;

    float Sum = 0.0;

    // ────────────────────────────────────────────────────────────────
    // STEP 2: MULTI-ANGLE HORIZON SEARCH (line 892-904)
    // ────────────────────────────────────────────────────────────────

    uint NumAngles = (uint)GTAOParams[4].y;                     // e.g., 2 angles
    float SinDeltaAngle = GTAOParams[4].z;
    float CosDeltaAngle = GTAOParams[4].w;

    float2 ScreenDir = RandomVec;                               // Normalized direction

    for(uint Angle = 0; Angle < NumAngles; Angle++) {

        // ────────────────────────────────────────────────────────────
        // SUBSTEP 2A: DUAL-DIRECTION HORIZON SEARCH (line 894-895)
        // ────────────────────────────────────────────────────────────

        float2 Angles = SearchForLargestAngleDual_HZB(
            GTAO_NUMTAPS,                           // 8 taps (medium quality)
            UV,                                     // Base screen coordinate
            ScreenDir * PixelSize,                  // Direction in UV space
            StepRadius,                             // ~0.55 pixels
            Offset,                                 // Temporal jitter
            ViewSpacePos,                           // Origin
            ViewDir,                                // View direction
            AttenFactor                             // 2.0/R²
        );
        // Returns: float2(LeftHorizonAngle, RightHorizonAngle) in radians

        // ────────────────────────────────────────────────────────────
        // SUBSTEP 2B: INNER INTEGRAL (line 897)
        // ────────────────────────────────────────────────────────────

        float AngularAO = ComputeInnerIntegral(
            UV,
            Angles,
            ScreenDir,
            ViewDir,
            ViewSpaceNormal,
            SceneDepth
        );
        // Returns: AO contribution for this angle (typically 0.0-0.5)

        Sum += AngularAO;

        // ────────────────────────────────────────────────────────────
        // SUBSTEP 2C: ROTATE FOR NEXT ANGLE (line 900-902)
        // ────────────────────────────────────────────────────────────

        // Rotate ScreenDir by (360° / NumAngles)
        float2 TempScreenDir = ScreenDir.xy;
        ScreenDir.x = (TempScreenDir.x * CosDeltaAngle) + (TempScreenDir.y * -SinDeltaAngle);
        ScreenDir.y = (TempScreenDir.x * SinDeltaAngle) + (TempScreenDir.y * CosDeltaAngle);

        Offset = frac(Offset + 0.617);  // Golden ratio jitter for next angle
    }

    // ────────────────────────────────────────────────────────────────
    // STEP 3: NORMALIZE & APPLY DISTANCE FADE (line 906-915)
    // ────────────────────────────────────────────────────────────────

    float AO = Sum / ((float)NumAngles);                        // Average over angles
    AO *= 2.0 / PI;                                             // Normalize by π

    // Fade out in distance
    float FadeMul = ScreenSpaceAOParams[4].x;                   // 1.0 / FadeRadius
    float FadeAdd = ScreenSpaceAOParams[4].y;                   // -(FadeDistance - FadeRadius) / FadeRadius
    AO = lerp(AO, 1.0, saturate(SceneDepth * FadeMul + FadeAdd));

    return AO;  // Final output: [0, 1], 1 = no occlusion
}

// ============================================================================
// HORIZON SEARCH WITH HZB (line 697-768)
// ============================================================================

float2 SearchForLargestAngleDual_HZB(
    uint NumSteps,          // GTAO_NUMTAPS (e.g., 8)
    float2 BaseUV,          // Screen coordinate
    float2 ScreenDir,       // Normalized direction (scaled to UV space)
    float SearchRadius,     // StepRadius (~0.55 pixels)
    float InitialOffset,    // Temporal jitter
    float3 ViewPos,         // Camera position (0, 0, 0)
    float3 ViewDir,         // Normalized view direction
    float AttenFactor       // 2.0 / (WorldRadius²)
) {
    float2 BestAng = float2(-1, -1);        // Initialize to "no horizon found"
    float Thickness = GTAOParams[1].y;      // Thickness blend (default 0.5)

    UNROLL
    for(uint i = 1; i < NumSteps + 1; i++) {
        float fi = (float)i;

        // ────────────────────────────────────────────────────────────
        // OFFSET: Grows with iteration
        // ────────────────────────────────────────────────────────────
        float2 UVOffset = ScreenDir * max(SearchRadius * (fi + InitialOffset), (fi + 1));
        UVOffset.y *= -1;  // Flip Y for screen coordinates

        // ────────────────────────────────────────────────────────────
        // HZB MIP SELECTION: Higher mips for distant steps
        // ────────────────────────────────────────────────────────────
        float MipLevel = GTAO_BIASMIPLEVEL;  // 0, 1, or 2 (quality-dependent)
        if(i == 2) MipLevel++;               // Step 2: coarser
        if(i > 3)  MipLevel += 2;            // Steps 4+: much coarser

        // ────────────────────────────────────────────────────────────
        // DUAL DIRECTION: Sample both +offset and -offset
        // ────────────────────────────────────────────────────────────
        float4 UV2 = BaseUV.xyxy + float4(UVOffset.xy, -UVOffset.xy);

        float SceneDepthL = GetHZBDepth(UV2.xy, MipLevel);  // Left direction
        float SceneDepthR = GetHZBDepth(UV2.zw, MipLevel);  // Right direction

        // ────────────────────────────────────────────────────────────
        // PROCESS LEFT DIRECTION
        // ────────────────────────────────────────────────────────────
        float3 SamplePosL = ScreenToViewPos(UV2.xy, SceneDepthL);
        float3 V_L = SamplePosL - ViewPos;                   // Vector to sample
        float LenSq_L = dot(V_L, V_L);
        float Ang_L = dot(V_L, ViewDir) * rsqrt(LenSq_L + 0.0001);  // Horizon angle (cos)

        float FallOff_L = saturate(LenSq_L * AttenFactor);   // Attenuate distant samples
        if(FallOff_L < 1.0) {
            Ang_L = lerp(Ang_L, BestAng.x, FallOff_L);       // Apply falloff

            // ⚠️ THICKNESS HEURISTIC: Blend instead of replace for closer angles
            if(Ang_L > BestAng.x) {
                BestAng.x = Ang_L;  // New horizon farther, take it
            } else {
                // New angle closer: blend with best
                BestAng.x = lerp(Ang_L, BestAng.x, Thickness);
            }
        }

        // ────────────────────────────────────────────────────────────
        // PROCESS RIGHT DIRECTION (same logic)
        // ────────────────────────────────────────────────────────────
        float3 SamplePosR = ScreenToViewPos(UV2.zw, SceneDepthR);
        float3 V_R = SamplePosR - ViewPos;
        float LenSq_R = dot(V_R, V_R);
        float Ang_R = dot(V_R, ViewDir) * rsqrt(LenSq_R + 0.0001);

        float FallOff_R = saturate(LenSq_R * AttenFactor);
        if(FallOff_R < 1.0) {
            Ang_R = lerp(Ang_R, BestAng.y, FallOff_R);

            if(Ang_R > BestAng.y) {
                BestAng.y = Ang_R;
            } else {
                BestAng.y = lerp(Ang_R, BestAng.y, Thickness);
            }
        }
    }

    // Convert dot products (cos values) back to angles
    BestAng.x = acosFast(clamp(BestAng.x, -1.0, 1.0));
    BestAng.y = acosFast(clamp(BestAng.y, -1.0, 1.0));

    return BestAng;  // Horizon angles in radians
}

// ============================================================================
// INNER INTEGRAL: Zafar & Olsson 2014 (line 771-795)
// ============================================================================

float ComputeInnerIntegral(
    float2 UV,
    float2 Angles,              // [LeftHorizonAngle, RightHorizonAngle] in radians
    float2 ScreenDir,           // Search direction
    float3 ViewDir,             // Normalized view direction
    float3 ViewSpaceNormal,     // Surface normal in view space
    float SceneDepth            // Linear depth
) {
    // ────────────────────────────────────────────────────────────────
    // PROJECT NORMAL ONTO SEARCH PLANE
    // ────────────────────────────────────────────────────────────────

    float3 PlaneNormal = normalize(cross(float3(ScreenDir.xy, 0), ViewDir));
    float3 Perp = cross(ViewDir, PlaneNormal);
    float3 ProjNormal = ViewSpaceNormal - PlaneNormal * dot(ViewSpaceNormal, PlaneNormal);

    float LenProjNormal = length(ProjNormal) + 0.000001;  // Avoid division by zero
    float RecipMag = 1.0 / LenProjNormal;

    // ────────────────────────────────────────────────────────────────
    // COMPUTE GAMMA: Normal angle relative to horizon plane
    // ────────────────────────────────────────────────────────────────

    float CosAng = dot(ProjNormal, Perp) * RecipMag;
    float Gamma = acosFast(CosAng) - PI_HALF;
    float CosGamma = dot(ProjNormal, ViewDir) * RecipMag;
    float SinGamma = CosAng * -2.0;

    // ────────────────────────────────────────────────────────────────
    // CLAMP ANGLES TO NORMAL HEMISPHERE
    // ────────────────────────────────────────────────────────────────

    Angles.x = Gamma + max(-Angles.x - Gamma, -PI_HALF);      // Left angle
    Angles.y = Gamma + min(Angles.y - Gamma, PI_HALF);        // Right angle

    // ────────────────────────────────────────────────────────────────
    // INTEGRATE: Projected occlusion over horizon wedge
    // ────────────────────────────────────────────────────────────────
    // Formula: ∫∫ cos(θ) dθ from θ₁ to θ₂ over wedge
    // Simplified to double-integral form (left + right)

    float AO = (LenProjNormal) * 0.25 *
        ((Angles.x * SinGamma + CosGamma - cos(2.0 * Angles.x - Gamma)) +
         (Angles.y * SinGamma + CosGamma - cos(2.0 * Angles.y - Gamma)));

    return max(AO, 0.0);  // Clamp to [0, ∞)
}

// ============================================================================
// KEY MACROS & CONSTANTS
// ============================================================================

#define GTAO_NUMTAPS 8                      // Horizon search steps (quality-dependent)
#define GTAO_BIASMIPLEVEL 0                 // HZB mip offset (quality-dependent)
#define GTAO_MAX_PIXEL_SCREEN_RADIUS 256.0  // Max pixel radius clamp (constant)
#define PI_HALF (PI * 0.5)

// ============================================================================
// QUALITY SETTINGS TABLE
// ============================================================================

/*
SHADER_QUALITY | GTAO_NUMTAPS | GTAO_BIASMIPLEVEL | Effect
     0          |      4       |       2           | Very Low: Fast, noisy
     1          |      6       |       1           | Low: Noisy
     2          |      8       |       0           | Medium: Balanced (default)
     3          |     12       |       0           | High: Smooth
     4          |     20       |       0           | Very High: Smooth, slow
*/

// ============================================================================
// VERIFICATION CHECKLIST FOR IMPLEMENTERS
// ============================================================================

/*
1. Screen Radius (line 870-876):
   ✓ ViewSpacePos.z is positive and in expected range (e.g., 100-2000 cm)
   ✓ PixelRadius scales inversely with depth (larger at close range)
   ✓ PixelRadius clamped to [GTAO_NUMTAPS, GTAO_MAX_PIXEL_SCREEN_RADIUS]

2. Horizon Search (line 697-768):
   ✓ Angles are in [-1, 1] before acos() conversion
   ✓ HZB mip level escalates per iteration (0→1→3)
   ✓ Thickness blend preserves best angle while preventing false occlusion

3. Inner Integral (line 771-795):
   ✓ LenProjNormal is non-zero (valid normal projection)
   ✓ AO output is non-negative before normalization
   ✓ Clamping to hemisphere prevents back-face occlusion

4. Final Output:
   ✓ AO in [0, 1] after normalization and fade
   ✓ PSNR > 40dB vs UE5 reference on same scene
*/
