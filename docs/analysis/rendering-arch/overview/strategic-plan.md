<!-- doc-meta
title: Strategic Render Improvement Plan — Definitive Prioritized Action Plan
description: Phased implementation plan for UzEngine visual quality improvements, based on cross-engine consensus analysis
author: engine-architect
date: 2026-03-22
-->

# Strategic Render Improvement Plan

**Based on**: `consensus-analysis.md` cross-engine analysis
**Honest starting point**: The render is salvageable. Two parameter changes and one feature implementation will produce 80% of the visual improvement.

---

## EXECUTIVE SUMMARY

The current render looks wrong for THREE reasons, in order of severity:

1. **Parameters are miscalibrated** — exposure 1.4 and skylight 3.5 are both 2-3× above reference values. Fix these first. Cost: 5 minutes.
2. **Micro-shadows are disabled** — a TODO blocks surface contact detail. Fix this second. Cost: 2-4 hours.
3. **SSR is missing** — all reflective surfaces are flat. Implement SSR last. Cost: 1-2 days.

Everything else is either already correct or lower-priority polish.

---

## Phase 1: Quick Wins — Maximum Visual Impact, Minimum Effort

These changes are parameter corrections. They require no new code, no new shaders, and no architectural changes. They will have IMMEDIATE visible impact.

---

### P1.1 — Fix Exposure Default (CRITICAL, 5 minutes)

**What**: Change `r.Exposure` default from `1.4` to `1.0`.

**Why**: 1.4 is 1.4× brighter than the neutral reference point. Combined with ACES filmic tonemapping (which is already designed for HDR input), the scene is systematically overexposed. Highlights are crushed. Midtones are washed out. ALL THREE reference engines default to `1.0` or equivalent auto-exposure targeting 0.18 middle gray.

**Consensus**: UE5 (AutoExposureBias=0.0), Unity HDRP (exposure=1.0), Godot (sky_energy=1.0) — 3/3 engines agree.

**Which files**:
- `runtime/engine/renderer/core/cvar_registry.cpp` or equivalent CVar registration
- Search: `r.Exposure` default value registration

**What value**: Change default from `1.4f` to `1.0f`.

**Priority**: P0
**Effort**: 5 minutes
**Visual impact**: Scene will immediately show correct contrast and highlight distribution.

---

### P1.2 — Fix SkyLight Intensity Default (CRITICAL, 5 minutes)

**What**: Change `r.SkyLight.Intensity` default from `3.5` to `1.0`.

**Why**: SkyLight Intensity is the multiplier on ALL IBL ambient lighting (diffuse SH + specular probes). At 3.5, every shadow region receives 3.5× the correct ambient contribution. This makes shadows milky gray instead of dark, kills scene contrast, and makes every material look like it's made of glowing chalk. UE5 SkyLight defaults to Intensity=1.0. Unity HDRP ambient=1.0. Godot sky_energy=1.0.

**Consensus**: 3/3 engines use 1.0. UzEngine uses 3.5.

**Which files**:
- Same CVar registry as above
- Also check: `SkyLightComponent` default constructor in `sky_light_component.h` or equivalent

**What value**: Change default from `3.5f` to `1.0f`.

**Priority**: P0
**Effort**: 5 minutes
**Visual impact**: Shadows will become dark and contrasty. Materials will show correct tonal separation.

**Note**: After this change, the MinAmbient floor `r.SkyLight.MinAmbient = 0.1` may need to be lowered to `0.02f` as well. At MinAmbient=0.1, even pure shadow regions get 10% of the IBL ambient, which prevents deep shadows. UE5 has no MinAmbient floor by default.

---

### P1.3 — Fix Micro-Shadow TODO (HIGH, 2-4 hours)

**What**: Debug and re-enable micro-shadows in `deferred_lighting_ps.hlsl:1038-1040`.

**Why**: Micro-shadows apply surface-level contact shadowing using the GTAO/AO texture to darken crevices and surface detail. Without this, every surface looks uniformly lit at the micro scale — no contact darkening in grooves, wrinkles, or surface detail.

The TODO at line 1038-1040 says: `TODO: investigate GTAO texture state at this point in the pipeline`.

This is a diagnostic task, not an implementation task.

**Consensus**: UE5 (micro-shadows), Unity HDRP (contact shadows + AO-based darkening), Godot (SSAO contact shadows) — 3/3 engines apply this.

**Which files**:
- `runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl:1038-1040`
- `runtime/engine/renderer/passes/gtao_pass.cpp` (check output texture state after pass)

**Diagnostic approach**:
1. Read `deferred_lighting_ps.hlsl:1030-1050` to see the exact disabled block
2. Read `frame_setup.cpp` to trace GTAO texture state between GTAO pass and deferred lighting pass
3. Add `--shadow-debug 1` AO grid tile visualization to verify GTAO texture is populated
4. If SRV conflict: ensure GTAO texture is transitioned to SRV state before deferred lighting PS reads it
5. Re-enable the micro-shadow computation

**Priority**: P0
**Effort**: 2-4 hours
**Visual impact**: Surface detail will show micro-contact darkening. Brick, stone, fabric will all gain realism.

---

### P1.4 — Verify and Correct ACES Parameter Calibration (MEDIUM, 1 hour)

**What**: Audit the ACES parameter defaults against the published reference. Confirm the UzEngine defaults (Slope=0.91, Toe=0.45, Shoulder=0.22) are correct.

**Why**: The UzEngine audit flags these parameters as present but says "should be verified against reference". The standard Narkowicz 2015 ACES fit has NO adjustable slope/toe/shoulder — those are film aesthetic controls on top of the base ACES curve. If the defaults are wrong, the tone curve will produce tinted or over-contrasted output.

**Reference values**:
- UE5 default: FilmSlope=0.88, FilmToe=0.55, FilmShoulder=0.26, FilmBlackClip=0.0, FilmWhiteClip=0.04
- UzEngine: Slope=0.91, Toe=0.45, Shoulder=0.22, BlackClip=0.0, WhiteClip=0.04

**Which files**:
- `runtime/shaders/runtime/include/pbr_common.hlsli:26-54`
- CVar registry for tonemap parameters

**Action**: Compare UzEngine film defaults against UE5 reference. If different, update to match UE5 defaults. If intentionally different, document why.

**Priority**: P1
**Effort**: 1 hour
**Consensus**: UE5 + Unity agree on ACES. Godot uses simplified version. 2/3 consensus on exact parameters.

---

### P1.5 — Verify Contact Shadow Length Default (MEDIUM, 30 minutes)

**What**: Audit `r.ContactShadow.Length = 0.03` against reference engine defaults.

**Why**: Contact shadow length is expressed as a fraction of screen height. At 0.03 (3% of screen), the ray is very short. UE5 uses `ContactShadowLength = 0.0` (disabled by default, enabled per-light). Unity HDRP uses `length = 0.5` world units. The 0.03 screen-space value may be too short to catch shadows on close-range geometry.

**Which files**:
- CVar registry
- `runtime/shaders/runtime/include/contact_shadows.hlsli`

**Action**: Test with values 0.05, 0.1, and 0.15 and compare visual output. Select the value that best matches UE5 reference at equivalent scene scale.

**Priority**: P1
**Effort**: 30 minutes
**Visual impact**: Better contact shadowing detail on surfaces near the camera.

---

## Phase 2: Medium Effort — Important Missing Features

---

### P2.1 — Implement Screen-Space Reflections (SSR) (CRITICAL, 8-12 hours)

**What**: Implement the two compute shaders declared in `pipeline_library.h` but never written: `ssr_trace_cs.hlsl` and `ssr_resolve_cs.hlsl`. Integrate into the post-process pipeline before tonemapping.

**Why**: Without SSR, ALL reflective and metallic surfaces fall back to the 128x128 sky IBL cubemap. The result is that floors, wet stone, metal, water, and glass all show the same blurry, low-frequency sky color instead of reflecting their actual surroundings. This is the single feature whose absence most clearly signals "game engine, circa 2010" to a viewer.

**Consensus**: UE5 (default ON, Hi-Z ray march), Unity HDRP (default ON, Hi-Z pyramid), Godot 4 (default ON, screen-space march) — 3/3 engines have SSR as default-on.

**Technical approach** (based on HDRP Hi-Z method — best quality/cost ratio):

```
Step 1: Build Hi-Z mip chain (already exists: hiz_pyramid_pass.cpp)
Step 2: ssr_trace_cs.hlsl
  - Input: depth buffer (Hi-Z), gbuffer normals, gbuffer roughness, scene color
  - For each pixel with roughness < maxRoughness (0.8):
    - Compute reflection ray from view direction + normal
    - Ray march using Hi-Z mips (coarse → fine)
    - Output: hit UV + hit confidence
  - Reference: @ref McGuire 2014 "Efficient GPU Screen-Space Ray Tracing"

Step 3: ssr_resolve_cs.hlsl
  - Input: SSR hit buffer, scene color, BRDF LUT
  - Compute Fresnel weight at hit point
  - Blend SSR result with probe fallback: lerp(probe, ssr, confidence * fresnel)
  - Apply temporal accumulation (95% history weight)
  - Output: SSR contribution buffer

Step 4: Composite in deferred_lighting_ps.hlsl or post-process pass
  - Add SSR contribution BEFORE tonemap: finalColor += ssrContrib
```

**Key parameters**:
- `maxRoughness = 0.8` (disable SSR for very rough surfaces, too noisy)
- `maxSteps = 32` (Hi-Z allows fewer steps than linear march)
- `thickness = 0.1` (depth tolerance to prevent false hits)
- `useHistory = true` (temporal accumulation)

**Priority**: P0
**Effort**: 8-12 hours
**Visual impact**: TRANSFORMATIVE. All reflective surfaces will show accurate reflections.

---

### P2.2 — Implement Full TAA with Halton Jitter (MEDIUM, 6-8 hours)

**What**: Complete `taa_upscaler.cpp/h` with proper Halton sub-pixel jitter and a TAA resolve pass.

**Why**: TAA with Halton jitter is the foundation on which all temporal features rest. Contact shadows currently dither with `frameId * float2(47,17)` — this noise pattern should be resolved by TAA. Without TAA resolve, the temporal dithering shows as noise. SSR (when implemented) will need TAA for history accumulation.

**Consensus**: UE5, Unity HDRP (mandatory for temporal features), Godot 4 — 3/3 engines.

**Technical approach**:
```
Step 1: Generate Halton(2,3) sequence jitter per frame
  - jitter_x = halton(frame_index, 2) - 0.5  // [-0.5, +0.5] subpixel
  - jitter_y = halton(frame_index, 3) - 0.5
  - Apply to projection matrix jitter column in PerFrameCB

Step 2: TAA resolve pass
  - Input: current jittered frame, history buffer, motion vectors
  - Reproject history via motion vectors
  - Clamp history to current frame neighborhood (AABB clamp)
  - Blend: output = lerp(history, current, 0.1)  // 10% current frame
  - Output: anti-aliased frame to history buffer

Step 3: Remove jitter from TSR output
  - If TSR is active: feed TAA-resolved input to TSR
```

**Key parameters** (Unity HDRP reference):
- `stationaryBlending = 0.95` (95% history for static content)
- `motionAmplification = 100.0` (temporal scale for motion)
- `sharpness = 0.5` (output sharpness)

**Priority**: P1
**Effort**: 6-8 hours
**Visual impact**: Eliminates shimmer on all high-frequency edges, enables SSR temporal accumulation.

---

### P2.3 — Complete Depth of Field (MEDIUM, 4-6 hours)

**What**: Implement and enable the DOF pass. Parameters are declared but the pass is disabled (`r.DOF = false`).

**Why**: DOF is critical for cinematic shots, cutscenes, and any close-up material. Without it, everything from the near plane to the horizon is in equal focus, which looks wrong for close-up shots and prevents artistic composition control.

**Consensus**: UE5 (BokehDOF, GaussianDOF, CircleDOF), Unity HDRP (Bokeh + Gaussian), Godot 4 (CoC blur) — 3/3 engines.

**Technical approach** (Gaussian DOF — simplest correct implementation):
```
Step 1: Compute Circle of Confusion (CoC) per pixel
  - coc = abs(depth - focusDistance) / (depth * aperture)
  - Store in R16F texture

Step 2: Near/far separation
  - Near: pixels where depth < focusDistance (foreground blur)
  - Far: pixels where depth > focusDistance (background blur)

Step 3: Separable blur
  - Horizontal pass: blur by CoC radius
  - Vertical pass: same

Step 4: Composite
  - Blend sharp + blurred based on CoC
```

**Parameters (UE5 reference)**:
- `FocalDistance = 0.0` (disabled, camera at origin)
- `FocalRegion = 0.0` (in-focus zone width, cm)
- `NearTransitionRegion = 300 cm`
- `FarTransitionRegion = 500 cm`
- `Aperture = 4.0` (f-stop)

**Priority**: P1
**Effort**: 4-6 hours
**Visual impact**: Enables cinematic-quality shots and corrects "everything in focus" flatness.

---

### P2.4 — Add Color Grading (Per-Range Controls) (MEDIUM, 4-6 hours)

**What**: Implement per-range color grading controls (Shadows, Midtones, Highlights) as a pass before final output.

**Why**: All three reference engines have per-range grading as a primary artistic control. Without it, the only tonal control is ACES film parameters. Color grading is what separates a "correct" render from an "artistic" render.

**Consensus**: UE5 (FColorGradingSettings with Global/Shadows/Midtones/Highlights), Unity HDRP (saturation/contrast/temperature/tint + split controls), Godot 4 (tone grading pass) — 3/3 engines.

**Technical approach**:
```
Parameters to add:
  - saturation: float (1.0 default, [0, 2])
  - contrast: float (1.0 default, [0.5, 2])
  - temperature: float (0.0 default, [-100, 100])
  - shadowsColor: float3 (1,1,1 default)
  - midtonesColor: float3 (1,1,1 default)
  - highlightsColor: float3 (1,1,1 default)
  - shadowsEnd: float (0.3 default)
  - highlightsStart: float (0.55 default)

Implementation: Post-tonemap pass (operates on [0,1] LDR output)
```

**Priority**: P1
**Effort**: 4-6 hours
**Visual impact**: Enables look development. Without grading, every scene has the same "raw" tone.

---

## Phase 3: Advanced Features — Polish and Quality Ceiling

These features are important for AAA quality parity but require significant engineering effort. They are not blockers for the render looking "good" — they are what separates "good" from "exceptional".

---

### P3.1 — Volumetric Fog (Froxel-Based) (1-2 weeks)

**What**: Replace/supplement height fog with froxel-based volumetric fog.

**Why**: Froxel fog captures light scattering from all scene lights (not just directional), produces correct god-rays/light shafts, and responds to volumetric shadow maps. Height fog is flat and cannot produce these effects.

**Consensus**: UE5 (froxels, 64 depth slices, 256x256 XY), Unity HDRP (volume lighting), Godot 4 (volumetric fog) — 3/3 engines. UzEngine has height fog only.

**Technical approach** (UE5 reference):
- 3D texture: 256x256x64 or 160x90x64 slices
- Per-voxel: scatter + extinction coefficients
- Integration: front-to-back ray march, store accumulated transmittance + inscatter
- Temporal reprojection (75% history blend for stability)
- Light injection: directional + point lights inject into froxel grid

**Priority**: P2
**Effort**: 1-2 weeks

---

### P3.2 — Subsurface Scattering (Separable SSS) (1 week)

**What**: Implement separable SSS for skin, wax, and marble materials.

**Why**: Any scene with human characters will look like mannequins without SSS. Light should scatter through thin surfaces (ear lobes, nose tip, fingers).

**Consensus**: UE5 (Jimenez 2012 separable SSS), Unity HDRP (subsurface profile), Godot 4 (subsurface scattering texture) — 3/3 engines.

**Technical approach**:
- G-buffer: separate SSS mask texture
- Post-lighting separable blur (2-pass: H + V)
- SSS profile texture (1D kernel per profile)
- Blend SSS contribution with direct diffuse via SSS weight

**Priority**: P2
**Effort**: 1 week

---

### P3.3 — Anisotropic BRDF Extension (3-5 days)

**What**: Add tangent-space anisotropy to the GGX BRDF for brushed metals, woven cloth.

**Why**: Flat metallic surfaces (like brushed aluminum or carbon fiber) require directional roughness. Without anisotropy, all metallic surfaces have circular highlight shapes.

**Consensus**: UE5 (Anisotropy parameter, default 0.0), Unity HDRP (anisotropy texture), Godot 4 (anisotropy + flowmap) — 3/3 engines.

**Technical approach** (Godot reference pattern):
```hlsl
/// @ref Heitz 2014 "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"
half D_GGX_Anisotropic(half NoH, half3 H, half3 T, half3 B, half alpha_x, half alpha_y) {
    half TdotH = dot(T, H);
    half BdotH = dot(B, H);
    half a2 = alpha_x * alpha_y;
    half3 v = half3(alpha_y * TdotH, alpha_x * BdotH, a2 * NoH);
    half v2 = dot(v, v);
    half w2 = a2 / v2;
    return a2 * w2 * w2 * (1.0h / HALF_PI);
}
```

**Priority**: P2
**Effort**: 3-5 days

---

### P3.4 — Box Reflection Probes + Parallax Correction (3-5 days)

**What**: Add box-projection support to reflection probes and implement parallax correction.

**Why**: Interior scenes (rooms, corridors, hallways) require box probes to avoid the "standing inside a large sphere" distortion that sphere probes produce. Without parallax correction, reflections look like they're from a scene filmed elsewhere rather than the actual room.

**Consensus**: UE5 (BoxReflectionCapture with parallax correction), Unity HDRP (box projection = true default), Godot 4 (probe type = BOX) — 3/3 engines.

**Technical approach** (Unity HDRP reference):
```hlsl
// Box projection correction
float3 boxProjectedDir = R;  // reflection vector
[unroll] for (int i = 0; i < 3; ++i) {
    if (abs(R[i]) > 0.001) {
        float t = (R[i] > 0) ? (boxMax[i] - probePos[i]) / R[i]
                              : (boxMin[i] - probePos[i]) / R[i];
        boxProjectedDir[i] = t;
    }
}
float3 correctedR = normalize(float3(boxProjectedDir.x, boxProjectedDir.y, boxProjectedDir.z));
```

**Priority**: P2
**Effort**: 3-5 days

---

### P3.5 — Full ACES RRT + ODT Pipeline (1 week)

**What**: Replace the Narkowicz polynomial ACES fit with the full ACES Reference Rendering Transform (RRT) + Output Display Transform (ODT).

**Why**: The polynomial fit is a simplified approximation. The full ACES pipeline includes a glow module, red modifier, and precise ODT that produces the characteristic "film look" used in actual movie production. Narkowicz fits closely in midtones but diverges in highlights and shadows.

**Consensus**: UE5 (full ACES pipeline), Unity HDRP (ACES 1.0.3 reference). Note: Godot uses simplified version, so only 2/3 consensus.

**Technical approach**:
- Reference: `ampas/aces-dev` repository, `transforms/ctl/` directory
- Implement RRT → ODT pipeline as a compute pass
- Apply color space transforms: Linear sRGB → AP0 → RRT → AP1 → ODT → sRGB

**Priority**: P3
**Effort**: 1 week

---

## Immediate Action Checklist (Before Any Engineering Work)

The following changes can be made in under 30 minutes and will produce IMMEDIATE visual improvement:

```
[ ] 1. Change r.Exposure default: 1.4f → 1.0f
[ ] 2. Change r.SkyLight.Intensity default: 3.5f → 1.0f
[ ] 3. Change r.SkyLight.MinAmbient default: 0.1f → 0.02f
[ ] 4. Take screenshot before + after to verify improvement
```

These three changes cost 5 minutes and will produce more visual improvement than 1 week of new feature implementation.

---

## Effort / Priority Summary

| Item | Priority | Effort | Consensus | Expected Visual Gain |
|------|----------|--------|-----------|---------------------|
| Fix r.Exposure = 1.0 | P0 | 5 min | 3/3 | High — correct midtone balance |
| Fix r.SkyLight.Intensity = 1.0 | P0 | 5 min | 3/3 | High — restore shadow contrast |
| Fix r.SkyLight.MinAmbient = 0.02 | P0 | 5 min | 3/3 | Medium — darker shadow floors |
| Re-enable micro-shadows | P0 | 2-4 h | 3/3 | High — surface contact detail |
| Verify ACES film parameters | P1 | 1 h | 3/3 | Medium — tone curve accuracy |
| Contact shadow length tuning | P1 | 0.5 h | 3/3 | Low-medium |
| Implement SSR | P0 | 8-12 h | 3/3 | Transformative — reflective surfaces |
| Complete TAA with Halton jitter | P1 | 6-8 h | 3/3 | High — reduces shimmer, enables SSR TAA |
| Complete DOF | P1 | 4-6 h | 3/3 | Medium — cinematic shots |
| Color grading per-range | P1 | 4-6 h | 3/3 | Medium — artistic look development |
| Froxel volumetric fog | P2 | 1-2 w | 3/3 | High — atmospheric depth, light shafts |
| Subsurface scattering | P2 | 1 w | 3/3 | High — character skin realism |
| Anisotropic BRDF | P2 | 3-5 d | 3/3 | Medium — metal material quality |
| Box reflection probes + parallax | P2 | 3-5 d | 3/3 | Medium — interior scene quality |
| Full ACES RRT+ODT | P3 | 1 w | 2/3 | Low-medium — highlight rolloff |

---

## Implementation Order (Recommended)

```
Day 1, Morning (30 min):  Apply parameter fixes (P1.1, P1.2, P1.3)
Day 1, Afternoon (4 h):   Fix micro-shadow TODO (P1.3)
Day 2 (1 h):              Verify ACES parameters, tune contact shadow length (P1.4, P1.5)
Days 3-4 (8-12 h):        Implement SSR (P2.1)
Days 5-6 (6-8 h):         Complete TAA (P2.2)
Week 2 (4-6 h):           Complete DOF, add color grading (P2.3, P2.4)
Week 3+:                  Advanced features (P3.x)
```

This plan produces visible improvement on Day 1 within 30 minutes, and reaches AAA visual quality bar by end of Week 2.
