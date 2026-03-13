# Tasks: Shader Correctness Fixes

## 1. Critical — Register Mismatches
- [x] 1.1 Fix LightingCB register: align constant_buffers.h comment with actual b2 usage in deferred_lighting_ps.hlsl (F-01) ✅ comments updated
- [x] 1.2 ~~Fix ShadowCB register~~ NOT A BUG: root param 3 → ShaderRegister=4 (b4), mapping is correct (F-02)
- [x] 1.3 ~~Fix grid_ps.hlsl b1 collision~~ NOT A BUG: grid pass has isolated root signature, no collision (F-12)
- [x] 1.4 Fix overlay_vs.hlsl vs overlay_text_ps.hlsl root constant field naming mismatch (F-13) ✅ aligned to vertexOffset

## 2. Critical — PBR Math Fixes
- [x] 2.1 ~~Fix geometry term k~~ NOT A BUG: k=alpha/2 is correct for direct lights (Karis 2013), IBL needs separate function when added (F-06)
- [x] 2.2 ~~Add sRGB-to-linear decode~~ NOT A BUG: relies on hardware sRGB format conversion (GPU auto-decodes), correct design (F-05)
- [x] 2.3 ~~Fix cascade selection~~ NOT A BUG: Euclidean distance is valid for cascade selection, only naming misleading (viewDepth → rename later) (F-04)
- [ ] 2.4 Refactor cascade selection loop to remove confusing dual-assignment pattern (F-03)

## 3. High — Shader Quality
- [ ] 3.1 Create `runtime/shaders/runtime/include/pbr_common.hlsli` — extract shared NDF, Fresnel, geometry, Cook-Torrance functions (F-07)
- [ ] 3.2 Create `runtime/shaders/runtime/include/shadow_common.hlsli` — extract shared PCF, cascade selection (F-07)
- [ ] 3.3 Update mesh_ps.hlsl and deferred_lighting_ps.hlsl to use shared includes
- [ ] 3.4 Replace per-channel Reinhard with luminance-based Reinhard or ACES tonemapping (F-08)
- [x] 3.5 Fix tangent transformation: use normalMatrix instead of model matrix (F-14) ✅ mesh_vs.hlsl fixed
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
