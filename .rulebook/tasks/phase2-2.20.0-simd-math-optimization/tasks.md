## Phase 1: Texture Mipchain SIMD (CRITICAL — 5-10x speedup)
- [ ] 1.1 Read `runtime/engine/renderer/upload/async_texture_decoder.cpp` — identify the scalar 2x2 box filter downsample loop (~L147-168).
- [ ] 1.2 Implement SSE2 mipchain downsample: unpack uint8 to uint16, add 4 neighbors, shift right 2, pack back to uint8. Process 4 pixels (16 bytes) per iteration.
- [ ] 1.3 Add AVX2 fast path: process 8 pixels (32 bytes) per iteration with runtime `__cpuid` check for AVX2 support.
- [ ] 1.4 Handle edge cases: odd-dimension mips (1xN, Nx1) fall back to scalar.
- [ ] 1.5 Add `UZ_PROFILE_SCOPE("MipchainGenerate")` around the mip loop for benchmarking.
- [ ] 1.6 Build + verify: load a 2048x2048 texture, confirm all mip levels match scalar output (bit-exact or within rounding).

## Phase 2: Mesh SDF Triangle Normals (HIGH — 3-4x speedup)
- [ ] 2.1 Read `runtime/engine/renderer/gi/mesh_sdf_baker.cpp` — identify the scalar cross product + normalize loop (~L123-146).
- [ ] 2.2 Replace scalar edge vectors with `XMLoadFloat3` + `XMVectorSubtract`.
- [ ] 2.3 Replace scalar cross product with `XMVector3Cross`.
- [ ] 2.4 Replace scalar normalize with `XMVector3Normalize`.
- [ ] 2.5 Store results via `XMStoreFloat3` into `tri.normal`.
- [ ] 2.6 Build + verify: SDF bake produces identical normals (within float epsilon).

## Phase 3: SH Basis Vectorization (MEDIUM — 3-4x speedup)
- [ ] 3.1 Read `runtime/engine/renderer/ibl/sh_projection.cpp` — identify `EvaluateSHBasis()` (~L56-81).
- [ ] 3.2 Create `EvaluateSHBasis4()` that takes 4 directions packed as XMVECTOR x/y/z arrays.
- [ ] 3.3 Evaluate all 9 SH coefficients using SIMD multiply/add for 4 directions simultaneously.
- [ ] 3.4 Update the cubemap pixel loop to call `EvaluateSHBasis4()` in batches of 4 pixels.
- [ ] 3.5 Handle remainder pixels (count % 4 != 0) with scalar fallback.
- [ ] 3.6 Build + verify: SH coefficients match scalar version within float epsilon.

## Phase 4: Vec3 Component-wise Min/Max (MEDIUM — 2x speedup)
- [ ] 4.1 Read `runtime/engine/math/vector3.cpp` — identify `Vec3::min()` and `Vec3::max()` (~L37-43).
- [ ] 4.2 Replace `std::min`/`std::max` per-component with `XMLoadFloat3` + `XMVectorMin`/`XMVectorMax` + `XMStoreFloat3`.
- [ ] 4.3 Build + run all 70 math unit tests to confirm no regressions.

## Phase 5: Shadow Cascade Frustum Batch (LOW — 2-4x speedup)
- [ ] 5.1 Read `runtime/engine/renderer/shadows/shadow_cascade.cpp` — identify frustum corner loop (~L92-97).
- [ ] 5.2 Batch 4 NDC corners: load 4 Vec4s, multiply all by invViewProj using `XMVector4Transform`, perspective divide.
- [ ] 5.3 Process remaining 4 corners in second batch.
- [ ] 5.4 Build + verify: shadow cascades render identically.

## Phase 6: Radiance Cache Grid Snap (LOW — 2-3x speedup)
- [ ] 6.1 Read `runtime/engine/renderer/gi/radiance_cache.cpp` — identify scalar floor/abs (~L105-129).
- [ ] 6.2 Replace with `XMLoadFloat3` + `XMVectorFloor` + `XMVectorMultiply` for grid snap.
- [ ] 6.3 Replace scalar `std::abs` diff with `XMVectorAbs` + `XMVectorSubtract`.
- [ ] 6.4 Build + verify: radiance cache behavior unchanged.

## Phase 7: Validation
- [ ] 7.1 Run full unit test suite — all tests pass.
- [ ] 7.2 Profile benchmark: capture before/after timings for each optimized function via Tracy/UZ_PROFILE_SCOPE.
- [ ] 7.3 Visual verification: PBR demo screenshot at default + elevated camera — pixel-identical to pre-optimization.
- [ ] 7.4 Document results in `docs/analysis/simd-optimization-results.md` with benchmark numbers.
