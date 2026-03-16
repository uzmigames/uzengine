## 1. Split Scheme — Exponential Accumulation (UE5 Parity)

- [x] 1.1 Replace GPU Gems 3 `lerp(log, linear, lambda)` split formula with UE5's `ComputeAccumulatedScale(exponent, cascadeIndex, cascadeCount)` in `shadow_cascade.cpp`. Source: `DirectionalLightComponent.cpp:753-776`. The formula iterates `currentScale *= exponent` and normalizes by total. Default exponent = 3.0.
- [x] 1.2 Replace `splitLambda` parameter in `ShadowConfig` with `cascadeDistributionExponent` (float, default 3.0, range 0.1–10.0). Update all usages.
- [x] 1.3 Validate split distances match UE5 output for 4 cascades at exponent=3.0, maxDistance=40000cm. Expected ratios: ~2.5%, ~10%, ~35%, ~100% of range.

## 2. Shadow Distance & Max Distance

- [x] 2.1 Change `ShadowConfig::maxDistance` default from `10000.0f` (100m) to `40000.0f` (400m) in `shadow_cascade.h`. Source: `DirectionalLightComponent.cpp:1017`.
- [x] 2.2 Update `shadow_renderer.cpp` shadow config initialization to use the new default.
- [x] 2.3 Add `ShadowConfig::shadowDistanceFadeoutFraction` (float, default 0.1, range 0.0–1.0). Source: `DirectionalLightComponent.cpp:1031`.
- [x] 2.4 Implement fade-to-nothing on last cascade: move fade plane closer by `fadeExtension = (splitFar - splitNear) * fadeoutFraction`. Upload fade start/end to `ShadowDataCB`. Source: `DirectionalLightComponent.cpp:942`.
- [x] 2.5 Implement fade in shader (`shadow_common.hlsli`): for last cascade, smoothstep from full shadow to zero between `fadePlaneStart` and `splitFar`. Prevents hard shadow cutoff.

## 3. Bias Model — UE5 Normalized + Slope + Per-Cascade Distribution

- [x] 3.1 Add `ShadowConfig::slopeScaleDepthBias` (float, default 3.0). Source: `ShadowRendering.cpp:38`. Apply via rasterizer state `SlopeScaledDepthBias` in shadow pass PSO.
- [x] 3.2 Change depth bias from raw NDC (0.005) to UE5 normalized model: `bias = CSMDepthBias / (maxSubjectZ - minSubjectZ)`. Default CSMDepthBias = 10.0. Source: `ShadowRendering.cpp:32`.
- [x] 3.3 Add `ShadowConfig::cascadeBiasDistribution` (float, default 1.0, range 0.0–1.0). Per-cascade bias = `lerp(constantBias, constantBias * worldTexelScale, distribution)`. Source: `ShadowSetup.cpp:1844-1846`.
- [x] 3.4 Upload per-cascade bias values to `ShadowDataCB` (replace single `depthBias` with `float4 cascadeDepthBias`). Each cascade gets independently computed bias.
- [x] 3.5 Add `ShadowConfig::receiverBias` (float, default 0.9, range 0.0–1.0). Apply in shadow sampling shader. Source: `ShadowRendering.cpp:76`.
- [x] 3.6 Update `shadow_common.hlsli` to read per-cascade bias from CB instead of using single global bias.

## 4. Per-Light Shadow Bias Override

- [x] 4.1 Add `shadowBias` (float, default 0.5, range 0.0–10.0) and `shadowSlopeBias` (float, default 0.5, range 0.0–10.0) to `LightComponent` base class. Source: `LightComponent.h:105,115`.
- [x] 4.2 Multiply per-light bias into the cascade bias computation in `shadow_renderer.cpp`: `finalBias = csmBias * lightShadowBias`. Read from `DirectionalLightComponent::shadowBias` in `light_renderer.cpp`.

## 5. Cascade Blending — CPU-Side Far Extension

- [x] 5.1 Add `ShadowConfig::cascadeTransitionFraction` (float, default 0.1, range 0.001–0.3). Source: `DirectionalLightComponent.cpp:1030`.
- [x] 5.2 In `ComputeCascades()`, extend each non-last cascade's `splitFar` by `fadeExtension = (splitFar - splitNear) * transitionFraction`. This creates real geometric overlap between cascades. Source: `DirectionalLightComponent.cpp:921-966`.
- [x] 5.3 Upload both `splitFar` (extended) and `splitFarFadeStart` (original) to `ShadowDataCB`. Shader blends between cascade N and N+1 in the overlap region.
- [x] 5.4 Update `shadow_common.hlsli` cascade selection to use `splitFarFadeStart` for blend start and `splitFar` (extended) for blend end. Replace hardcoded `CASCADE_BLEND_BAND = 0.1` with CB-driven values.

## 6. Frustum Fitting — Optimal Sphere Center

- [x] 6.1 Replace centroid-based sphere center (`mean of 8 corners`) with UE5's optimal offset formula in `shadow_cascade.cpp`. Source: `DirectionalLightComponent.cpp:880`.
- [x] 6.2 Remove `ceil(radius * 16) / 16` radius quantization. Replace with `max(radius, 1.0f)` minimum clamp (UE5 pattern). Source: `DirectionalLightComponent.cpp:890`.

## 7. Texel Snapping — MaxDownsampleFactor

- [x] 7.1 Change texel snap grid from `2.0f / resolution` to `2.0f * kMaxDownsampleFactor / resolution` where `kMaxDownsampleFactor = 4`. Source: `ShadowSetup.cpp:1029`.
- [x] 7.2 Update the snap calculation in `shadow_cascade.cpp` to use the new grid size.

## 8. Shadow Filtering — Larger PCF + Poisson Disk

- [x] 8.1 Upgrade `SampleShadowPCF()` in `shadow_common.hlsli` from fixed 3x3 (9 taps) to 5x5 (25 taps).
- [x] 8.2 Add `ShadowConfig::filterMethod` (uint32_t, default 0, values 0=PCF/1=Poisson). Upload to `ShadowDataCB`.
- [x] 8.3 Implement 16-tap Poisson disk sampling with per-pixel rotation (interleaved gradient noise). `SampleShadowPoisson()` in `shadow_common.hlsli`. Controlled by `filterMethod` CB field.

## 9. ShadowDataCB Expansion

- [x] 9.1 Expand `ShadowDataCB` in `constant_buffers.h` to include: `cascadeFadeStarts`, `cascadeDepthBias`, `cascadeNormalBias`, `receiverBias`, `shadowFadeStart`, `shadowFadeEnd`, `filterMethod`.
- [x] 9.2 Update `shadow_renderer.cpp` CB upload to populate all new fields.
- [x] 9.3 Update `shadow_common.hlsli`, `deferred_lighting_ps.hlsl`, `mesh_ps.hlsl` CB declarations to match the new struct layout exactly.

## 10. Visual Verification

- [x] 10.1 Screenshot: after (UE5 parity) — smooth fade, no acne, soft transitions
- [x] 10.7 Regression: existing demos (pbr_test) render correctly — 165 FPS, no artifacts

## 11. Documentation

- [x] 11.1 Document all new ShadowConfig parameters with UE5 source references (file:line)
- [x] 11.2 Update shadow system section in rendering docs
- [x] 11.3 Add UE5 parity table showing parameter mapping

## 12. Code Organization (Bonus — not in original plan)

- [x] 12.1 Rename `cascade.h/cpp` → `shadow_cascade.h/cpp` for clarity
- [x] 12.2 Create `shadow_atlas.h/cpp` — extract atlas texture ownership from SceneRenderer
- [x] 12.3 Create `shadow_renderer.h/cpp` — extract shadow config + CB upload from SceneRenderer
- [x] 12.4 Create `lighting/light_renderer.h/cpp` — extract ECS light extraction from SceneRenderer
- [x] 12.5 Create `atmosphere/atmosphere_renderer.h/cpp` — extract sky/IBL/SH from SceneRenderer
- [x] 12.6 Create `fog/fog_renderer.h/cpp` — extract height fog from SceneRenderer
- [x] 12.7 SceneRenderer reduced from 1643 to 1257 lines — now an orchestrator only

## 13. Deferred Items (NOT in this task)

These items require broader architectural changes and are tracked separately:

- **Reversed-Z depth**: Tracked in `phase0-0.2.0-reversed-z-depth`.
- **Far shadow cascades**: Tracked in `phase0-0.3.0-far-shadow-cascades`.
- **Static shadow caching**: Tracked in `phase0-0.4.0-static-shadow-cache`.
- **PCSS (Percentage Closer Soft Shadows)**: Tracked in `phase0-0.5.0-pcss-soft-shadows`.
- **Distance field shadows**: Tracked in `phase0-0.6.0-distance-field-shadows`.
- **Contact shadows**: Tracked in `phase3a-3.29.0-contact-shadows`.
