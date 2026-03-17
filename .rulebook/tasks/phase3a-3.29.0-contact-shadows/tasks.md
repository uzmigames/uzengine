## 1. Implementation

- [x] 1.1 Implement contact shadow ray march in HLSL include (`contact_shadows.hlsli`) — `CastContactShadowRay` adapted from UE5 `ScreenSpaceShadowRayCast.ush:9-64`. Marches in clip/NDC space, samples depth buffer at each step, returns hit distance or -1. `InterleavedGradientNoise` from UE5 `RandomInterleavedGradientNoise.ush:11-18` for temporal dither. `ComputeContactShadow` wraps ray cast with screen-to-world scaling (UE5 `DeferredLightingCommon.ush:156-229`).
- [ ] 1.2 Implement soft fade at ray end — last 25% of ray length: lerp shadow factor toward 1.0 (no shadow). Prevents hard cutoff artifact at max ray distance. `fadeFactor = smoothstep(0.75, 1.0, stepProgress)`.
- [x] 1.3 Implement depth bias for contact shadows — UE5 uses `StartDepth != SampleDepth` self-intersection avoidance (ScreenSpaceShadowRayCast.ush:45) and `CompareToleranceScale=2.0` (DeferredLightingCommon.ush:68) tolerance band. Both implemented in `CastContactShadowRay`.
- [x] 1.4 Implement screen-space ray length mode — `contactShadowLength` in screen fraction, converted to world-space via `viewDepth * contactShadowLength`. Equivalent to UE5's `GetScreenRayLengthMultiplierForProjectionType` for perspective projection. Field added to PerFrameCB and PerFrameParams at offset 480.
- [x] 1.5 Integrate into deferred lighting pass — after cascade shadow lookup, `ComputeContactShadow` called for directional light. Result multiplied into `shadow` variable (`shadow *= contactShadow`), matching UE5 `DeferredLightingCommon.ush:221-222`.
- [ ] 1.6 Add per-light contact shadow support — each light in LightGPU struct gets `contactShadowLength` parameter. Point/spot lights: compute contact shadow along light-to-pixel direction. Directional: along light direction.
- [x] 1.7 Add ContactShadowDesc parameters — `contactShadowLength` field added to PerFrameCB (constant_buffers.h) and PerFrameParams (per_frame_params.h). Default 0.0 (off, UE5 parity: LightRendering.cpp:640). Step count hardcoded to 8 (UE5 parity: DeferredLightingCommon.ush:197). Casting intensity 1.0 (UE5 parity: LightRendering.cpp:641).
- [ ] 1.8 Create contact shadow demo — objects sitting on a flat surface. Toggle contact shadows on/off. Show fine shadow detail at object bases and in small crevices.

## 2. Visual Verification

- [ ] 2.1 Screenshot: objects on surface with contact shadows — visible darkening at base/contact point
- [ ] 2.2 Screenshot: same scene without contact shadows — flat, floating look
- [ ] 2.3 Screenshot: fine detail — small objects/folds casting contact shadows CSM can't resolve
- [ ] 2.4 Screenshot: no false self-shadow on flat surfaces (bias working)
- [ ] 2.5 Regression: existing demos still work

## 3. Documentation

- [x] 3.1 Document contact shadow parameters with UE5 equivalents — `docs/guides/contact-shadows.md` with full UE5 source references, constants table, deviations list.
- [x] 3.2 Add algorithm overview and performance characteristics — included in guide doc: 8-step ray march, IGN dithering, 0.1-0.3ms estimated cost.
