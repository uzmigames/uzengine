<!-- doc-meta
title: Deep Audit — TAA & SSR vs UE5 5.6
author: shader-engineer (opus)
date: 2026-03-18
status: complete
-->

# Deep Audit: TAA & SSR Shaders vs UE5 5.6

Line-by-line comparison of UzEngine's TAA and SSR compute shaders against
`W:\UE_5.6_Source\Engine\Shaders\Private\TemporalAA.usf` and
`W:\UE_5.6_Source\Engine\Shaders\Private\SSRT\SSRTRayCast.ush` /
`SSRTReflections.usf`.

---

## TAA Issues

### ISSUE 1: ComputeMotionVector is a stub — returns zero

**Description:** `ComputeMotionVector()` at line 116-126 of `taa_resolve_cs.hlsl`
always returns `float2(0,0)`. This means TAA has NO reprojection for camera
motion or object motion. The shader comment even contains a `TODO`. For any
camera movement, history lookup uses the wrong UV, causing ghosting and
disocclusion artifacts.

**UE5:** `TemporalAA.usf:2087-2121` — Uses `View.ClipToPrevClip` matrix to
compute camera reprojection, then overlays per-pixel velocity from
`GBufferVelocityTexture` for dynamic objects:

```hlsl
// UE5 TemporalAA.usf:2089-2091
float4 ThisClip = float4( PosN.xy, PosN.z, 1 );
float4 PrevClip = mul( ThisClip, View.ClipToPrevClip );
float2 PrevScreen = PrevClip.xy / PrevClip.w;
```

Then at line 2098-2103, it reads the velocity buffer:
```hlsl
ENCODED_VELOCITY_TYPE EncodedVelocity = SampleVelocityTexture(...);
bool DynamicN = EncodedVelocity.x > 0.0;
if(DynamicN) { BackN = DecodeVelocityFromTexture(EncodedVelocity).xy; }
```

**UzEngine:** `taa_resolve_cs.hlsl:116-126` — Hard-coded `return float2(0,0)`.

**FIX:** At minimum, implement depth-based camera reprojection using
`prevViewProj` (which the spec says should be in PerFrameCB). This requires:
1. Adding `prev_view_proj` to PerFrameCB
2. Computing NDC -> world -> prev clip -> prev NDC -> motion vector
3. When velocity buffer is available, read it and override camera motion for
   dynamic pixels.

```hlsl
float2 ComputeMotionVector(int2 pixelCoord, float2 uv)
{
    float depth = DepthTex.Load(int3(pixelCoord, 0)).x;
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float4 clipPos = float4(ndc, depth, 1.0);
    float4 worldPos = mul(clipPos, invViewProj);
    worldPos /= worldPos.w;
    float4 prevClip = mul(worldPos, prevViewProj);
    float2 prevNdc = prevClip.xy / prevClip.w;
    float2 prevUV = prevNdc * float2(0.5, -0.5) + 0.5;
    return uv - prevUV;
}
```

**IMPACT: HIGH** — TAA is fundamentally broken for any camera motion.

---

### ISSUE 2: Missing bicubic (Catmull-Rom) history sampling

**Description:** UzEngine uses simple bilinear sampling for the history buffer
(line 320). UE5 uses 5-tap Catmull-Rom bicubic interpolation for the main TAA
pass (`AA_BICUBIC=1`), which significantly reduces blurriness.

**UE5:** `TemporalAA.usf:1840-1880` — `AA_BICUBIC` path uses
`GetBicubic2DCatmullRomSamples()` from `TextureSampling.ush` to compute 5-9
bilinear taps that approximate the bicubic kernel.

**UzEngine:** `taa_resolve_cs.hlsl:320` — Single bilinear sample:
```hlsl
float3 rawHistory = HistoryTex.SampleLevel(LinearClampSampler, clampedHistoryUV, 0).rgb;
```

**FIX:** Implement the Catmull-Rom 5-tap filter. This is a well-known technique:
compute 5 bilinear samples with specific offsets and weights that reconstruct
the bicubic kernel. Reference: `TextureSampling.ush` `GetBicubic2DCatmullRomSamples`.

**IMPACT: MEDIUM** — Causes slightly blurrier output than UE5 but TAA still
functions. Should be implemented for visual parity.

---

### ISSUE 3: Missing history clip (AABB ray intersection)

**Description:** UzEngine uses `clamp()` for history rejection (line 328).
UE5's default path for `TAA_PASS_CONFIG==0` with `AA_CLIP` disabled also uses
`clamp()` (line 1958), so this is actually correct for the non-clip path.

However, UE5's `AA_CLIP=1` path uses `HistoryClip()` which performs a ray-AABB
intersection from history toward filtered color, producing tighter rejection
with less color shift. This is an optional quality improvement.

**UE5:** `TemporalAA.usf:1957-1961` — `!AA_CLIP` path:
```hlsl
History.Color = clamp(History.Color, NeighborMin.Color, NeighborMax.Color);
```

**UzEngine:** `taa_resolve_cs.hlsl:328`:
```hlsl
float3 clampedHistory = clamp(historyYCoCg, neighborMin, neighborMax);
```

**FIX:** None required for parity. The clamp path matches UE5's default
`!AA_CLIP` behavior. For higher quality, implement `HistoryClip()` (AABB
ray intersection from `TemporalAA.usf:656-682`) as an optional upgrade.

**IMPACT: LOW** — Matches UE5's default behavior.

---

### ISSUE 4: Spatial filter weights are computed in-shader, not precomputed

**Description:** UzEngine computes Gaussian weights in the shader itself
(line 230: `exp(-2.29 * dot(pixelOffset, pixelOffset))`). UE5 precomputes
these weights on the CPU (TemporalAA.cpp:448-449) including the jitter offset,
normalizes them, and passes them via `PlusWeights` uniform array.

The UzEngine approach is functionally equivalent but:
1. Does not divide by `FilterSize` (UE5 TemporalAA.cpp:437-438)
2. Lacks normalization of the spatial weights before multiplication with HDR
   weights (UE5 normalizes SampleWeights on CPU, then PlusWeights)

**UE5:** `TemporalAA.cpp:434-478` — CPU-side weight computation with
`FilterSize` divisor and per-weight normalization.

**UzEngine:** `taa_resolve_cs.hlsl:228-230` — In-shader Gaussian, no FilterSize:
```hlsl
float2 pixelOffset = kPlusOffsets[j] - jitterOffset;
float spatialWeight = exp(-2.29 * dot(pixelOffset, pixelOffset));
```

**FIX:** The approach works but for exact parity, the spatial weights should be
precomputed on the CPU, divided by FilterSize, and normalized. The difference
is minor since FilterSize defaults to 1.0 for non-upsample TAA.

**IMPACT: LOW** — Functionally equivalent for FilterSize=1.0.

---

### ISSUE 5: Missing responsive AA stencil support

**Description:** UE5 reads a stencil buffer to identify "responsive" pixels
(hair, particles, translucency) and forces a higher blend weight (1/4) for
those pixels to reduce ghosting on semi-transparent or thin features.

**UE5:** `TemporalAA.usf:1987-2000` — `AA_SINGLE_PASS_RESPONSIVE`:
```hlsl
uint SceneStencilRef = StencilTexture.Load(int3(SceneStencilUV, 0));
InputParams.bIsResponsiveAAPixel = (SceneStencilRef & kResponsiveStencilMask) ? 1.f : 0.f;
```
Then at line 2254:
```hlsl
BlendFinal = InputParams.bIsResponsiveAAPixel ? (1.0/4.0) : BlendFinal;
```

**UzEngine:** Not implemented. No stencil read, no responsive AA path.

**FIX:** Add stencil buffer SRV, read bit 3, and force BlendFinal=0.25 for
responsive pixels. Requires marking hair/translucent materials with the stencil
bit in the GBuffer pass.

**IMPACT: LOW** — Only affects hair and translucent objects. Can be deferred
until those features exist.

---

### ISSUE 6: Missing dynamic anti-ghosting

**Description:** UE5 has an anti-ghosting system that detects when pixels
transition between static and dynamic (by checking velocity buffer in a plus
pattern). When a previously static pixel is now near dynamic objects, it resets
history to avoid ghosting.

**UE5:** `TemporalAA.usf:2151-2166` — `AA_DYNAMIC_ANTIGHOST`:
```hlsl
bool Dynamic = Dynamic1 || Dynamic3 || Dynamic4 || Dynamic5 || Dynamic7;
IgnoreHistory = IgnoreHistory || (!Dynamic && History.Color.a > 0);
```

**UzEngine:** Not implemented.

**FIX:** Requires velocity buffer. Once velocity buffer is available, read 5
velocity samples in a plus pattern and check for dynamic/static transitions.

**IMPACT: MEDIUM** — Causes ghosting when objects start/stop moving.

---

### ISSUE 7: Missing FilteredTemporalWeight in BlendFinal calculation

**Description:** UE5 multiplies `CurrentFrameWeight` by
`FilteredTemporalWeight` (line 2231), which accounts for the spatial filter's
contribution. UzEngine uses `currentFrameWeight` directly without this factor.

**UE5:** `TemporalAA.usf:2231`:
```hlsl
BlendFinal = IntermediaryResult.FilteredTemporalWeight * CurrentFrameWeight;
```

**UzEngine:** `taa_resolve_cs.hlsl:333`:
```hlsl
float blendFinal = currentFrameWeight;
```

**FIX:** For non-upsample TAA, `FilteredTemporalWeight` defaults to 1.0
(initialized in `CreateIntermediaryResult()`), so this is only different for
TAAU. No fix needed for 1:1 resolution TAA.

**IMPACT: LOW** — Only affects upsampling path (not implemented).

---

### ISSUE 8: Missing stochastic quantization

**Description:** UE5 applies stochastic dithering to the output to reduce
banding in low-precision render targets (`AA_ENABLE_STOCASTIC_QUANTIZATION=1`).

**UE5:** `TemporalAA.usf:2369-2376`:
```hlsl
FinalOutput0.rgb = QuantizeForFloatRenderTarget(FinalOutput0.rgb, E.x, OutputQuantizationError);
```

**UzEngine:** Not implemented.

**FIX:** Add `QuantizeForFloatRenderTarget()` — manipulates mantissa bits to
distribute quantization error. Low priority for RGBA16F targets.

**IMPACT: LOW** — Only visible with low-precision targets (R11G11B10F).

---

### ISSUE 9: NaN guard applied unconditionally

**Description:** UzEngine applies the NaN guard `blendFinal = -min(-blendFinal, 0.0)`
unconditionally (line 347). UE5 only does this for GLSL/Metal compilers where
`saturate(NaN)` does not return 0.

**UE5:** `TemporalAA.usf:2248-2251`:
```hlsl
#if AA_NAN && (COMPILER_GLSL || COMPILER_METAL)
    BlendFinal = -min(-BlendFinal, 0.0);
#endif
```

**UzEngine:** `taa_resolve_cs.hlsl:347`:
```hlsl
blendFinal = -min(-blendFinal, 0.0);  // Always applied
```

**FIX:** Not harmful — extra ALU on DX12 but functionally correct. Can
optionally guard with `#ifdef` for DX12-only path.

**IMPACT: NONE** — Correct but slightly wasteful.

---

### ISSUE 10: Velocity magnitude computed in wrong units

**Description:** UzEngine computes velocity as `length(motionVector * rcp(texelSize))`
which should give pixel-space velocity. However, since `ComputeMotionVector`
returns `float2(0,0)`, velocity is always 0 and the `lerp(blendFinal, 0.2, ...)`
at line 337 never activates.

**UE5:** `TemporalAA.usf:2094-2108`:
```hlsl
float2 BackTemp = BackN * OutputViewportSize.xy;
Velocity = sqrt(dot(BackTemp, BackTemp));
```

**UzEngine:** `taa_resolve_cs.hlsl:309-310`:
```hlsl
float2 velocityPixels = motionVector * rcp(texelSize); // always 0
float velocityMag = length(velocityPixels);            // always 0
```

**FIX:** Fix Issue 1 first (ComputeMotionVector). The velocity calculation
formula itself is correct once motion vectors are non-zero.

**IMPACT: HIGH** — Velocity-based blend modulation is completely inactive.

---

## SSR Trace Issues

### ISSUE 11: Missing ray behind-camera clamp

**Description:** UE5 clamps `RayEndWorldDistance` to prevent the ray from going
behind the camera. UzEngine passes `WorldTMax` directly without this clamp.

**UE5:** `SSRTRayCast.ush:83`:
```hlsl
float RayEndWorldDistance = ViewRayDirection.z < 0.0
    ? min(-0.95 * SceneDepth / ViewRayDirection.z, WorldTMax)
    : WorldTMax;
```

**UzEngine:** `ssr_trace_cs.hlsl:243`:
```hlsl
float3 RayEndWorld = RayOriginWorld + WorldRayDirection * WorldTMax;
```

**FIX:** Add the behind-camera clamp. This requires computing ViewRayDirection
(WorldRayDirection in view space) which needs the view matrix. Alternatively,
check if the ray end projects behind the near plane and clamp.

```hlsl
// Transform ray direction to view space
float3 viewRayDir = mul(float4(WorldRayDirection, 0), viewMatrix).xyz;
float RayEndWorldDistance = viewRayDir.z < 0.0
    ? min(-0.95 * SceneDepth / viewRayDir.z, WorldTMax)
    : WorldTMax;
float3 RayEndWorld = RayOriginWorld + WorldRayDirection * RayEndWorldDistance;
```

**IMPACT: MEDIUM** — Causes incorrect ray casting when reflection rays point
toward the camera (e.g., floor reflections looking down).

---

### ISSUE 12: Missing WorldTMaxClamp

**Description:** UE5 clamps `WorldTMax` to `WorldTMaxClamp` (default 1000000)
before ray initialization. UzEngine passes `sceneDepth` directly as WorldTMax.

**UE5:** `SSRTRayCast.ush:79`:
```hlsl
WorldTMax = min(WorldTMax, WorldTMaxClamp);
```

**UzEngine:** `ssr_trace_cs.hlsl:528-531` — passes `sceneDepth` directly.

**FIX:** Add `min(WorldTMax, 1000000.0)`. Minor issue since sceneDepth rarely
exceeds this value.

**IMPACT: LOW**

---

### ISSUE 13: RayDepthScreen computation differs from UE5

**Description:** UE5 computes `RayDepthScreen` by adding a view-space Z offset
through the `ViewToClip` matrix: `RayStartClip + mul(float4(0,0,SceneDepth,0), View.ViewToClip)`.
UzEngine instead projects `RayOriginWorld + float3(0,0,SceneDepth)` through
`viewProj`, which adds a WORLD-space Z offset instead of a VIEW-space Z offset.

**UE5:** `SSRTRayCast.ush:93-94`:
```hlsl
float4 RayDepthClip = RayStartClip + mul(float4(0, 0, RayEndWorldDistance, 0), View.ViewToClip);
float3 RayDepthScreen = RayDepthClip.xyz * rcp(RayDepthClip.w);
```

**UzEngine:** `ssr_trace_cs.hlsl:257-258`:
```hlsl
float4 RayDepthClip = mul(float4(RayOriginWorld + float3(0, 0, SceneDepth), 1.0f), viewProj);
float3 RayDepthScreen = RayDepthClip.xyz / RayDepthClip.w;
```

The UE5 approach adds `SceneDepth` along the camera's local Z axis (view
space). UzEngine adds along the world Z axis. For cameras not aligned with
world Z, this produces a different compare tolerance.

**FIX:** Need a `viewMatrix` (world-to-view) to compute the correct tolerance.
Or decompose the existing `viewProj` to extract the view Z direction and add
the offset in world space along that direction:

```hlsl
// Extract camera forward from invViewProj (3rd column of view matrix)
float3 cameraForward = normalize(float3(viewProj[0][2], viewProj[1][2], viewProj[2][2]));
float4 RayDepthClip = mul(float4(RayOriginWorld + cameraForward * SceneDepth, 1.0f), viewProj);
```

**IMPACT: MEDIUM** — Incorrect compare tolerance causes false hits/misses at
oblique camera angles.

---

### ISSUE 14: Missing bUncertain tracking and bStopWhenUncertain

**Description:** UE5 tracks whether the ray becomes "uncertain" (passes behind
geometry without hitting it) and can optionally stop early. UzEngine does not
track this state.

**UE5:** `SSRTRayCast.ush:456-465`:
```hlsl
bool4 bMultipleSampleUncertain = (MultipleSampleDepthDiff + CompareTolerance) < -CompareTolerance;
bUncertain = bUncertain || (bLocalMultisampleUncertain && !bFoundHit);
```

**UzEngine:** Not implemented. The `bUncertain` output does not exist.

**FIX:** Add uncertainty tracking. For SSR (not SSGI), `bStopWhenUncertain`
is false by default (`CreateDefaultCastSettings()`), so this only affects the
output signal, not the march behavior. Low priority.

**IMPACT: LOW** — Default SSR settings don't stop on uncertainty.

---

### ISSUE 15: Missing HZB UV factor (HZBUvFactorAndInvFactor)

**Description:** UE5 applies `InHZBUvFactorAndInvFactor` to convert between
viewport UV and HZB UV when the HZB doesn't cover the full render target
(e.g., with viewport offsets or dynamic resolution). UzEngine assumes identity.

**UE5:** `SSRTRayCast.ush:275-276`:
```hlsl
float3 RayStartUVz = float3((RayStartScreen.xy * float2(0.5, -0.5) + 0.5) * InHZBUvFactorAndInvFactor.xy, ...);
```

And at output (line 623):
```hlsl
OutHitUVz.xy *= InHZBUvFactorAndInvFactor.zw;
```

**UzEngine:** `ssr_trace_cs.hlsl:310`:
```hlsl
float3 RayStartUVz = float3((RayStartScreen.xy * float2(0.5f, -0.5f) + 0.5f), ...);
// No InHZBUvFactorAndInvFactor multiplication
```

**FIX:** If UzEngine's HZB always covers the full render target (no dynamic
resolution, no viewport offset), this is correct. Otherwise, add the factor.

**IMPACT: LOW** — Correct for full-viewport HZB. Would need fixing for dynamic
resolution.

---

### ISSUE 16: SSR hit color sampled at HZB UV instead of reprojected UV

**Description:** UE5 reprojects the hit point to the PREVIOUS frame's UV using
`ClipToPrevClip` and the velocity buffer (`ReprojectHit`), then samples the
PREVIOUS frame's scene color at the reprojected position. UzEngine samples the
CURRENT frame's scene color at the hit UV.

This is a significant difference. UE5's approach avoids self-reflection
artifacts because the scene color texture being sampled is from the previous
frame (before current reflections are composited). UzEngine samples the current
frame's scene color which may contain stale or incomplete lighting.

**UE5:** `SSRTReflections.usf:393-397`:
```hlsl
float2 SampleUV;
float Vignette;
ReprojectHit(PrevScreenPositionScaleBias, GBufferVelocityTexture,
             GBufferVelocityTextureSampler, HitUVz, SampleUV, Vignette);
OutColor = SampleScreenColor(SceneColor, SceneColorSampler, SampleUV) * Vignette;
```

Where `SceneColor` is the PREVIOUS frame's color (`PrevSceneColorPreExposureCorrection`
is applied at line 404).

**UzEngine:** `ssr_trace_cs.hlsl:560`:
```hlsl
float4 hitColor = SampleSceneColor(hitUVz.xy);
```

**FIX:** Either:
1. Use previous frame's scene color as the SSR source texture (requires
   keeping the previous frame's lit output), or
2. Sample current frame color but apply the reprojection offset for temporal
   stability.

**IMPACT: HIGH** — Causes self-reflection feedback loops and temporal
instability.

---

### ISSUE 17: SSR vignette uses single position, UE5 uses min of current and previous

**Description:** UE5's `ReprojectHit` computes vignette from BOTH the current
screen position AND the reprojected previous screen position, taking the
minimum. UzEngine only uses the current hit position.

**UE5:** `SSRTRayCast.ush:692`:
```hlsl
OutVignette = min(ComputeHitVignetteFromScreenPos(ThisScreen),
                  ComputeHitVignetteFromScreenPos(PrevScreen));
```

**UzEngine:** `ssr_trace_cs.hlsl:554`:
```hlsl
float vignette = ComputeHitVignette(hitUVz.xy); // Only current position
```

**FIX:** Once reprojection is implemented (Issue 16), compute vignette from
both current and previous positions and take the minimum.

**IMPACT: LOW** — Only affects edge pixels during camera motion.

---

### ISSUE 18: Missing PrevSceneColorPreExposureCorrection

**Description:** UE5 multiplies the SSR output by
`PrevSceneColorPreExposureCorrection` (line 404) to handle exposure changes
between frames. UzEngine does not have this correction.

**UE5:** `SSRTReflections.usf:404`:
```hlsl
OutColor.rgb *= PrevSceneColorPreExposureCorrection;
```

**UzEngine:** Not implemented.

**FIX:** When auto-exposure is implemented, add exposure correction to match
the pre-exposure of the current frame.

**IMPACT: LOW** — No auto-exposure system yet, so exposure is constant.

---

## SSR Resolve Issues

### ISSUE 19: Fresnel F0 for metals uses 0.8 instead of albedo

**Description:** The SSR resolve uses `lerp(0.04, 0.8, metallic)` for F0
(line 222 of `ssr_resolve_cs.hlsl`). UE5 does NOT apply Fresnel weighting in
the SSR pass at all — instead, it applies `EnvBRDF` (the split-sum
approximation) during compositing in the lighting pass.

For the single-ray SSR path, UE5's output is simply:
```hlsl
OutColor = SampleScreenColor(...) * Vignette;
OutColor *= RoughnessFade;
OutColor *= SSRParams.r;
```
No Fresnel, no G_SmithV. The `EnvBRDF` integration happens later when
combining SSR with IBL.

**UE5:** `SSRTReflections.usf:397-402`:
```hlsl
OutColor = SampleScreenColor(SceneColor, SceneColorSampler, SampleUV) * Vignette;
// ... (multi-ray path has Karis tonemap but no Fresnel)
OutColor *= RoughnessFade;
OutColor *= SSRParams.r;
```

And the comment at line 322:
```hlsl
// Correct integration applies DGF below but for speed we apply EnvBRDF
// later when compositing
```

**UzEngine:** `ssr_resolve_cs.hlsl:222-228`:
```hlsl
float3 F0 = lerp(0.04f, 0.8f, metallic);  // Invented constant 0.8
float3 F  = FresnelSchlick(NoV, F0);
float3 reflection = hitColor * F;
```

**FIX:** Remove Fresnel weighting from the SSR resolve pass entirely. The SSR
output should be unweighted scene color * vignette * roughnessFade * intensity.
Fresnel/EnvBRDF weighting should happen in the deferred lighting pass when
compositing SSR with IBL.

The `0.8` constant for metallic F0 is INVENTED — UE5 never uses this value.
Real metallic F0 comes from the albedo texture which is not available in this
pass.

**IMPACT: HIGH** — Incorrect energy distribution. Metals will be too dark at
grazing angles and dielectrics too bright. Double-Fresnel when combined with
IBL Fresnel in the lighting pass.

---

### ISSUE 20: SSR resolve applies roughnessFade and intensity TWICE

**Description:** The SSR trace pass (`ssr_trace_cs.hlsl`) already multiplies
by `roughnessFade` (line 568) and `ssrIntensity` (line 569). The resolve pass
applies them AGAIN (lines 231-232 of `ssr_resolve_cs.hlsl`). UE5 applies these
only ONCE.

**UE5:** `SSRTReflections.usf:401-402` — Applied once:
```hlsl
OutColor *= RoughnessFade;
OutColor *= SSRParams.r;
```

**UzEngine trace:** `ssr_trace_cs.hlsl:568-569`:
```hlsl
result *= roughnessFade;  // First application
result *= ssrIntensity;   // First application
```

**UzEngine resolve:** `ssr_resolve_cs.hlsl:231-232`:
```hlsl
reflection *= roughnessFade;   // SECOND application — WRONG
reflection *= ssrIntensity;    // SECOND application — WRONG
```

**FIX:** Remove the `roughnessFade` and `ssrIntensity` multiplications from
the resolve pass. They are already baked into the trace output.

**IMPACT: HIGH** — SSR reflections are squared in intensity: roughnessFade^2
and intensity^2. Reflections appear much dimmer than intended.

---

### ISSUE 21: G_SmithV computed but never used

**Description:** `ssr_resolve_cs.hlsl` computes `ComputeSmithV()` (lines
150-154) but never calls it. The function exists for the multi-ray SSR path
(SSR_QUALITY >= 3) which importance-samples the GGX distribution, but UzEngine
only implements single-ray SSR.

**UE5:** `SSRTReflections.usf:194` — `G_SmithV` is computed but only used in
the multi-ray path (line 335, `#if 0` guarded):
```hlsl
float G_SmithV = 2 * NoV / (NoV + sqrt(NoV * (NoV - NoV * a2) + a2));
// Used later in: SampleColor.rgb *= F * G_SmithL; (line 335, disabled)
```

**UzEngine:** `ssr_resolve_cs.hlsl:150-154` — Defined but dead code.

**FIX:** Remove the unused function or keep for future multi-ray SSR. Not a
correctness issue.

**IMPACT: NONE** — Dead code.

---

### ISSUE 22: Blend weight in alpha uses average Fresnel

**Description:** The resolve pass stores `confidence * roughnessFade * intensity * avg(F)`
in alpha (line 238). This value is used to blend SSR with IBL in the lighting
pass. However, UE5 does not produce a blend weight — it stores confidence
(vignette) in alpha and the compositing uses EnvBRDF for the blend factor.

**UzEngine:** `ssr_resolve_cs.hlsl:238`:
```hlsl
float blendWeight = confidence * roughnessFade * ssrIntensity * dot(F, 1.0f / 3.0f);
```

**FIX:** Since the Fresnel weighting should be removed (Issue 19) and the
double roughnessFade/intensity is wrong (Issue 20), this blend weight is
fundamentally incorrect. After fixing Issues 19 and 20, the alpha should
simply be the confidence/vignette from the trace pass.

**IMPACT: HIGH** — Incorrect SSR/IBL blending.

---

## Summary

### Critical (must fix for correct rendering)

| # | Issue | File | Impact |
|---|-------|------|--------|
| 1 | ComputeMotionVector is a stub (returns 0,0) | taa_resolve_cs.hlsl:116 | HIGH |
| 10 | Velocity modulation completely inactive | taa_resolve_cs.hlsl:309 | HIGH |
| 16 | SSR samples current frame instead of reprojected previous | ssr_trace_cs.hlsl:560 | HIGH |
| 19 | Resolve applies invented Fresnel (0.8 metallic F0) | ssr_resolve_cs.hlsl:222 | HIGH |
| 20 | roughnessFade and intensity applied TWICE | ssr_trace+resolve | HIGH |
| 22 | Blend weight formula is wrong | ssr_resolve_cs.hlsl:238 | HIGH |

### Important (affects quality)

| # | Issue | File | Impact |
|---|-------|------|--------|
| 2 | Missing bicubic history sampling | taa_resolve_cs.hlsl:320 | MEDIUM |
| 6 | Missing dynamic anti-ghosting | taa_resolve_cs.hlsl | MEDIUM |
| 11 | Missing ray behind-camera clamp | ssr_trace_cs.hlsl:243 | MEDIUM |
| 13 | RayDepthScreen uses world Z instead of view Z | ssr_trace_cs.hlsl:257 | MEDIUM |

### Low Priority (minor or future features)

| # | Issue | File | Impact |
|---|-------|------|--------|
| 3 | History clamp vs clip (matches UE5 default) | taa_resolve_cs.hlsl:328 | LOW |
| 4 | Spatial weights computed in shader vs CPU | taa_resolve_cs.hlsl:228 | LOW |
| 5 | Missing responsive AA stencil | taa_resolve_cs.hlsl | LOW |
| 7 | Missing FilteredTemporalWeight | taa_resolve_cs.hlsl:333 | LOW |
| 8 | Missing stochastic quantization | taa_resolve_cs.hlsl | LOW |
| 9 | NaN guard unconditional (harmless) | taa_resolve_cs.hlsl:347 | NONE |
| 12 | Missing WorldTMaxClamp | ssr_trace_cs.hlsl | LOW |
| 14 | Missing bUncertain tracking | ssr_trace_cs.hlsl | LOW |
| 15 | Missing HZB UV factor | ssr_trace_cs.hlsl:310 | LOW |
| 17 | Vignette single vs dual position | ssr_trace_cs.hlsl:554 | LOW |
| 18 | Missing exposure correction | ssr_trace_cs.hlsl | LOW |
| 21 | G_SmithV dead code | ssr_resolve_cs.hlsl:150 | NONE |

### What Is Correct

The following aspects match UE5 exactly:

- **RGBToYCoCg / YCoCgToRGB** — identical coefficients (TemporalAA.usf:597-619)
- **Luma4 / HdrWeight4 / HdrWeightY** — identical (TemporalAA.usf:623-637)
- **WeightedLerpFactors** — identical (TemporalAA.usf:684-692)
- **3x3 neighborhood offsets** — identical (TemporalAA.usf:447-458)
- **Plus indices** — identical {1,3,4,5,7} (TemporalAA.usf:464)
- **Closest depth cross pattern** — matches UE5's `AA_CROSS=2` (TemporalAA.usf:2034-2079)
- **Blend weight formula** — `lerp(blend, 0.2, saturate(v/40))` matches (TemporalAA.usf:2233)
- **Anti-flicker guard** — `max(blend, saturate(0.01*lumaH/abs(lumaF-lumaH)))` matches (TemporalAA.usf:2246)
- **NaN output guard** — `-min(-output, 0)` matches (TemporalAA.usf:2304-2306)
- **GetStepScreenFactorToClipAtScreenEdge** — identical (SSRTRayCast.ush:46-56)
- **SSRTRay struct** — identical layout (SSRTRayCast.ush:60-66)
- **CastScreenSpaceRay hit refinement** — identical linear interpolation (SSRTRayCast.ush:544-607)
- **ComputeHitVignette** — identical `saturate(abs(sp)*5-4)` (SSRTRayCast.ush:676)
- **GetRoughnessFade** — identical `min(r*scale+2, 1)` (SSRTReflections.usf:48)
- **SampleScreenColor NaN guard** — identical (SSRTRayCast.ush:733)
- **InterleavedGradientNoise** — identical constants (SSRTReflections.usf:348)
- **Ray march loop with batched samples** — matches UE5 (SSRTRayCast.ush:384-479)
- **Mip level progression** — `Level += (8/NumSteps)*Roughness` matches (SSRTRayCast.ush:424-428)
- **StartMipLevel=1.0** — matches (SSRTRayCast.ush:660)
- **SlopeCompareToleranceScale=4.0** — matches SSR path (SSRTRayCast.ush:643)
- **StepOffset bias=-0.5** — matches (SSRTReflections.usf:352)
- **NumSteps=16 for SSR_QUALITY=2** — matches (SSRTReflections.usf:203)
