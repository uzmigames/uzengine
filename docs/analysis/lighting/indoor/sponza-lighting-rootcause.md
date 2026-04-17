# Sponza Lighting Root Cause Analysis — Expanded

Date: 2026-04-08 (updated)
Scene: `sample/content/scenes/sponza.uzscene`
Reference: github.com/diharaw/hybrid-rendering (Sponza deferred path)

---

## Executive Summary

The Sponza scene appears flat, washed out, and warm-tinted compared to the hybrid-rendering reference.
The root causes are an **exposure/intensity calibration mismatch** that pushes all lit pixels onto
the shoulder of the Hill ACES tonemapper, a **double-multiply on the IBL ambient path** that raises
shadow-region brightness to compete with direct sun, and a **warm tint compounding** from scene
`sky_light.light_color` on top of an already-warm HDRI.

**There is NO bug in the shadow pipeline, the BRDF math, or the GBuffer.** The geometry, shadow cascades,
PCF sampling, and PBR shading are all correct. The problem is purely in the **lighting intensity
calibration** and **tonemap configuration**.

---

## Verified Data Points (this session)

### Diagnostic Output (`--diag --frames 120`)

| Target | Status | Min | Max | Avg | Notes |
|--------|--------|-----|-----|-----|-------|
| gbuffer.albedo | OK | 0.10 | 1.00 | 0.35 | Correct albedo range |
| gbuffer.normal | OK | 0.00 | 0.51 | 0.35 | Normal map encoded correctly |
| gbuffer.emissive | OK | 0.00 | 16.48 | 4.12 | Emissive stores packed data, not actual emissive color |
| shadow.atlas | OK | 0.00 | 0.00 | 0.00 | D32 depth readback artifact — NOT a real zero |
| gtao.result | OK | 0.80 | 0.82 | 0.81 | Very narrow AO range — barely occluding |
| hdr | OK | 0.25 | 1.00 | 0.52 | This is post-tonemap, NOT pre-tonemap HDR |

### SH Irradiance Values (runtime log, frame 100+)

```
SkyLight SH L0: R=0.8086 G=0.7195 B=0.8046
Scale: 1.50 (indirectLightingIntensity from SkyLightComponent)
Scaled SH L0: R=1.2129 G=1.0792 B=1.2069
```

Then multiplied by `skyTint = {1.0, 0.95, 0.88} * 1.5 = {1.5, 1.425, 1.32}` in the shader.
**Net SH irradiance at shader apply site: L0 ≈ {1.82, 1.54, 1.59}** — well above the [0,1] range.

### Exposure Pipeline (verified from logs)

```
Scene CVar: r.AutoExposure=0, r.Exposure=1.0
AutoExposure: disabled → m_lastExposure = pow(2, bias=0) = 1.0
perFrame.exposure = cv_Exposure->value = 1.0
Note: CVar default for r.Exposure is 3.0 (frame_constants.cpp:76)
Comment at frame_constants.cpp:417 incorrectly says "default 10.0"
```

### Tonemap Configuration

```
Scene CVar: r.Tonemap.Slope=1.0, r.Tonemap.Toe=0.0 (DEAD CODE under operator 1)
Default: r.TonemapOperator=1 (Hill ACES) — frame_postprocess.cpp:29
Hill ACES takes NO runtime parameters — fixed polynomial curve
```

---

## Quantitative Lighting Math

### Direct Sun (mid-gray stone pixel, NoL=0.5, shadow=1.0)

```
light_renderer.cpp:60 → sunColor = {1.0, 0.98, 0.95} * 80 = {80, 78.4, 76}
deferred_lighting_ps.hlsl:322 → diff = EvalFrostbiteDiffuse(diffColor=0.5, ...)
                                       ≈ diffColor/π ≈ 0.159 (Frostbite divides by π internally)
deferred_lighting_ps.hlsl:324 → Lo = (diff + spec) * lightColor * NoL * shadow
                                   ≈ 0.159 * 80 * 0.5 * 1.0 = 6.36 (per R channel, ignoring spec)
```

### Ambient (same pixel, under arch where shadow=0)

```
frame_constants.cpp:269 → SH scaled by shScale = skyIndirectIntensity = 1.5
Shader EvalSH_L2(N) → irradiance ≈ 1.21 (from logged L0)
deferred_lighting_ps.hlsl:752 → diffuseAmbient = (1-metallic) * irradiance * baseColor * (1-totalScatter)
                                                ≈ 1.0 * 1.21 * 0.5 * 0.95 = 0.575
deferred_lighting_ps.hlsl:828 → ambient = (diffuseAmbient * ao + specAmbient * specOcc) * skyTint
                                        ≈ (0.575 * 0.81 + ~0.05) * 1.5 = 0.77
```

### Pre-tonemap HDR values

| Pixel Location | Direct Lo | Ambient | Total Pre-TM | Notes |
|----------------|-----------|---------|--------------|-------|
| Sun-lit floor (shadow=1, NoL≈0.5) | 6.36 | 0.77 | **7.13** | Well above ACES knee |
| Shadowed arch (shadow=0) | 0.00 | 0.77 | **0.77** | Below ACES knee |
| Grazing angle (NoL≈0.1) | 1.27 | 0.77 | **2.04** | Mid-shoulder region |

### Post Hill-ACES tonemap (exposure=1.0)

```
ACESHillFit(7.13) ≈ 0.96   (sun-lit floor — nearly white)
ACESHillFit(0.77) ≈ 0.59   (shadowed arch — mid-gray)
ACESHillFit(2.04) ≈ 0.82   (grazing angle — light gray)
```

**Contrast ratio sun/shadow = 0.96/0.59 = 1.63:1** — should be ~3-5:1 for visible sun shafts.
The ACES shoulder compresses a 9.3:1 HDR ratio down to 1.6:1. This is the core visual problem.

### What the reference does differently

```
hybrid-rendering lighting.glsl: Li = light_color * light_intensity (intensity ~1-10)
hybrid-rendering tone_map.frag: aces_film(color * u_exposure) where exposure ≈ 0.1-0.5
Net pre-ACES value for sun-lit pixel: ~0.8-1.5 (WITHIN the linear region of ACES)
Net pre-ACES value for shadow pixel: ~0.1-0.3
Post-ACES contrast ratio: ~3-4:1
```

---

## Root Cause Bugs (ranked by visual impact)

### Bug 1 [P0]: Exposure/intensity calibration mismatch

**Symptom**: Flat, washed lighting with no visible sun shafts.

**Root cause**: Sun `intensity=80` produces pre-tonemap HDR values of ~6-7 for direct-lit surfaces.
With `exposure=1.0`, these values sit on the shoulder of Hill ACES where the curve is nearly flat
(derivative → 0). Shadow-region pixels at ~0.8 sit in the linear region. The curve compresses the
9:1 HDR contrast to 1.6:1 LDR contrast, destroying the visual separation between sun-lit and
shadowed areas.

**Evidence**:
- `light_renderer.cpp:60`: `sunColor = dirCol * dirLight.intensity` → {80, 78, 76}
- `frame_constants.cpp:76`: `r.Exposure` CVar default=3.0, scene override=1.0
- `frame_constants.cpp:418`: `perFrame.exposure = cv_Exposure->value` (when auto-exposure off)
- `tonemap_ps.hlsl:164`: `color *= exposure` (1.0 = no scaling)
- `tonemap_ps.hlsl:180`: `ACESHillFit(color)` — fixed curve, no parameters

**The calibration disconnect**: The engine CVar default for `r.Exposure` is 3.0, meaning the
intensity=80 was designed to be divided by 3.0 (or more with auto-exposure). The scene file forces
`r.Exposure=1.0` and `r.AutoExposure=0`, removing ALL compensation. Pre-tonemap HDR is 3× higher
than the pipeline was calibrated for.

**Fix**: The scene's `r.Exposure=1.0` override is wrong for intensity=80 sun. Either:
- (A) Remove `r.Exposure=1.0` from scene CVars → engine default 3.0 applies, pre-TM values drop to ~2.1 (better)
- (B) Set `r.Exposure=0.15` in scene → pre-TM ≈ 1.0 (matches reference calibration)
- (C) Divide sun intensity: `intensity=10` instead of 80, keep exposure=1.0

Option (C) is the cleanest — it brings UzEngine's intensity scale in line with the reference
(hybrid-rendering uses intensity ~1-10). Options A/B are exposure hacks that don't fix the
underlying scale mismatch.

### Bug 2 [P0]: skyTint double-multiplies IBL intensity

**Symptom**: Shadow regions too bright, competing with direct light.

**Root cause**: SH coefficients are scaled by `skyIndirectIntensity=1.5` on the CPU
(`frame_constants.cpp:269`), then the shader multiplies the result by
`skyTint = skyLightColor * skyLightIntensity = {1.5, 1.425, 1.32}` at the apply site
(`deferred_lighting_ps.hlsl:828`). The intensity component is applied TWICE:
once via `indirectLightingIntensity` in the SH scale, and again via `skyLightIntensity` in skyTint.

**Evidence**:
- `frame_constants.cpp:269`: `const float shScale = skyIndirectIntensity;` (= 1.5)
- `frame_constants.cpp:259`: `perFrame.skyLightIntensity *= cv_SkyLightIntensity->value;` (= 1.5 * 1.0)
- `deferred_lighting_ps.hlsl:398`: `skyTint = float3(skyLightColorR, skyLightColorG, skyLightColorB) * skyLightIntensity;`
- `deferred_lighting_ps.hlsl:828`: `ambient = (...) * skyTint;`
- Comment at cpp:265-266 says "the shader's line 881 already multiplies by skyLightIntensity,
  so we only scale SH by indirectLightingIntensity" — but `skyTint` at shader line 398 INCLUDES
  `skyLightIntensity`, which means the shader DOES multiply by `skyLightIntensity` again. The
  comment is acknowledging the double-multiply but the "only" is misleading — both scales apply.

**Intended behavior**: `skyLightIntensity` should control specular IBL brightness (radiance lookup),
while `indirectLightingIntensity` should control diffuse IBL brightness (SH irradiance). Currently
both multiply the diffuse ambient term because `skyTint` includes `skyLightIntensity`.

**Fix**: Change shader line 398 to NOT include `skyLightIntensity` in `skyTint`:
```hlsl
// BEFORE (double-multiply):
float3 skyTint = float3(skyLightColorR, skyLightColorG, skyLightColorB) * skyLightIntensity;

// AFTER (tint-only — intensity was already baked into SH coefficients):
float3 skyTint = float3(skyLightColorR, skyLightColorG, skyLightColorB);
```

### Bug 3 [P1]: Tonemap Slope/Toe/Shoulder CVars are dead code

**Symptom**: User adjusts `r.Tonemap.Slope/Toe` with no visible effect.

**Root cause**: `r.TonemapOperator=1` (Hill ACES) is a fixed polynomial curve that takes no
runtime parameters. The CB fields `_toneCurveSlope/Toe/Shoulder/BlackClip/WhiteClip` are uploaded
but never read by any operator in the switch statement.

**Evidence**:
- `frame_constants.cpp:47-51`: CVars uploaded to CB
- `tonemap_ps.hlsl:178-186`: `switch(tonemapOperator)` — none of the cases read those CB fields
- `sponza.uzscene:11-12`: `"r.Tonemap.Slope": 1.0, "r.Tonemap.Toe": 0.0` — no effect

**Fix**: Either wire a parametric filmic curve into a new case, or remove the dead CVars.
For matching the reference, switching to `r.TonemapOperator=2` (Narkowicz ACES) is recommended.

### Bug 4 [P1]: Warm sky_light.light_color compounds with warm HDRI

**Symptom**: Orange/pink tint on all indirect-lit surfaces.

**Root cause**: Scene sets `sky_light.light_color = {1.0, 0.95, 0.88}` (warm tint) on top of
`grasslands_sunset_4k.hdr` which is already warmly colored (sunset sky). The compound tint
produces a pink/salmon cast on stone surfaces.

**Evidence**:
- `sponza.uzscene:104-108`: `"light_color": [1.0, 0.95, 0.88]`
- Shader skyTint includes this color → multiplied into every ambient pixel

**Fix**: Set `light_color = [1.0, 1.0, 1.0]` when using colored HDRIs. The HDRI provides its
own color temperature.

### Bug 5 [P1]: GTAO barely contributes

**Symptom**: No visible contact shadows or ambient occlusion in corners.

**Root cause**: GTAO result is nearly uniform (avg=0.81, range [0.80, 0.82]). With
`r.GTAO.Intensity=0.5` and `r.GTAO.Radius=60cm`, the occlusion is too mild for Sponza's
large-scale geometry.

**Evidence**:
- `--diag` output: gtao.result avg=0.81 with 0.02 range
- Visual: GTAO pass dump shows uniform mid-gray with faint structure

**Fix**: This is a tuning issue, not a bug. Consider `r.GTAO.Intensity=1.0` and
`r.GTAO.Radius=120` for large interiors. But fix Bugs 1-2 first — GTAO impact is
secondary to the main calibration problem.

---

## NON-issues (verified correct)

1. **Shadow pipeline**: Sun direction math, cascade computation, atlas rendering, PCF sampling
   all correct. `shadow.atlas` `avg=0` is a D32 depth readback artifact in PassInspector.
2. **BRDF**: Frostbite diffuse + GGX specular + energy-conserving Fresnel all correct.
3. **GBuffer**: Albedo, normal, metallic, roughness correctly populated.
4. **HDRI SH projection**: SH IS projected from the actual HDRI file, NOT stale or procedural.
5. **Sun direction**: `[-60, 0, 0]` = 60° above horizon in +X direction. Correct.
6. **IBL not shadowed**: Correct per UE5/reference — ambient is independent of directional shadow.
7. **GTAO application**: Applied to diffuse ambient only, not direct light. Correct.

---

## Recommended Fix Sequence

Each step produces a measurable visual improvement and can be verified independently.

### Step 1: Fix intensity scale (Bug 1)

**File**: `sample/content/scenes/sponza.uzscene`

Change sun `intensity` from 80 to 10. This brings the direct lighting scale in line with the
hybrid-rendering reference where `light_intensity` is O(1-10).

```json
"intensity": 10
```

Expected: Pre-tonemap direct Lo drops from ~6.3 to ~0.8 per channel. Hill ACES maps this
to ~0.6, giving the sun-lit floor visible contrast against ambient (~0.59 → 0.6 vs 0.8).

Alternatively (if other scenes depend on intensity=80): set `r.Exposure=0.125` in scene CVars
to achieve the same pre-ACES value.

### Step 2: Fix skyTint double-multiply (Bug 2)

**File**: `runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl`

Line 398: Remove `skyLightIntensity` from `skyTint`. The SH was already scaled by
`skyIndirectIntensity` on the CPU, and `skyLightIntensity` should only affect specular IBL
(radiance cubemap), not diffuse ambient.

```hlsl
// Line 398 — BEFORE:
float3 skyTint = float3(skyLightColorR, skyLightColorG, skyLightColorB) * skyLightIntensity;

// Line 398 — AFTER:
float3 skyTint = float3(skyLightColorR, skyLightColorG, skyLightColorB);
```

Expected: Ambient drops by factor of 1.5 (from net ~0.77 to ~0.51). Shadow-region pixels
become darker, improving contrast with sun-lit areas.

### Step 3: Neutralize light_color for HDRI (Bug 4)

**File**: `sample/content/scenes/sponza.uzscene`

```json
"light_color": [1.0, 1.0, 1.0]
```

Expected: Removes the warm-on-warm compound tint. Stone surfaces appear neutral gray.

### Step 4: Switch tonemap operator (Bug 3)

**File**: `sample/content/scenes/sponza.uzscene`

Add CVar: `"r.TonemapOperator": 2` to match hybrid-rendering's Narkowicz ACES.
Or remove the scene's `r.Tonemap.Slope/Toe` overrides since they're dead code.

### Step 5: Remove dead CVars from scene

**File**: `sample/content/scenes/sponza.uzscene`

Remove `r.Tonemap.Slope` and `r.Tonemap.Toe` since they have no effect under any operator.

### Step 6: Tune GTAO (optional, after Steps 1-4)

Increase `r.GTAO.Intensity` to 1.0 and `r.GTAO.Radius` to 120 for stronger contact occlusion.

---

## Pipeline Trace (Complete)

### 1. Direct Sun Path

| Stage | File:Line | Data | Value |
|-------|-----------|------|-------|
| ECS entity | sponza.uzscene:29-37 | rotation `[-60,0,0]`, intensity 80, color `[1.0,0.98,0.95]` | |
| Direction calc | light_renderer.cpp:51 | `sunDir = (rotation * Forward).Normalized()` | ≈(0.5, 0, 0.866) |
| Color*intensity | light_renderer.cpp:60 | `sunColor = dirCol * intensity` | {80, 78.4, 76} |
| Upload to CB | frame_constants.cpp:394 | `perFrame.lightColor = {sunColor, 0}` | {80, 78.4, 76, 0} |
| Frostbite diff | deferred_lighting_ps.hlsl:322 | `EvalFrostbiteDiffuse(diffColor=0.5, ...)` | ≈0.159 |
| Lo direct | deferred_lighting_ps.hlsl:324 | `(diff+spec) * lightColor * NoL * shadow` | ≈6.36 |
| Exposure | tonemap_ps.hlsl:164 | `color *= exposure` | *1.0 (no change) |
| Contrast | tonemap_ps.hlsl:172 | `pow(x/0.18, 1.0)*0.18` | identity (contrast=1.0) |
| ACES Hill | tonemap_ps.hlsl:180 | `ACESHillFit(6.36)` | ≈0.96 |

### 2. Ambient/IBL Path

| Stage | File:Line | Data | Value |
|-------|-----------|------|-------|
| HDRI load | frame_constants.cpp:196-234 | `grasslands_sunset_4k.hdr` → SH projection | |
| Raw SH L0 | runtime log | `R=0.8086 G=0.7195 B=0.8046` | |
| SH scale | frame_constants.cpp:269 | `shScale = skyIndirectIntensity = 1.5` | |
| Scaled SH L0 | frame_constants.cpp:271-276 | `coeffs * 1.5` | R=1.21, G=1.08, B=1.21 |
| skyLightIntensity | frame_constants.cpp:173,259 | `sl.intensity * CVar = 1.5 * 1.0` | 1.5 |
| skyLightColor | frame_constants.cpp:174-176 | `{1.0, 0.95, 0.88}` | warm tint |
| skyTint (shader) | deferred_lighting_ps.hlsl:398 | `skyLightColor * skyLightIntensity` | {1.5, 1.425, 1.32} |
| EvalSH_L2 | deferred_lighting_ps.hlsl:651 | `irradiance ≈ 1.21` (from pre-scaled SH) | |
| diffuseAmbient | deferred_lighting_ps.hlsl:752 | `(1-met)*irrad*baseColor*(1-scatter)` | ≈0.575 |
| ambient | deferred_lighting_ps.hlsl:828 | `(diffAmb*ao + specAmb*specOcc) * skyTint` | ≈0.77 |
| **Double-multiply** | | `1.5 (SH scale) × 1.5 (skyTint)` | **net 2.25× on diffuse** |

### 3. Tonemap Path

| Stage | File:Line | Data | Value |
|-------|-----------|------|-------|
| CVar default | frame_postprocess.cpp:29 | `r.TonemapOperator=1` (Hill ACES) | |
| Exposure CVar | frame_constants.cpp:76 | `r.Exposure` default=3.0, scene=1.0 | |
| AutoExposure | auto_exposure.cpp:356-358 | disabled → returns pow(2,0)=1.0 | |
| CB exposure | frame_constants.cpp:418 | `perFrame.exposure = 1.0` | |
| Apply exposure | tonemap_ps.hlsl:164 | `color *= 1.0` | no-op |
| Saturation | tonemap_ps.hlsl:168 | `lerp(luma, color, 1.1)` | slight boost |
| Contrast | tonemap_ps.hlsl:172 | `pow(x/0.18, 1.0)*0.18 = x` | identity |
| Hill ACES | tonemap_ps.hlsl:180 | Fixed polynomial curve | |
| Dead CVars | | Slope/Toe/Shoulder never read | **bug** |

---

## Visual Evidence

| Screenshot | Path | Finding |
|------------|------|---------|
| Final output | `.screenshots/sponza_current.png` | Flat, washed, warm tint, no sun shafts |
| HDR pass dump | `.screenshots/pass-dump-f201/11-hdr.png` | Very dark (PNG clamps HDR >1.0) — confirms values >1.0 exist |
| Albedo GBuffer | `.screenshots/pass-dump-f201/00-gbuffer-albedo.png` | Dark albedo textures — expected for stone |
| GTAO result | `.screenshots/pass-dump-f201/10-gtao-result.png` | Uniform ~0.81 gray — barely any occlusion |
| Emissive | `.screenshots/pass-dump-f201/04-gbuffer-emissive.png` | All black — no emissive materials (correct) |

---

## Reference Comparison (hybrid-rendering)

| Aspect | hybrid-rendering | UzEngine | Delta |
|--------|------------------|----------|-------|
| Sun intensity | O(1-10) | 80 | **8-80× higher** |
| Exposure | `u_exposure ≈ 0.1-0.5` (push constant) | 1.0 (CVar) | **2-10× higher** |
| Pre-ACES direct | ~0.8-1.5 | ~6.4 | **4-8× higher** (on ACES shoulder) |
| Pre-ACES ambient | ~0.1-0.3 | ~0.77 | **2.5-7× higher** |
| Tonemap operator | Narkowicz ACES (`aces_film()`) | Hill ACES (AP1 matrices) | Different curve shape |
| IBL double-scale | No | Yes (SH scale + skyTint) | **Bug** |
| Sky tint on HDRI | No | Yes (warm {1,0.95,0.88}) | **Compound tint** |
| Contrast ratio (post-TM) | ~3-4:1 | ~1.6:1 | **Half expected contrast** |
