<!-- doc-meta
title: PoC1 vs Current Engine — Deep Rendering Comparison
author: shader-engineer
date: 2026-03-18
-->

# PoC1 vs Current Engine — Deep Rendering Comparison

Line-by-line comparison of `E:\UzEnginePoC1` shaders vs `E:\UzEngine\runtime` shaders,
identifying every difference that contributes to the visual quality regression.

---

## Executive Summary — Why PoC1 Looks Better

The PoC1 renderer had a **full post-processing composite pass** that combined SSAO, SSR,
bloom, contact shadows, auto-exposure, DOF, motion blur, god rays, color grading
(saturation, contrast, white balance), sharpening (CAS), chromatic aberration, vignette,
film grain, and ACES tonemapping into **one final fullscreen shader**. The current engine
performs deferred lighting and tonemapping, but **does not wire most post-process effects
into the output**. The PoC1 also had richer G-Buffer data (envMultiplier, specularMultiplier
packed per-pixel) and more complete IBL ambient with horizon occlusion and multi-scattering.

---

## DIFFERENCE 1: Post-Process Composite Pass — MISSING ENTIRELY

```
DIFFERENCE: PoC1 has a full composite pass that combines 9 inputs (HDR, SSAO, bloom,
  auto-exposure, SSR, contact shadows, DOF, motion blur, god rays) with color grading,
  sharpening, and film grain. Current engine has NO composite pass — deferred lighting
  outputs directly to tonemap.

PoC1: composite.ps.hlsl (full file, 112 lines)
  - Reads 9 bindless textures: hdrTex, ssaoTex, bloomTex, exposureTex, ssrTex,
    contactShadowTex, dofTex, motionBlurTex, godRaysTex
  - Applies: contact shadows * hdr -> SSR additive -> DOF replace -> motion blur replace
    -> bloom additive -> exposure * white balance -> saturation/contrast -> ACES -> gamma
  - Default params: exposure=1.0, bloomStrength=0.04, gamma=2.2, colorSaturation=1.0,
    colorContrast=1.0, sharpenStrength=0.3

Current: No composite pass exists. deferred_lighting_ps.hlsl outputs linear HDR.
  tonemap_ps.hlsl reads HDR + optional bloom and applies exposure+ACES+sRGB.
  SSAO is baked into AO in deferred lighting. SSR is composited in deferred lighting.
  Contact shadows are in deferred lighting. No DOF, motion blur, or god rays wiring.

VISUAL IMPACT:
  - No visible bloom glow around bright objects (bloomStrength default 0.04 not applied)
  - No CAS sharpening (sharpenStrength=0.3 made textures visibly crisper)
  - No color grading (saturation=1.0, contrast=1.0 were actively applied)
  - No motion blur or DOF (even though passes exist in PoC1)
  - Flat, unsaturated look compared to PoC1's post-processed output

FIX: Wire bloom, DOF, motion blur into tonemap_ps.hlsl or create a dedicated
  composite pass. Add CAS sharpening as a separate pass or inline in tonemap.
  Ensure bloom texture is actually bound and bloomIntensity > 0.
```

---

## DIFFERENCE 2: G-Buffer Layout — envMultiplier and specularMultiplier Lost

```
DIFFERENCE: PoC1 G-Buffer stored envMultiplier and specularMultiplier per-pixel.
  Current G-Buffer does not store these — they are lost during the deferred transition.

PoC1: gbuffer_write.ps.hlsl:175-176
  RT1.b = envMultiplier * 0.5   (packed into RGB10A2 blue channel)
  RT2.a = specularMultiplier * 0.5 (packed into RGBA8 alpha)

Current: gbuffer_ps.hlsl:246
  RT1 = float4(OctEncode(N), metallic, roughness)
  No envMultiplier or specularMultiplier stored.

VISUAL IMPACT:
  - Materials cannot control their environment reflection intensity per-pixel
  - Specular multiplier is lost — all materials have uniform specular response
  - Materials like glass (high envMul) or cloth (low envMul) render identically

FIX: Either:
  (a) Add a 3rd G-Buffer RT for material params (envMul, specMul, emission scale)
  (b) Use the existing emissive RT alpha channel to pack one of these values
  (c) Pack envMul+specMul into the 2 remaining bits of the normal RT
```

---

## DIFFERENCE 3: IBL Ambient — Horizon Occlusion and Multi-Scatter Missing

```
DIFFERENCE: PoC1 deferred lighting has horizon occlusion (horizon = saturate(1 + dot(R, N)),
  specularAmbient *= horizon*horizon) and Kulla-Conty multi-scatter energy compensation
  with proper Favg and geometric series. Current deferred lighting uses a simpler
  multi-scatter approximation.

PoC1: deferred_lighting.ps.hlsl:351-360
  float horizon = saturate(1.0 + dot(R, N));
  specularAmbient *= horizon * horizon;   // <-- prevents light leak on concave surfaces

  // Kulla-Conty 2017 multi-scatter:
  float3 FssEss = F0 * envBRDF.x + envBRDF.y;
  float  Ess    = envBRDF.x + envBRDF.y;
  float  Ems    = 1.0 - Ess;
  float3 Favg   = F0 + (1.0 - F0) / 21.0;
  float3 FmsEms = Ems * FssEss * Favg / (1.0 - Favg * Ems);
  specularAmbient += FmsEms * irradiance * 0.5;

Current: deferred_lighting_ps.hlsl:860-865
  // No horizon occlusion at all
  // Multi-scatter is simpler:
  float Ems = 1.0 - (envBRDF.x + envBRDF.y);
  float3 Favg = F0 + (1.0 - F0) / 21.0;
  specularIBL *= 1.0 + Favg * Ems / (1.0 - Favg * Ems);
  // Missing: FssEss term, missing: FmsEms * irradiance * 0.5 contribution

VISUAL IMPACT:
  - Specular light leaks around concave geometry edges (no horizon fade)
  - Slightly less energy compensation on rough metals (missing irradiance*0.5 boost)
  - Rough metallic surfaces appear slightly darker than PoC1

FIX: Add `float horizon = saturate(1.0 + dot(R, N));` before specular IBL application.
  Multiply specular by `horizon * horizon`. This is a single line addition.
```

---

## DIFFERENCE 4: Specular Occlusion — Present in PoC1, Missing in Current

```
DIFFERENCE: PoC1 applies SpecularOcclusion(NoV, ao, roughness) to specular ambient.
  Current does not apply specular occlusion — only scalar AO or AOMultiBounce.

PoC1: deferred_lighting.ps.hlsl:362-363
  float specOcc = SpecularOcclusion(NoV, ao, roughness);
  specularAmbient *= specOcc;

  SpecularOcclusion formula (ibl.hlsli:37-39):
  saturate(pow(NoV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao)

Current: deferred_lighting_ps.hlsl:871-872
  float3 aoTerm = (gtaoSrvIndex != 0xFFFFFFFF) ? AOMultiBounce(ao, albedo) : float3(ao, ao, ao);
  ambient = (diffuseIBL + specularIBL) * aoTerm * skyLightIntensity * skyLightTint;
  // Same AO applied to both diffuse and specular — no view-dependent specular occlusion

VISUAL IMPACT:
  - Specular reflections bleed into occluded areas (creases, edges, crevices)
  - AO affects specular and diffuse equally — not physically correct
  - Less defined surface detail on rough materials near concavities

FIX: Apply SpecularOcclusion to specular IBL SEPARATELY from diffuse AO.
  specularIBL *= SpecularOcclusion(NdotV, ao, roughness);
  diffuseIBL *= aoTerm;  // keep multi-bounce AO for diffuse
```

---

## DIFFERENCE 5: Micro-Shadow Term — Present in PoC1, Missing in Current

```
DIFFERENCE: PoC1 applies a micro-shadow term to direct lighting based on AO.
  This darkens direct light contribution in AO-occluded areas.

PoC1: deferred_lighting.ps.hlsl:301-302
  float NoL_direct = saturate(dot(N, normalize(-lightDir)));
  float microShadow = saturate(NoL_direct + 2.0 * ao * ao - 1.0);
  Lo += directLight * sScale * shadowFactor * microShadow;

Current: deferred_lighting_ps.hlsl:684-685
  Lo = EvaluateBRDF(N, V, L_dir, lightColor.xyz,
                    albedo, metallic, alpha, F0) * shadow;
  // No micro-shadow — AO only affects ambient

VISUAL IMPACT:
  - Direct light fully illuminates geometry in AO-occluded creases
  - Less defined surface detail under direct lighting
  - Flat look in corners and crevices that should be darker

FIX: After computing directional light, multiply by microShadow:
  float NdotL_direct = saturate(dot(N, L_dir));
  float microShadow = saturate(NdotL_direct + 2.0 * ao * ao - 1.0);
  Lo *= microShadow;
```

---

## DIFFERENCE 6: Shadow Filtering — Poisson+Distance Penumbra vs Fixed 5x5 PCF

```
DIFFERENCE: PoC1 uses 16-tap Poisson disk with distance-based penumbra scaling.
  Current uses 5x5 grid PCF (25 taps) or 16-tap Poisson with fixed per-cascade radius.

PoC1: deferred_lighting.ps.hlsl:189
  float penumbra = clamp(shadowZ * 3.5, 0.5, 2.5) * (1.0 + cascadeIdx * 0.3);
  // Penumbra scales with depth AND cascade — natural soft-to-hard gradient

Current: shadow_common.hlsli:108-118
  static const float kCascadePCFRadius[4] = { 2.0, 3.0, 4.0, 6.0 };
  // Fixed radius per cascade — no depth-based variation within a cascade

VISUAL IMPACT:
  - Shadows appear uniformly soft within each cascade instead of getting softer with distance
  - Contact shadows at close range are too soft (2.0 texels vs PoC1's 0.5 texels)
  - Missing the natural penumbra gradient that makes shadows feel grounded

FIX: Add depth-based penumbra scaling within each cascade:
  float depthPenumbra = clamp(sceneDepth * 3.5, 0.5, 2.5);
  float totalPenumbra = depthPenumbra * (1.0 + cascadeIdx * 0.3);
  Use totalPenumbra instead of fixed kCascadePCFRadius.
```

---

## DIFFERENCE 7: TAA Pass — Full Implementation vs Not Wired

```
DIFFERENCE: PoC1 has a complete TAA implementation with jitter-weighted Gaussian
  neighborhood filtering, variance AABB clamping in YCoCg, velocity dilation,
  HDR-weighted temporal blend, anti-flicker, and SSAO baked into TAA accumulation.
  Current engine does NOT run any TAA pass.

PoC1: taa.ps.hlsl (233 lines)
  - Jittered camera (sub-pixel Halton sequence)
  - 3x3 neighborhood with jitter-weighted Gaussian (sigma=0.47)
  - Variance AABB in YCoCg color space
  - Velocity dilation from closest depth in X-pattern at distance 2
  - HDR weight: rcp(Luma4 + 4.0)
  - Anti-flicker luminance-diff suppression
  - Blend factor: base 0.04, velocity ramp to 0.4, clamped [0.01, 0.5]
  - SSAO baked into HDR signal before TAA accumulation (temporal AO stability)

Current: No TAA wiring. Deferred lighting outputs directly to tonemap.
  No velocity buffer generated. No jitter applied to camera.

VISUAL IMPACT:
  - Visible aliasing on all geometry edges (stair-stepping, shimmer)
  - Specular highlights shimmer/sparkle instead of being temporally stable
  - SSAO appears noisy (no temporal accumulation)
  - Normal maps and thin geometry flicker when camera moves slightly

FIX: Implement the TAA resolve pass from PoC1:
  1. Add Halton jitter to camera projection (PerFrameCB already has jitter fields in spec)
  2. Write velocity buffer during G-Buffer pass (GBuffer3 = RG16F velocity per spec)
  3. Port taa.ps.hlsl as a post-process pass after deferred lighting
  4. Feed SSAO into TAA so AO accumulates temporally
```

---

## DIFFERENCE 8: GTAO Pass — Full Implementation vs Partially Wired

```
DIFFERENCE: PoC1 has a complete XeGTAO-inspired ambient occlusion pass with horizon-search,
  bent normals, and multi-bounce approximation. It runs as a standalone pass and feeds
  into TAA for temporal stability. Current engine supports GTAO SRV index but the
  pass may not actually generate the texture.

PoC1: gtao.ps.hlsl (151 lines)
  - 4 directions x 6 steps horizon search
  - Depth-derived view-space normals (cross product of position derivatives)
  - Fast acos approximation (XeGTAO)
  - GTAO visibility integral: (cos(n) + (h1+h2)*sin(n) - ...) / 4.0
  - Bent normal output for indirect lighting directionality
  - Multi-bounce: visibility + visibility * (1 - visibility) * 0.3
  - Default params: aoRadius=0.5, aoIntensity=1.0, aoFalloff=1.0

Current: deferred_lighting_ps.hlsl supports gtaoSrvIndex but:
  - No GTAO compute/pixel shader found in runtime/shaders/
  - GTAO value is 1.0 when gtaoSrvIndex == 0xFFFFFFFF (no GTAO active)
  - Falls back to material AO only (from G-Buffer)

VISUAL IMPACT:
  - No screen-space ambient occlusion visible at all
  - Geometry contact points have no darkening (objects float visually)
  - Interior scenes have no depth cues from occlusion
  - Surface detail is flat — no small-scale shadows in creases

FIX: Port gtao.ps.hlsl as a compute or fullscreen pixel shader pass.
  Wire it into the render graph between G-Buffer write and deferred lighting.
  The deferred lighting shader already reads gtaoSrvIndex — just need the pass.
```

---

## DIFFERENCE 9: SSR Pass — Full Implementation vs Partially Wired

```
DIFFERENCE: PoC1 has a complete screen-space reflections pass with linear ray march,
  binary refinement, screen-edge fade, and Fresnel-based intensity. Current engine
  has SSR compositing code in deferred lighting but no SSR generation pass.

PoC1: ssr.ps.hlsl (161 lines)
  - 32-step linear ray march in view space
  - 4-step binary refinement for sub-pixel accuracy
  - Adaptive step size (stepLen *= 1.05 per step)
  - Screen-edge fade (smoothstep at borders)
  - Fresnel intensity: pow(1-NdotV, 5) * 0.9 + 0.1
  - Temporal jitter for banding reduction

Current: deferred_lighting_ps.hlsl:841-853 has SSR compositing:
  if (ssrSrvIndex != 0xFFFFFFFF) {
      combinedSpecular = lerp(prefilteredColor, ssrData.rgb, saturate(ssrData.a));
  }
  But NO SSR generation pass exists in the runtime shaders.

VISUAL IMPACT:
  - No visible reflections on wet/metallic/glossy surfaces
  - Floor reflections entirely missing
  - Scene feels less grounded — no visual connection between objects and surfaces

FIX: Port ssr.ps.hlsl as a fullscreen post-process pass.
  Wire output to ssrSrvIndex. The compositing code in deferred lighting already works.
```

---

## DIFFERENCE 10: Contact Shadows — Different Implementation Quality

```
DIFFERENCE: PoC1 has a dedicated contact shadow pass (view-space ray march with
  keijiro-style rejection depth, smooth occlusion weight, and distance fade).
  Current engine has inline contact shadows in deferred lighting (UE5-style NDC march).
  Both exist but differ in quality.

PoC1: contact_shadows.ps.hlsl (127 lines)
  - View-space ray march with screen reprojection
  - Smooth occlusion weight: (1-weight)*(1-offs) — gradual falloff
  - Distance fade to 50m (contact shadows diminish far from camera)
  - keijiro-style rejection depth (prevents false positives)
  - Output: separate R8_UNORM texture, composited in composite.ps.hlsl
  - Default params: contactShadowIntensity=0.1, sampleCount=16, rejectionDepth=0.1

Current: contact_shadows.hlsli (207 lines)
  - UE5-style NDC clip-space ray march (CastScreenSpaceShadowRay)
  - Binary hit/miss (no smooth weight — 0.0 or 1.0 output)
  - No distance fade
  - 8 steps (vs PoC1's 16)
  - contactShadowLength must be > 0 (default is 0.0 = DISABLED)

VISUAL IMPACT:
  - Contact shadows are OFF by default (contactShadowLength = 0.0)
  - When enabled, binary hit/miss creates hard shadow artifacts vs PoC1's smooth gradients
  - No distance fade — contact shadows appear at full strength far from camera (noisy)
  - Fewer steps (8 vs 16) — more missed occlusion, less detailed shadows

FIX:
  1. Set contactShadowLength default to 0.02 (2% screen-space fraction)
  2. Add smooth occlusion weight instead of binary hit/miss
  3. Add distance fade (saturate(1 - dist/50.0))
  4. Increase step count to 16 for better quality
```

---

## DIFFERENCE 11: Tonemap Pipeline — Color Grading Lost

```
DIFFERENCE: PoC1 composite pass applies white balance, saturation, and contrast
  BEFORE tonemapping. Current tonemap_ps.hlsl applies FilmToneMap but has no
  saturation/contrast/white balance controls.

PoC1: composite.ps.hlsl:96-102
  color *= finalExposure;
  color = WhiteBalance(color, colorTemperature, colorTint);
  float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
  color = lerp(float3(luma, luma, luma), color, colorSaturation);
  color = pow(max(color, 0.0), colorContrast);
  color = ACESFilm(color);

Current: tonemap_ps.hlsl:290-300
  color *= exposure;
  color = FilmToneMap(color, filmSlope, filmToe, filmShoulder, filmBlackClip, filmWhiteClip);
  color = LinearToSrgb(color);
  // No saturation, no contrast, no white balance

VISUAL IMPACT:
  - Flat, desaturated look compared to PoC1
  - No ability to warm/cool the scene (white balance)
  - No contrast control — image appears washed out
  - Artists cannot tune the look

FIX: Add color grading BEFORE tonemapping in tonemap_ps.hlsl:
  Add saturation, contrast, and white balance as PerFrameCB parameters.
  Apply in the same order as PoC1 (exposure -> WB -> saturation -> contrast -> tonemap).
```

---

## DIFFERENCE 12: Bloom — Exists in PoC1 Pipeline, Not Wired in Current

```
DIFFERENCE: PoC1 has bloomPass_.init() and a bloom texture wired into the composite pass.
  Current engine has bloom mentioned in tonemap_ps.hlsl but the bloom texture is not
  bound (bloomColorIdx == 0 or bloomIntensityBits == 0).

PoC1: renderer.cpp:171, composite.ps.hlsl:93
  bloomPass_.init(shaderLib_, width, height);
  color = lerp(color, color + bloom, bloomStrength);  // bloomStrength=0.04

Current: tonemap_ps.hlsl:257-262
  if (tonemapIndices.bloomColorIdx != 0) {
      color += bloom * bloomIntensity;
  }
  // bloomColorIdx is likely 0 or bloomIntensity is 0

VISUAL IMPACT:
  - No bloom glow around emissive objects, bright specular highlights, or sun
  - Scene appears flat and clinical without bloom
  - Emissive materials don't visually "pop" — no light bleed

FIX: Ensure bloomPass runs and its output SRV index is passed to tonemap_ps.
  Set default bloomIntensity to 0.04 (PoC1 default).
```

---

## DIFFERENCE 13: Gamma Handling — PoC1 Manual vs Current sRGB

```
DIFFERENCE: PoC1 applies manual pow(color, 1.0/gamma) with gamma=2.2 default.
  Current uses LinearToSrgb() which is the correct piecewise sRGB curve.

PoC1: composite.ps.hlsl:108
  color = pow(max(color, 0.0), 1.0 / gamma);  // gamma = 2.2

Current: tonemap_ps.hlsl:310
  color = LinearToSrgb(color);
  // Piecewise: linear segment below 0.0031308, pow(x, 1/2.4)*1.055-0.055 above

VISUAL IMPACT:
  - Very subtle: sRGB curve is slightly different from pow(1/2.2) in the darks.
  - Not a major quality difference — current is technically more correct.
  - However, if the backbuffer IS sRGB (DXGI_FORMAT_R8G8B8A8_UNORM_SRGB), then
    manual LinearToSrgb is being applied twice (double gamma) making the image
    appear washed out.

FIX: Verify whether the backbuffer format is _SRGB or _UNORM. If _SRGB,
  do NOT apply LinearToSrgb — the hardware does it automatically.
  If _UNORM, LinearToSrgb is correct.
```

---

## DIFFERENCE 14: Normal Map Processing — Identical

Both PoC1 (gbuffer_write.ps.hlsl:159-162) and current (gbuffer_ps.hlsl:172-178)
use the same approach: sample XY from normal map, scale by normalStrength/normalIntensity,
reconstruct Z, transform by TBN. No difference here.

---

## DIFFERENCE 15: Vis_SmithJointApprox — Different Epsilon Guard

```
DIFFERENCE: Minor numerical difference in the visibility function.

PoC1: deferred_lighting.ps.hlsl:93
  return 0.5 / (Vis_SmithV + Vis_SmithL + 1e-5);

Current: pbr_common.hlsli:144
  return 0.5f * rcp(Vis_SmithV + Vis_SmithL);
  // No epsilon — relies on rcp() not generating inf/NaN for tiny denominators

VISUAL IMPACT:
  - Potential NaN/inf fireflies when NoV or NoL are very small
  - Very rare but could cause single-pixel bright spots

FIX: Add 1e-5 epsilon: return 0.5f * rcp(Vis_SmithV + Vis_SmithL + 1e-5f);
```

---

## DIFFERENCE 16: Fresnel F90 Micro-Occlusion

```
DIFFERENCE: PoC1's F_Schlick returns f90*Fc + (1-Fc)*F0 (swapped order).
  Current FresnelSchlick returns F0 + (1-F0) * Pow5(1-cosTheta) (standard form).
  The PoC1 form includes the f90 = saturate(50*F0.g) clamp.

PoC1: deferred_lighting.ps.hlsl:97-100
  float3 F_Schlick(float3 F0, float VoH) {
      float Fc = Pow5(1.0 - VoH);
      float f90 = saturate(50.0 * F0.g);
      return f90 * Fc + (1.0 - Fc) * F0;
  }

Current: pbr_common.hlsli:94-96 (FresnelSchlick)
  return F0 + (1.0 - F0) * Pow5(saturate(1.0 - cosTheta));
  // NO f90 clamp — standard Schlick

Current: pbr_common.hlsli:270-271 (EvaluateBRDF — f90 IS used)
  float F90 = saturate(50.0f * F0.g);
  float3 F = F0 + (F90 - F0) * Pow5(1.0 - VdotH);

VISUAL IMPACT:
  - The EvaluateBRDF path correctly uses f90 — so direct lighting matches.
  - But FresnelSchlick (used elsewhere) does NOT use f90, which could affect
    any code path calling FresnelSchlick directly.
  - FresnelSchlickRoughness in the IBL path also omits f90.

FIX: Minor — ensure all Fresnel paths use the f90 micro-occlusion clamp.
```

---

## DIFFERENCE 17: Auto-Exposure — PoC1 Has It, Current Has Stub

```
DIFFERENCE: PoC1 has ExposurePass with luminance histogram and temporal adaptation.
  Current engine has exposure field in PerFrameCB but no auto-exposure pass.

PoC1: exposure_pass.cpp (full implementation)
  - 1x1 RT for exposure value
  - Histogram-based luminance metering
  - Temporal adaptation (lerp from previous frame)
  - Component-driven from PostProcessCoreData

Current: PerFrameCB has `float exposure` at offset 252.
  Set from C++ to a fixed value. No auto-exposure pass.

VISUAL IMPACT:
  - Bright scenes are blown out, dark scenes are too dark
  - No dynamic range adaptation when looking at sun vs shadows
  - Interior/exterior transitions don't adapt

FIX: Implement auto-exposure pass (see PoC1 exposure_pass.cpp).
```

---

## DIFFERENCE 18: God Rays — PoC1 Has It, Current Missing

```
DIFFERENCE: PoC1 has godRaysPass with volumetric light shafts.
  Current engine has no god rays implementation.

PoC1: renderer.cpp:179
  godRaysPass_.init(shaderLib_, width, height);
  // composite.ps.hlsl reads godRaysTex at t8

Current: No god rays pass or shader.

VISUAL IMPACT:
  - No visible light shafts from directional light through atmosphere
  - Less dramatic lighting in exterior scenes

FIX: Port god rays pass from PoC1.
```

---

## DIFFERENCE 19: DOF (Depth of Field) — PoC1 Has It, Current Missing

```
DIFFERENCE: PoC1 has dofPass with focus distance/aperture/focal length.
  Current engine has no DOF implementation.

PoC1: renderer.cpp:177
  dofPass_.init(shaderLib_, width, height);
  // composite.ps.hlsl: if (dofFocusDistance > 0.001) color = dofColor

Current: No DOF pass or shader.

VISUAL IMPACT:
  - Background always in sharp focus — no cinematic depth separation
  - Less visual depth cues

FIX: Port DOF pass from PoC1.
```

---

## DIFFERENCE 20: CAS Sharpening — PoC1 Has It, Current Missing

```
DIFFERENCE: PoC1 composite pass includes sharpenStrength=0.3 (CAS-style sharpening).
  Current has no sharpening pass or inline sharpening.

PoC1: composite_pass.cpp:107
  const float sharpenStrength = ppExt ? ppExt->sharpen_strength : ppExtDefaults.sharpen_strength;
  // Default: 0.3

Current: No sharpening.

VISUAL IMPACT:
  - Textures appear slightly soft/blurry compared to PoC1
  - Normal map detail is less pronounced
  - Overall image lacks crispness

FIX: Add CAS or unsharp mask pass before final output. AMD FidelityFX CAS is
  a simple single-pass filter that adds significant perceived sharpness.
```

---

## DIFFERENCE 21: Shadow Squaring (PCF Over-Blur Correction)

```
DIFFERENCE: Current engine applies shadow * shadow (PCF over-blur correction from UE5).
  PoC1 does NOT apply this squaring.

PoC1: deferred_lighting.ps.hlsl:198
  return shadow / 16.0;   // raw PCF average, no squaring

Current: shadow_common.hlsli:424
  shadow = shadow * shadow;   // PCF over-blur correction

VISUAL IMPACT:
  - Current shadows are DARKER than PoC1 (squaring makes 0.5 shadow → 0.25)
  - Shadow penumbra is narrower and more contrasty
  - This was intentional for UE5 parity but makes shadows look harder/darker

FIX: This is a design choice. If shadows appear too dark, reduce receiverBias
  from 0.9 toward 0.7, or remove the squaring for softer shadows.
  The user's complaint "shadows too dark and hard" is partly caused by this.
```

---

## Priority Summary — Fixes Ranked by Visual Impact

| Priority | Issue | Visual Impact | Effort |
|----------|-------|---------------|--------|
| P0 | TAA not wired | Aliasing, shimmer, noise | High |
| P0 | GTAO pass missing | No screen-space AO | Medium |
| P0 | SSR pass missing | No reflections | Medium |
| P0 | Bloom not wired | No glow/bloom | Low |
| P0 | Contact shadows OFF by default | No fine shadow detail | Low |
| P1 | Horizon occlusion missing | Specular light leak | Low |
| P1 | Specular occlusion missing | Specular in AO areas | Low |
| P1 | Micro-shadow missing | Flat direct lighting | Low |
| P1 | Color grading (sat/contrast) | Washed out look | Low |
| P1 | CAS sharpening missing | Soft textures | Low |
| P2 | envMultiplier/specMul lost | Per-material IBL control | Medium |
| P2 | Auto-exposure missing | No dynamic range adapt | Medium |
| P2 | Shadow squaring too dark | Hard shadows | Low |
| P2 | DOF missing | No depth blur | Medium |
| P2 | God rays missing | No light shafts | Medium |
| P3 | Vis epsilon guard | Rare fireflies | Trivial |
| P3 | F90 inconsistency | Very subtle | Trivial |
| OK | Normal map processing | No difference | None |
| OK | Gamma handling | Correct in current | None |

---

## Quick Wins (< 30 min each)

1. **Enable contact shadows**: Set `contactShadowLength` default to `0.02f` in C++
2. **Add horizon occlusion**: 2 lines in deferred_lighting_ps.hlsl IBL section
3. **Add specular occlusion**: 1 line in deferred_lighting_ps.hlsl
4. **Add micro-shadow**: 3 lines after directional light evaluation
5. **Wire bloom**: Ensure bloomPass output SRV is passed to tonemap, set intensity=0.04
6. **Add Vis epsilon**: Change `rcp(...)` to `rcp(... + 1e-5f)` in pbr_common.hlsli
7. **Add color grading**: Saturation/contrast before tonemap (5 lines)
