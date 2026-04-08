## 1. ShadowConfig Extension

- [x] 1.1 Add `farShadowDistance` (float, default 300000.0f cm = 3km) to `ShadowConfig` in `cascade.h`. Source: `DirectionalLightComponent.cpp:1023`.
- [x] 1.2 Add `farShadowCascadeCount` (uint32_t, default 0 = disabled, max 4) to `ShadowConfig`. Source: `DirectionalLightComponent.h:112`.
- [x] 1.3 Add `farShadowLodBias` (int32_t, default 0) to `ShadowConfig`. Source: `DirectionalLightComponent.cpp:68`.
- [x] 1.4 Update `kMaxShadowCascades` from 4 to 8 to accommodate main(4) + far(4). Update all GPU arrays sized to this constant.

## 2. Far Cascade Split Computation

- [x] 2.1 In `ComputeCascades()`, after computing main cascade splits over `[shadowNear, mainMaxDistance]`, compute far cascade splits over `[mainMaxDistance, farShadowDistance]` using same exponential distribution formula.
- [x] 2.2 Only compute far cascades if `farShadowCascadeCount > 0` AND `(farShadowDistance - maxDistance) > 100.0f` (UE5 activation guard). Source: `DirectionalLightComponent.cpp:348`.
- [x] 2.3 Far cascade indices start at `mainCascadeCount`. Output `ShadowCascade` array contains `[main0..mainN, far0..farM]` contiguously.
- [x] 2.4 Mark far cascades with `ShadowCascade::bFarShadowCascade = true` flag. Add this bool to `ShadowCascade` struct.

## 3. Shadow Atlas Layout

- [x] 3.1 Expand shadow atlas to support `mainCascadeCount + farShadowCascadeCount` tiles. Options: (a) increase atlas size, (b) use NxM grid instead of 2x2, (c) variable-resolution tiles for far cascades.
- [x] 3.2 Update `GetCascadeTileOffset()` in `shadow_pass.cpp` to support >4 cascades. Generalize from 2x2 grid to NxM layout.
- [x] 3.3 Update atlas viewport/scissor calculation to handle the expanded layout.
- [x] 3.4 If atlas size changes, update `kShadowAtlasSize` in `scene_renderer.h` and recreate the atlas texture.

## 4. Far Cascade Rendering

- [x] 4.1 Update `AddShadowPass()` to render far cascades after main cascades. Same pipeline, same draw commands.
- [ ] 4.2 Implement LOD bias for far cascades: when `bFarShadowCascade=true`, apply `farShadowLodBias` to mesh LOD selection. This reduces geometry complexity for far shadows. <!-- BLOCKED: engine has no LOD system yet. farShadowLodBias is wired through ShadowPassDesc but DrawCommand has no LOD field. Will activate when LOD system (phase3a-3.38.0) is implemented. -->
- [x] 4.3 Add per-cascade LOD factor to `ShadowPassDesc` so the pass can request coarser geometry.

## 5. Shader Updates

- [x] 5.1 Update `ShadowDataCB` in `constant_buffers.h` to support up to 8 cascades: expand `cascadeViewProj[8]`, `cascadeSplits` to `Vec4 cascadeSplitsMain` + `Vec4 cascadeSplitsFar`, add `farCascadeCount`.
- [x] 5.2 Update `shadow_common.hlsli` cascade selection logic: if pixel depth > last main cascade split, search far cascades.
- [x] 5.3 Update `kCascadeOffsets[]` array to support 8 cascade atlas tile positions.
- [x] 5.4 Far cascades use same PCF filtering as main cascades (no quality reduction in filtering).

## 6. Scene Renderer Integration

- [x] 6.1 Update `scene_renderer.cpp` shadow config to pass `farShadowCascadeCount` and `farShadowDistance` from scene environment or directional light component.
- [x] 6.2 Upload far cascade data to expanded `ShadowDataCB`.
- [x] 6.3 Total cascade count for shader = `mainCascadeCount + farShadowCascadeCount`.

## 7. Visual Verification

- [ ] 7.1 Screenshot: scene with far shadows disabled (current behavior) — shadows end at 400m
- [ ] 7.2 Screenshot: same scene with 2 far cascades — shadows visible out to 3km
- [ ] 7.3 Screenshot: close-up — main cascade quality unchanged (no regression)
- [ ] 7.4 Screenshot: LOD bias effect — far cascade renders coarser geometry without artifacts
- [ ] 7.5 Regression: existing demos render correctly

## 8. Documentation

- [x] 8.1 Document far shadow cascade parameters with UE5 equivalents
- [x] 8.2 Document atlas layout for >4 cascades
- [x] 8.3 Performance guide: when to enable far shadows, cost expectations
