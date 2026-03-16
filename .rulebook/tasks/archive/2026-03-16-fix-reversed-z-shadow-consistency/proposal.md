# Proposal: Fix Reversed-Z Shadow Map Consistency (Phase 0 — Critical)

## Why

The reversed-Z migration (commit `b606ec9`) left shadow maps in an inconsistent state. While the architectural decision — shadow maps use standard-Z (0=near, 1=far) and main view uses reversed-Z (1=near, 0=far) — is correct and matches UE5, the implementation has critical bugs:

1. **Shadow depth PSO uses `Less` instead of `LessEqual`** — UE5 uses `CF_LessEqual` (ShadowDepthRendering.cpp:760). `Less` causes self-shadow acne on coplanar surfaces that `LessEqual` avoids.
2. **Magic number `1.0f` for shadow depth clear** — hardcoded instead of a named constant, making it indistinguishable from bugs when reviewing code alongside `kDepthFar = 0.0f`.
3. **`RHIRenderPassDepthAttachment::clearDepth` defaults to `1.0f`** — any new render pass that forgets to set clearDepth will silently use the wrong value for reversed-Z main view (should default to `kDepthFar = 0.0f`).
4. **No `OrthographicReversedZ()` in Mat4** — if any main-view code ever needs orthographic projection (debug views, UI, 2D overlays), it will silently get standard-Z and break.

These issues were identified via comprehensive analysis comparing UzEngine's implementation against UE5 5.6 source (`docs/analysis/rendering/ue5-reversed-z-shadow-maps.md`).

## What Changes

### Bug Fixes
1. Shadow depth PSO depth function: `Less` → `LessEqual` (both non-instanced and instanced pipelines)
2. `RHIRenderPassDepthAttachment::clearDepth` default: `1.0f` → `kDepthFar` (0.0f)

### New Constants
3. `kShadowDepthClear = 1.0f` — named constant for shadow map clear value (standard-Z far plane)
4. `kShadowDepthNear = 0.0f`, `kShadowDepthFar = 1.0f` — shadow-space depth constants

### New API
5. `Mat4::OrthographicReversedZ()` — reversed-Z orthographic projection for main-view use (UE5 `FReversedZOrthoMatrix` parity)

### Documentation
6. Update comments in shadow_cascade.cpp, pipeline_library.cpp, shadow_common.hlsli explaining WHY shadows use standard-Z while main view uses reversed-Z

## Impact
- Affected specs: `docs/specs/rendering/renderer.md`, `docs/analysis/rendering/ue5-reversed-z-shadow-maps.md`
- Affected code: `runtime/engine/rhi/rhi_types.h`, `runtime/engine/renderer/core/pipeline_library.cpp`, `runtime/engine/renderer/passes/lighting/shadow_pass.cpp`, `runtime/engine/renderer/shadows/shadow_cascade.cpp`, `runtime/engine/math/mat4.h`, `runtime/engine/math/mat4.cpp`, `runtime/shaders/runtime/include/shadow_common.hlsli`
- Breaking change: NO — all changes are bug fixes or additive APIs
- User benefit: Eliminates shadow acne artifacts, prevents future reversed-Z bugs in new passes
- Enables: Correct foundation for point/spot light shadows, virtual shadow maps, and any future depth-dependent passes
