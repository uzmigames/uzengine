## 1. Shader SM5 Fallback Validation
- [x] 1.1 Audit all runtime shaders for SM5 compatibility (no SM6+ intrinsics in non-bindless path)
- [x] 1.2 Verify mesh_vs.hlsl SM5 path — vertex input via Input Assembler (already correct)
- [x] 1.3 Verify mesh_ps.hlsl SM5 path — register-bound textures t0-t4, s0 (already correct)
- [x] 1.4 Verify gbuffer_ps.hlsl SM5 path — register-bound textures (already correct)
- [x] 1.5 Verify deferred_lighting_ps.hlsl SM5 path — G-Buffer via t0-t3 (already correct)
- [x] 1.6 Verify shadow_depth_vs.hlsl SM5 path — IA input, b0/b2 CBs (already correct)
- [x] 1.7 Verify fullscreen_vs.hlsl SM5 path — no bindless deps (already correct)
- [x] 1.8 Verify gbuffer_debug_ps.hlsl SM5 path — register-bound t0-t3 (already correct)

## 2. SPIR-V (Vulkan) Compilation
- [x] 2.1 Verify DXC compiles all shaders with `-spirv` (SM6.0, no UZ_BINDLESS) — all 7 pass
- [x] 2.2 Verify no HLSL constructs incompatible with SPIR-V — none found
- [x] 2.3 Verify constant buffer binding compatibility — b0/b1/b2 map correctly

## 3. Constant Buffer Compatibility
- [x] 3.1 Verify PerFrameCB layout — 256B with static_assert, fields before padding match
- [x] 3.2 Verify PerObjectCB layout — descriptor indices in CB but only used when non-zero
- [x] 3.3 Verify ShadowCB layout — 512B with static_assert, cascade matrices + params

## 4. Testing
- [x] 4.1 Add shader compilation tests — test_shader_cross_backend.cpp (21 parameterized tests)
- [x] 4.2 Verify all 3 targets produce valid bytecode — DXIL/DXBC/SPIR-V magic numbers validated
- [x] 4.3 Test CB alignment — existing test_constant_buffers.cpp covers static_asserts

## 5. Documentation
- [x] 5.1 Update rendering docs with cross-backend shader compatibility notes
- [x] 5.2 Document shader compilation matrix (shader × target × defines)
