## 1. Implementation

- [ ] 1.1 Implement contact shadow ray march in HLSL include (`contact_shadows.hlsli`) — `float ContactShadow(float3 viewPos, float3 lightDirView, int steps, float rayLength, float2 texelSize, Texture2D depthTex)`. March from viewPos along lightDirView, project each step to screen UV, compare ray depth vs scene depth. Return 0.0 if occluded, 1.0 if unoccluded.
- [ ] 1.2 Implement soft fade at ray end — last 25% of ray length: lerp shadow factor toward 1.0 (no shadow). Prevents hard cutoff artifact at max ray distance. `fadeFactor = smoothstep(0.75, 1.0, stepProgress)`.
- [ ] 1.3 Implement depth bias for contact shadows — skip first step (or add small offset along normal) to prevent self-shadowing on flat surfaces. Bias = 0.005 * viewDist.
- [ ] 1.4 Implement screen-space ray length mode — `contactShadowLength` in screen fraction (0.01 = 1% of screen diagonal). Convert to view-space ray length based on pixel depth. Matches UE5's screen-relative behavior.
- [ ] 1.5 Integrate into deferred lighting pass — after cascade shadow lookup, compute contact shadow for directional light. `finalShadow = min(cascadeShadow, contactShadow)`. Only compute for pixels that pass cascade shadow (optimization: skip already fully shadowed pixels).
- [ ] 1.6 Add per-light contact shadow support — each light in LightGPU struct gets `contactShadowLength` parameter. Point/spot lights: compute contact shadow along light-to-pixel direction. Directional: along light direction.
- [ ] 1.7 Add ContactShadowDesc parameters — enabled/disabled, stepCount (8 default), rayLength (0.01 screen fraction), intensity (1.0), bias (0.005).
- [ ] 1.8 Create contact shadow demo — objects sitting on a flat surface. Toggle contact shadows on/off. Show fine shadow detail at object bases and in small crevices.

## 2. Visual Verification

- [ ] 2.1 Screenshot: objects on surface with contact shadows — visible darkening at base/contact point
- [ ] 2.2 Screenshot: same scene without contact shadows — flat, floating look
- [ ] 2.3 Screenshot: fine detail — small objects/folds casting contact shadows CSM can't resolve
- [ ] 2.4 Screenshot: no false self-shadow on flat surfaces (bias working)
- [ ] 2.5 Regression: existing demos still work

## 3. Documentation

- [ ] 3.1 Document contact shadow parameters with UE5 equivalents
- [ ] 3.2 Add algorithm overview and performance characteristics
