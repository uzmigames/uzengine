# Reversed-Z Shadow Consistency — Spec Delta

## MODIFIED Requirements

### Requirement: Shadow Depth PSO Comparison Function
The shadow depth rendering PSO MUST use `RHICompareFunc::LessEqual` for its depth stencil comparison function, matching UE5 `CF_LessEqual` (ShadowDepthRendering.cpp:760). This applies to both non-instanced and instanced shadow depth pipelines.

#### Scenario: Coplanar surface does not self-shadow
Given a flat mesh rendered into the shadow map
When the same mesh is lit and shadow-sampled in the deferred pass
Then the surface MUST NOT exhibit self-shadow acne
And the shadow comparison MUST pass (return lit) for the surface's own depth

#### Scenario: Occluder behind surface produces shadow
Given a mesh A at depth 0.3 in shadow space and mesh B at depth 0.5 behind it
When mesh B is shadow-sampled against the shadow map
Then mesh B MUST be marked as shadowed (depth 0.5 >= stored depth 0.3)

### Requirement: Shadow Depth Clear Constant
The shadow map depth clear value MUST be defined as a named compile-time constant `kShadowDepthClear` in `rhi_types.h` with value `1.0f`. All shadow passes MUST use this constant instead of a magic number `1.0f`.

#### Scenario: Shadow atlas clear uses named constant
Given the shadow pass is about to render
When it clears the shadow depth atlas
Then it MUST call `ClearDepthStencil(texture, kShadowDepthClear, 0)` using the named constant

#### Scenario: Shadow render pass attachment uses named constant
Given a per-cascade depth-only render pass is configured
When the depth attachment clear value is set
Then it MUST use `kShadowDepthClear` (not a literal `1.0f`)

### Requirement: Shadow Depth Space Constants
The engine MUST define `kShadowDepthNear = 0.0f` and `kShadowDepthFar = 1.0f` as compile-time constants in `rhi_types.h`, separate from the main-view constants `kDepthNear = 1.0f` and `kDepthFar = 0.0f`.

#### Scenario: Constants distinguish main view from shadow depth space
Given code that reads depth constants
When it uses `kDepthFar` for main-view depth buffer clear
And uses `kShadowDepthClear` for shadow map clear
Then the values MUST be different (0.0f vs 1.0f) reflecting the two depth conventions

### Requirement: Default Depth Attachment Clear Value
The `RHIRenderPassDepthAttachment::clearDepth` field MUST default to `kDepthFar` (0.0f) so that new render passes default to reversed-Z semantics. Shadow passes MUST explicitly override this to `kShadowDepthClear`.

#### Scenario: New render pass inherits reversed-Z default
Given a developer creates a new `RHIRenderPassDesc` without setting `clearDepth`
When the render pass executes a depth clear
Then the clear value MUST be `kDepthFar` (0.0f), matching reversed-Z convention

#### Scenario: Shadow pass overrides default to standard-Z
Given the shadow pass creates its `RHIRenderPassDesc`
When it configures the depth attachment
Then it MUST explicitly set `clearDepth = kShadowDepthClear` (1.0f)

## ADDED Requirements

### Requirement: Reversed-Z Orthographic Projection
The engine MUST provide `Mat4::OrthographicReversedZ(float width, float height, float near, float far)` that maps near plane to z=1.0 and far plane to z=0.0, matching UE5 `FReversedZOrthoMatrix` (OrthoMatrix.h:65-82). The existing `Mat4::Orthographic()` MUST remain standard-Z for shadow map use.

#### Scenario: OrthographicReversedZ maps near to 1.0
Given a reversed-Z orthographic projection with near=1.0 and far=1000.0
When a point at the near plane is transformed
Then its z component after transform MUST equal 1.0

#### Scenario: OrthographicReversedZ maps far to 0.0
Given a reversed-Z orthographic projection with near=1.0 and far=1000.0
When a point at the far plane is transformed
Then its z component after transform MUST equal 0.0

#### Scenario: Standard Orthographic still maps near to 0.0
Given a standard orthographic projection (existing `Mat4::Orthographic`) with near=0.0 and far=100.0
When a point at the near plane is transformed
Then its z component after transform MUST equal 0.0
