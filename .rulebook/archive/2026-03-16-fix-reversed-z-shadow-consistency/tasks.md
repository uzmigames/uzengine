## 1. Implementation — P0 (Shadow Acne Fix)
- [x] 1.1 Change shadow depth PSO `depthFunc` from `RHICompareFunc::Less` to `RHICompareFunc::LessEqual` in `pipeline_library.cpp:467` (non-instanced)
- [x] 1.2 Change instanced shadow depth PSO `depthFunc` from `RHICompareFunc::Less` to `RHICompareFunc::LessEqual` in `pipeline_library.cpp:580`

## 2. Implementation — P1 (Named Constants & Safe Defaults)
- [x] 2.1 Add `kShadowDepthClear = 1.0f`, `kShadowDepthNear = 0.0f`, `kShadowDepthFar = 1.0f` constants to `rhi_types.h` alongside existing `kDepthFar`/`kDepthNear`
- [x] 2.2 Replace `1.0f` magic number in `shadow_pass.cpp:77` (`ClearDepthStencil`) with `kShadowDepthClear`
- [x] 2.3 Replace `1.0f` magic number in `shadow_pass.cpp:111` (`clearDepth`) with `kShadowDepthClear`
- [x] 2.4 Change `RHIRenderPassDepthAttachment::clearDepth` default from `1.0f` to `kDepthFar` in `rhi_types.h`
- [x] 2.5 Verify all existing render passes that set `clearDepth` explicitly still compile and behave correctly after default change

## 3. Implementation — P1 (Orthographic Reversed-Z)
- [x] 3.1 Add `Mat4::OrthographicReversedZ(float width, float height, float near, float far)` to `mat4.h` and `mat4.cpp`
- [x] 3.2 Document that `Mat4::Orthographic()` is standard-Z (for shadows) and `Mat4::OrthographicReversedZ()` is for main-view use

## 4. Implementation — P2 (Documentation & Comments)
- [x] 4.1 Update comment in `shadow_cascade.cpp:279` to explain WHY shadows use standard-Z while main view uses reversed-Z
- [x] 4.2 Update comment in `pipeline_library.cpp:467,580` to reference UE5 `CF_LessEqual` source line
- [x] 4.3 Update comment in `shadow_common.hlsli:40-41` to document the full depth-space flow (light-space ortho standard-Z → SampleCmp LESS → both sides in same space)
- [x] 4.4 Update comment in `shadow_common.hlsli:176` to explain the `0.99999` clamp (avoids false positive against clear=1.0)

## 5. Testing
- [x] 5.1 Unit test: `Mat4::OrthographicReversedZ()` maps near plane to z=1.0 and far plane to z=0.0
- [x] 5.2 Unit test: `Mat4::Orthographic()` (standard-Z) maps near plane to z=0.0 and far plane to z=1.0
- [x] 5.3 Unit test: `kShadowDepthClear == 1.0f`, `kShadowDepthNear == 0.0f`, `kShadowDepthFar == 1.0f` (compile-time constant verification)
- [x] 5.4 Visual test: PBR demo screenshot — shadows render correctly, no shadow acne on flat surfaces
- [x] 5.5 Visual test: shadow cascade transitions smooth, no hard lines between cascades
- [x] 5.6 Verify 95%+ coverage on all new code (29 new tests: 13 DepthConventionConstants + 16 Mat4Ortho)

## 6. Documentation
- [x] 6.1 Update `docs/analysis/rendering/ue5-reversed-z-shadow-maps.md` with UzEngine adaptation status (which items are now fixed)
- [x] 6.2 Add reversed-Z convention table to `docs/architecture/coding-standards.md` (main view: reversed-Z, shadows: standard-Z, constants to use)
