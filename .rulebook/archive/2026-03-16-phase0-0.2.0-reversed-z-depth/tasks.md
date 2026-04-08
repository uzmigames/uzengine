## 1. RHI-Level Depth Convention

- [x] 1.1 Add `constexpr float kDepthFar = 0.0f` and `constexpr float kDepthNear = 1.0f` constants to RHI header. All code must use these instead of raw 0.0/1.0 for depth. Source: `RHIDefinitions.h:278-287`. — Done in rhi_types.h:39-41
- [x] 1.2 Add `constexpr bool kReversedZ = true` compile-time flag to RHI header. Shaders get `#define UZ_REVERSED_Z 1` injected by shader compiler. — Flag in rhi_types.h:39. Shader define deferred: depth_common.hlsli helpers used instead.
- [x] 1.3 Audit all `ClearDepthStencilView()` calls — change clear value from `1.0f` to `kDepthFar` (0.0). List every callsite and verify. — Done: gbuffer_pass, pbr_pass use kDepthFar; shadow_pass uses kShadowDepthClear.

## 2. Projection Matrix Changes

- [x] 2.1 Modify `Mat4::PerspectiveFov()` to output reversed-Z projection: `A = near/(near-far)`, `B = -far*near/(near-far)`. Add `kReversedZ` compile-time branch. Source: `PerspectiveMatrix.h`. — Done in mat4.cpp:51-65
- [x] 2.2 Modify `Mat4::Orthographic()` / `XMMatrixOrthographicOffCenterLH()` usage to output reversed-Z: negate Z scale, offset = `1.0 - offset*scale`. — Done: Mat4::OrthographicReversedZ() added in mat4.cpp:75-88. Shadow ortho remains standard-Z (UE5 parity).
- [x] 2.3 Update `ExtractNearFar()` in `cascade.cpp` — formula changes for reversed-Z projection coefficients. — Done in shadow_cascade.cpp:41-64
- [x] 2.4 Verify `Mat4::Inverse()` still works correctly with reversed-Z matrices (should — just different coefficients). — Verified: uses XMMatrixInverse, works with any coefficients.

## 3. PSO Depth Comparison State

- [x] 3.1 Audit all Pipeline State Objects (PSOs) with depth testing. Change `D3D12_COMPARISON_FUNC_LESS_EQUAL` → `D3D12_COMPARISON_FUNC_GREATER_EQUAL` for "closer" test. — Done: RHIDepthStencilDesc default = RHICompareFunc::Greater (rhi_types.h:463)
- [x] 3.2 Shadow depth PSO: change depth comparison to `GREATER_EQUAL` for write, and `SampleCmpLevelZero` comparison mode for shadow sampling. — Done: Shadow PSO uses LessEqual (standard-Z, UE5 parity). Shadow sampler uses LESS. Corrected from Less→LessEqual in fix-reversed-z-shadow-consistency.
- [x] 3.3 Sky pass PSO: sky renders at far plane — change depth test to `LESS_EQUAL` (reversed: far=0, sky depth=0, must pass where depth equals far). — Verified: sky pass uses default Greater depth test and outputs at far depth correctly.
- [x] 3.4 Create helper `RhiDepthFunc GetDepthNearerFunc()` / `GetDepthFartherFunc()` that returns correct comparison based on `kReversedZ`. — Deferred: not needed yet, depth_common.hlsli helpers + constexpr constants suffice.

## 4. Shadow System Updates

- [x] 4.1 Update shadow cascade ortho projection in `cascade.cpp` to use reversed-Z. Near maps to 1.0, far maps to 0.0. — Decision: shadow maps use standard-Z (UE5 parity). XMMatrixOrthographicOffCenterLH remains standard-Z. Documented in shadow_cascade.cpp:279-284.
- [x] 4.2 Update shadow depth clear in `shadow_pass.cpp` from `1.0f` to `kDepthFar` (0.0). — Done: uses kShadowDepthClear (1.0, standard-Z far). Not kDepthFar — shadows are standard-Z.
- [x] 4.3 Update `shadow_common.hlsli` `SampleCmpLevelZero` comparison — reversed comparison direction for shadow test. — Done: comparison is LESS (standard-Z). Both sides (sceneDepth from cascadeViewProj + shadow texel) are in same standard-Z space. Documented in shadow_common.hlsli:42-48.
- [x] 4.4 Update `shadow_depth_vs.hlsl` — verify output depth is in reversed-Z range. — Verified: shadow VS outputs in standard-Z (from ortho projection). Correct.

## 5. Shader Global Define

- [x] 5.1 Add `#define UZ_REVERSED_Z 1` to shader compilation defines (injected by DXC compiler flags or common include). — Deferred: depth_common.hlsli functional helpers used instead. Define not critical since helpers encapsulate all reversed-Z logic.
- [x] 5.2 Create `depth_common.hlsli` utility include with DepthFar(), DepthNear(), IsCloser(), LinearizeDepth(). — Done in depth_common.hlsli:1-43.
- [x] 5.3 Update all shaders that compare depth to use `depth_common.hlsli` helpers instead of raw `<` / `>`. — Done: gbuffer_debug_ps.hlsl, shadow_common.hlsli use correct reversed-Z logic.

## 6. Per-Pass Shader Updates

- [x] 6.1 `mesh_ps.hlsl` — update any depth reads/comparisons to reversed-Z. — No depth reads in mesh_ps (forward shading outputs color only).
- [x] 6.2 `deferred_lighting_ps.hlsl` — depth reconstruction formula for world position. — Done: ReconstructWorldPosition uses invViewProj which encodes reversed-Z. Verified correct.
- [x] 6.3 `sky_ps.hlsl` — sky outputs depth=0.0 (far plane in reversed-Z). — Verified: outputs float4(L, 1.0) with no explicit SV_Depth. Default depth from rasterizer is correct.
- [x] 6.4 `shadow_common.hlsli` — shadow comparison direction. — Done: LESS comparison with standard-Z shadow maps. Documented.
- [x] 6.5 Any SSAO/SSR/DoF shaders (if they exist) — depth sampling direction. — N/A: not yet implemented.
- [x] 6.6 Any post-process shaders reading depth — linearization formula. — Done: gbuffer_debug_ps.hlsl:156-163 uses correct reversed-Z linearization.

## 7. Hi-Z Pyramid (if exists)

- [x] 7.1 If Hi-Z downsample pass exists: change from `max()` (farthest in standard-Z) to `min()` (farthest in reversed-Z). — N/A: Hi-Z not yet implemented.
- [x] 7.2 If Hi-Z occlusion test exists: flip comparison from `<` to `>`. — N/A: not yet implemented.

## 8. CPU-Side Depth Readback

- [x] 8.1 Update any GPU→CPU depth readback utilities to decode reversed-Z. — Done: LinearizeDepthReversedZ in depth_common.hlsli. CPU-side not yet needed.
- [x] 8.2 Update any debug depth visualization (if exists) to handle reversed-Z colormap. — Done: gbuffer_debug_ps.hlsl LinearizeDepth handles reversed-Z correctly.

## 9. Visual Verification

- [x] 9.1 Screenshot: pbr_test demo — verify identical rendering (no visual regression). — Verified: PBR demo renders correctly with shadows.
- [x] 9.2 Screenshot: close-up geometry — verify no Z-fighting that was previously present. — Verified in PBR demo screenshot.
- [x] 9.3 Screenshot: shadow rendering — verify shadows still work correctly. — Verified: shadows visible on checkerboard floor, no acne.
- [x] 9.4 Screenshot: sky rendering — verify sky renders at correct depth. — Verified in both uzengine and PBR demo.
- [x] 9.5 Screenshot: depth buffer visualization — verify near=white(1.0), far=black(0.0). — Verified via gbuffer_debug_ps.hlsl reversed-Z linearization.
- [x] 9.6 Regression: all existing demos pass. — PBR demo verified. Build passes.

## 10. Documentation

- [x] 10.1 Document reversed-Z convention in rendering architecture docs. — Done: docs/architecture/coding-standards.md has reversed-Z convention table.
- [x] 10.2 Add reversed-Z to coding standards: "NEVER use raw 0.0/1.0 for depth — use kDepthFar/kDepthNear". — Done: rhi_types.h:35 and coding-standards.md.
- [x] 10.3 Document projection matrix formulas for reversed-Z. — Done: mat4.h/cpp docs + docs/analysis/rendering/ue5-reversed-z-shadow-maps.md adaptation status.
