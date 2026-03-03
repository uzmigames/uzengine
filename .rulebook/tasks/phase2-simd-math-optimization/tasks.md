## 1. SIMD Capability Detection

- [ ] 1.1 Create `src/engine/math/simd_caps.h` with `SimdCaps::Detect()` using `__cpuid` intrinsic at startup to set flags `has_sse41`, `has_avx`, `has_avx2` in a static struct
- [ ] 1.2 Emit `UZ_LOG_INFO("SIMD: SSE4.1=%d AVX=%d AVX2=%d", ...)` at engine startup and `UZ_LOG_WARN("AVX2 not available, falling back to SSE4.1")` when AVX2 is absent
- [ ] 1.3 Add compile-time CMakeLists.txt target properties `/arch:AVX2` (MSVC) and `-mavx2 -mfma` (GCC/Clang) for the `engine_math` target; verify the build still succeeds with these flags before proceeding

## 2. Vec3, Vec4, Mat4 Wrapper Types

- [ ] 2.1 Replace the body of `src/engine/math/vec3.h` with a thin wrapper around `DirectX::XMFLOAT3` storage, adding `alignas(16)` to the struct and `ToXMVECTOR()` / `FromXMVECTOR()` conversion helpers
- [ ] 2.2 Replace the body of `src/engine/math/vec4.h` with a wrapper around `DirectX::XMFLOAT4` with `alignas(16)`, `ToXMVECTOR()`, and `FromXMVECTOR()` helpers
- [ ] 2.3 Replace the body of `src/engine/math/mat4.h` with a wrapper around `DirectX::XMFLOAT4X4` with `alignas(16)`, `ToXMMATRIX()`, and `FromXMMATRIX()` helpers; add `alignas(32)` for Mat4 array declarations (`alignas(32) Mat4 arr[N]`)
- [ ] 2.4 Add `static_assert(sizeof(Vec4) == 16 && alignof(Vec4) == 16, "Vec4 must be 16-byte aligned")` in `vec4.h`
- [ ] 2.5 Add `static_assert(sizeof(Mat4) == 64 && alignof(Mat4) == 16, "Mat4 must be 16-byte aligned")` in `mat4.h`
- [ ] 2.6 Provide accessor methods `x()`, `y()`, `z()`, `w()` as `float` getters and setters on all vector types to preserve call-site compatibility; do NOT use direct `.x` member access in new code

## 3. Compatibility Shim

- [ ] 3.1 Create `src/engine/math/math_compat.h` providing `#define` aliases mapping `.x` -> `.x()`, `.y` -> `.y()`, `.z` -> `.z()`, `.w` -> `.w()` for a transitional period; document in a comment that this shim will be removed in the subsequent release
- [ ] 3.2 Run the provided migration script `tools/migrate_math_access.py` against the entire `src/` tree to convert all existing `.x/.y/.z/.w` member accesses to accessor method calls; commit the result as a separate "math migration" commit before the optimization work

## 4. Transform Propagation SIMD Rewrite

- [ ] 4.1 Rewrite the scene graph transform propagation loop in `src/engine/scene/scene_graph.cpp` to load each node's local matrix into `XMMATRIX` via `XMLoadFloat4x4`, multiply with parent world matrix using `XMMatrixMultiply`, and store with `XMStoreFloat4x4` in breadth-first traversal order
- [ ] 4.2 Lay out the transform component in breadth-first order in the ECS component array before the propagation pass to maximize cache line sequential read access during the loop

## 5. ECS SoA Transform Layout

- [ ] 5.1 Restructure `src/engine/scene/components/transform_component.h` to use SoA layout: separate `alignas(32) float world_mat[MAX_ENTITIES][16]` arrays instead of a per-entity struct containing a `Mat4`, enabling sequential SIMD loads across entity indices
- [ ] 5.2 Update `src/engine/scene/scene_graph.cpp` to index into the SoA arrays directly; add a `static_assert` verifying `MAX_ENTITIES` is a multiple of 8 to ensure AVX2 256-bit loads never straddle a non-multiple-of-8 boundary

## 6. Frustum Culling SIMD Rewrite

- [ ] 6.1 Rewrite the frustum culling loop in `src/engine/renderer/culling.cpp` to load each entity's AABB min/max as `XMVECTOR` via `XMLoadFloat3` and test against all 6 frustum planes using `XMVector3Dot` and `XMVectorGreaterOrEqual` in a branch-free inner loop
- [ ] 6.2 Precompute frustum plane normals as `XMVECTOR` once per frame before entering the entity loop; do not reload them inside the loop

## 7. CPU Skinning Fallback SIMD Rewrite

- [ ] 7.1 Rewrite the CPU-side skinning fallback in `src/engine/renderer/skinning_pass.cpp` to use `XMVector3TransformNormal` for normals and `XMVector3Transform` for positions in the joint-weight accumulation loop
- [ ] 7.2 Ensure the skinning matrices are stored as `alignas(16) XMFLOAT4X4` arrays so the SIMD load path uses aligned `XMLoadFloat4x4` rather than the unaligned variant

## 8. Microbenchmarks

- [ ] 8.1 Create `tests/bench/math_bench.cpp` using Google Benchmark with fixture `TransformPropagationBench` parameterized over entity counts `{1000, 10000, 100000}`, measuring wall-clock time for one full scene graph propagation pass
- [ ] 8.2 Add a `ScalarTransformBench` using the old scalar `Mat4 * Mat4` implementation (kept in a separate `mat4_scalar.h` for comparison) to establish the baseline throughput
- [ ] 8.3 Add a `FrustumCullBench` parameterized over `{1000, 10000, 100000}` entities, measuring one full frustum cull pass
- [ ] 8.4 Document benchmark results in `docs/specs/renderer.md` with the measured speedup ratios on the development machine spec

## 9. Testing

- [ ] 9.1 Create `tests/math/test_simd_caps.cpp` verifying `SimdCaps::Detect()` populates at least one flag on any x86-64 machine (SSE4.1 is baseline); verify the function can be called multiple times without error
- [ ] 9.2 Test `Vec4` and `Mat4` alignment assertions fire at compile time when `alignas` is removed (use a negative-compilation test or `static_assert` in a test file)
- [ ] 9.3 Test `XMMatrixMultiply` result stored via `Mat4::FromXMMATRIX` matches the scalar reference multiplication for a known matrix pair within `1e-5f` tolerance
- [ ] 9.4 Test the transform propagation pass: create a 3-level deep scene graph (parent, child, grandchild) with known local transforms and verify world transforms match the scalar reference
- [ ] 9.5 Test the SoA layout: verify `world_mat[i]` for entity `i` is accessible and that `&world_mat[0]` is 32-byte aligned
- [ ] 9.6 Test the frustum culling loop: place 5 entities inside the frustum and 5 outside; verify exactly 5 are marked visible by the SIMD path, matching the scalar reference
- [ ] 9.7 Test CPU skinning: apply a known bone matrix to a vertex and verify the SIMD result matches the scalar result within `1e-5f`
- [ ] 9.8 Test the migration shim `math_compat.h`: verify existing call sites using `.x` continue to compile and produce correct values after the shim is applied
- [ ] 9.9 Run coverage -- all new files in `src/engine/math/`, `src/engine/scene/`, and `src/engine/renderer/` must reach >= 95% line and branch coverage before marking complete
