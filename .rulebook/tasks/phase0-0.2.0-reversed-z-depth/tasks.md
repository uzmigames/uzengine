## 1. RHI-Level Depth Convention

- [ ] 1.1 Add `constexpr float kDepthFar = 0.0f` and `constexpr float kDepthNear = 1.0f` constants to RHI header. All code must use these instead of raw 0.0/1.0 for depth. Source: `RHIDefinitions.h:278-287`.
- [ ] 1.2 Add `constexpr bool kReversedZ = true` compile-time flag to RHI header. Shaders get `#define UZ_REVERSED_Z 1` injected by shader compiler.
- [ ] 1.3 Audit all `ClearDepthStencilView()` calls — change clear value from `1.0f` to `kDepthFar` (0.0). List every callsite and verify.

## 2. Projection Matrix Changes

- [ ] 2.1 Modify `Mat4::PerspectiveFov()` to output reversed-Z projection: `A = near/(near-far)`, `B = -far*near/(near-far)`. Add `kReversedZ` compile-time branch. Source: `PerspectiveMatrix.h`.
- [ ] 2.2 Modify `Mat4::Orthographic()` / `XMMatrixOrthographicOffCenterLH()` usage to output reversed-Z: negate Z scale, offset = `1.0 - offset*scale`. Affects shadow cascade ortho projection.
- [ ] 2.3 Update `ExtractNearFar()` in `cascade.cpp` — formula changes for reversed-Z projection coefficients.
- [ ] 2.4 Verify `Mat4::Inverse()` still works correctly with reversed-Z matrices (should — just different coefficients).

## 3. PSO Depth Comparison State

- [ ] 3.1 Audit all Pipeline State Objects (PSOs) with depth testing. Change `D3D12_COMPARISON_FUNC_LESS_EQUAL` → `D3D12_COMPARISON_FUNC_GREATER_EQUAL` for "closer" test. List every PSO.
- [ ] 3.2 Shadow depth PSO: change depth comparison to `GREATER_EQUAL` for write, and `SampleCmpLevelZero` comparison mode for shadow sampling.
- [ ] 3.3 Sky pass PSO: sky renders at far plane — change depth test to `LESS_EQUAL` (reversed: far=0, sky depth=0, must pass where depth equals far).
- [ ] 3.4 Create helper `RhiDepthFunc GetDepthNearerFunc()` / `GetDepthFartherFunc()` that returns correct comparison based on `kReversedZ`.

## 4. Shadow System Updates

- [ ] 4.1 Update shadow cascade ortho projection in `cascade.cpp` to use reversed-Z. Near maps to 1.0, far maps to 0.0.
- [ ] 4.2 Update shadow depth clear in `shadow_pass.cpp` from `1.0f` to `kDepthFar` (0.0).
- [ ] 4.3 Update `shadow_common.hlsli` `SampleCmpLevelZero` comparison — reversed comparison direction for shadow test.
- [ ] 4.4 Update `shadow_depth_vs.hlsl` — verify output depth is in reversed-Z range.

## 5. Shader Global Define

- [ ] 5.1 Add `#define UZ_REVERSED_Z 1` to shader compilation defines (injected by DXC compiler flags or common include).
- [ ] 5.2 Create `depth_common.hlsli` utility include with:
  - `float DepthFar()` → returns 0.0
  - `float DepthNear()` → returns 1.0
  - `bool IsCloser(float a, float b)` → returns `a > b` for reversed-Z
  - `float LinearizeDepth(float rawDepth, float near, float far)` → reversed-Z formula
- [ ] 5.3 Update all shaders that compare depth to use `depth_common.hlsli` helpers instead of raw `<` / `>`.

## 6. Per-Pass Shader Updates

- [ ] 6.1 `mesh_ps.hlsl` — update any depth reads/comparisons to reversed-Z.
- [ ] 6.2 `deferred_lighting_ps.hlsl` — depth reconstruction formula for world position. Reversed-Z changes linearization.
- [ ] 6.3 `sky_ps.hlsl` — sky outputs depth=0.0 (far plane in reversed-Z). Verify depth test passes correctly.
- [ ] 6.4 `shadow_common.hlsli` — shadow comparison direction.
- [ ] 6.5 Any SSAO/SSR/DoF shaders (if they exist) — depth sampling direction.
- [ ] 6.6 Any post-process shaders reading depth — linearization formula.

## 7. Hi-Z Pyramid (if exists)

- [ ] 7.1 If Hi-Z downsample pass exists: change from `max()` (farthest in standard-Z) to `min()` (farthest in reversed-Z). This is critical for occlusion culling correctness.
- [ ] 7.2 If Hi-Z occlusion test exists: flip comparison from `<` to `>`.

## 8. CPU-Side Depth Readback

- [ ] 8.1 Update any GPU→CPU depth readback utilities to decode reversed-Z. `linearDepth = near / (rawDepth * (near - far) + far)` for reversed-Z perspective.
- [ ] 8.2 Update any debug depth visualization (if exists) to handle reversed-Z colormap.

## 9. Visual Verification

- [ ] 9.1 Screenshot: pbr_test demo — verify identical rendering (no visual regression)
- [ ] 9.2 Screenshot: close-up geometry — verify no Z-fighting that was previously present
- [ ] 9.3 Screenshot: shadow rendering — verify shadows still work correctly
- [ ] 9.4 Screenshot: sky rendering — verify sky renders at correct depth
- [ ] 9.5 Screenshot: depth buffer visualization — verify near=white(1.0), far=black(0.0)
- [ ] 9.6 Regression: all existing demos pass

## 10. Documentation

- [ ] 10.1 Document reversed-Z convention in rendering architecture docs
- [ ] 10.2 Add reversed-Z to coding standards: "NEVER use raw 0.0/1.0 for depth — use kDepthFar/kDepthNear"
- [ ] 10.3 Document projection matrix formulas for reversed-Z
