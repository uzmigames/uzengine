# Tasks: Test Coverage Expansion

## 1. Infrastructure
- [ ] 1.1 Create DX12 test fixture: shared device/command list setup/teardown for GPU tests
- [ ] 1.2 Create mock RHI backend for testing renderer without GPU hardware
- [ ] 1.3 Set up golden image comparison utilities for render output validation

## 2. Critical — Broken Tests
- [ ] 2.1 Rewrite test_vector3.cpp for new Vec3 API (T-02)
- [ ] 2.2 Rewrite test_vector4.cpp for new Vec4 API (T-02)
- [ ] 2.3 Rewrite test_quaternion.cpp for new Quat API (q.x not q.X()) (T-02)
- [ ] 2.4 Rewrite test_mat4.cpp for new Mat4 API (T-02)
- [ ] 2.5 Rewrite test_transform.cpp for new Transform API (T-02)
- [ ] 2.6 Rewrite test_color.cpp / test_bounds.cpp if they exist (T-02)

## 3. Critical — Zero Coverage Modules
- [ ] 3.1 Add GBuffer pass tests via mock RHI (T-01)
- [ ] 3.2 Add Lighting pass tests via mock RHI (T-01)
- [ ] 3.3 Add Shadow pass tests via mock RHI (T-01)
- [ ] 3.4 Add PBR pass tests via mock RHI (T-01)
- [ ] 3.5 Add shader compilation tests — compile every .hlsl and verify success (T-04)
- [ ] 3.6 Add full render pipeline integration test (T-03)

## 4. High — Coverage Gaps
- [ ] 4.1 Add RHI swapchain creation/resize/present tests
- [ ] 4.2 Add RHI command list recording tests
- [ ] 4.3 Add RHI PSO cache hit/miss tests
- [ ] 4.4 Add RHI resource destruction/leak tests
- [ ] 4.5 Add core/paths tests (GetExeDir, ResolveEngineResource)
- [ ] 4.6 Add core/config tests
- [ ] 4.7 Add core/profiling scope tests

## 5. Verification
- [ ] 5.1 Run full test suite and verify all tests pass
- [ ] 5.2 Measure coverage and report delta toward 95% target
