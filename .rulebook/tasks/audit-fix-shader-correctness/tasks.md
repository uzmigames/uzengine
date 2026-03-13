# Tasks: Shader Correctness Fixes

## 1. Critical — Register Mismatches
- [ ] 1.1 Fix LightingCB register: align constant_buffers.h comment with actual b2 usage in deferred_lighting_ps.hlsl (F-01)
- [ ] 1.2 Fix ShadowCB register: align shadow_depth_vs.hlsl b4 declaration with shadow_pass.cpp root slot 3 binding (F-02)
- [ ] 1.3 Fix grid_ps.hlsl b1 collision with engine-standard PerFrameCB at b1 (F-12)
- [ ] 1.4 Fix overlay_vs.hlsl vs overlay_text_ps.hlsl root constant field naming mismatch (F-13)

## 2. Critical — PBR Math Fixes
- [ ] 2.1 Fix geometry term k: change from `alpha / 2` (IBL) to `(roughness + 1)^2 / 8` (Karis 2013 direct lights) (F-06)
- [ ] 2.2 Add sRGB-to-linear decode for albedo texture samples before PBR math (F-05)
- [ ] 2.3 Fix cascade selection: use view-space Z (`mul(viewMatrix, posWS).z`) instead of Euclidean `length(posWS - camPos)` (F-04)
- [ ] 2.4 Refactor cascade selection loop to remove confusing dual-assignment pattern (F-03)

## 3. High — Shader Quality
- [ ] 3.1 Create `runtime/shaders/runtime/include/pbr_common.hlsli` — extract shared NDF, Fresnel, geometry, Cook-Torrance functions (F-07)
- [ ] 3.2 Create `runtime/shaders/runtime/include/shadow_common.hlsli` — extract shared PCF, cascade selection (F-07)
- [ ] 3.3 Update mesh_ps.hlsl and deferred_lighting_ps.hlsl to use shared includes
- [ ] 3.4 Replace per-channel Reinhard with luminance-based Reinhard or ACES tonemapping (F-08)
- [ ] 3.5 Fix tangent transformation: use normalMatrix instead of model matrix (F-14)
- [ ] 3.6 Clamp PCF kernel UVs to cascade atlas quadrant boundaries (F-15)

## 4. Medium — Cleanup
- [ ] 4.1 Remove hardcoded kNearPlane/kFarPlane from debug depth shader — read from PerFrameCB (F-09)
- [ ] 4.2 Fix fullscreen_vs.hlsl header comment to match actual bit-trick (F-10)
- [ ] 4.3 Add BC5 two-channel normal map reconstruction guard (F-11)
- [ ] 4.4 Add `[earlydepthstencil]` attribute to opaque pass pixel shaders

## 5. Testing
- [ ] 5.1 Add shader compilation tests for all modified shaders
- [ ] 5.2 Verify register bindings match between HLSL and C++ for every pass

## 6. Documentation
- [ ] 6.1 Document register binding convention in docs/engine/12-rendering/
- [ ] 6.2 Document shared .hlsli include structure
