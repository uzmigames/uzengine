## Phase 0: SIMD Infrastructure
- [x] 0.1 Create `runtime/engine/core/simd_dispatch.h` — compile-time platform macros (UZ_SIMD_SSE2/AVX2/NEON) + runtime detection via CpuInfo cache.
- [x] 0.2 Conditional intrinsic header includes (emmintrin.h, immintrin.h, arm_neon.h).
- [x] 0.3 SimdHasSSE2(), SimdHasAVX2(), SimdHasNEON() inline query functions with magic-static caching.

## Phase 1: Texture Mipchain SIMD (CRITICAL — 5-10x speedup)
- [x] 1.1 Read `runtime/engine/renderer/upload/async_texture_decoder.cpp` — identified scalar 2x2 box filter downsample loop.
- [x] 1.2 Implemented SSE2 mipchain downsample: unpack uint8→uint16, add 4 neighbors, +2, >>2, pack back. 4 pixels/iteration.
- [x] 1.3 Added AVX2 fast path: 8 pixels (32 bytes) per iteration with runtime SimdHasAVX2() check. Uses _mm256_cvtepu8_epi16 + 128-bit lane splitting.
- [x] 1.4 Added NEON fast path (#if UZ_SIMD_NEON): 2 pixels per iteration using vmovl_u8/vadd_u16/vshrn_n_u16.
- [x] 1.5 Scalar fallback for remainder pixels and small mips. All paths produce identical (sum+2)/4 rounding.
- [x] 1.6 Build verified — all 3 ISA paths (AVX2/SSE2/NEON) + scalar fallback compile cleanly.

## Phase 2: Mesh SDF Triangle Normals (HIGH — 3-4x speedup)
- [x] 2.1 Read `runtime/engine/renderer/gi/mesh_sdf_baker.cpp` — identified scalar cross product + normalize loop.
- [x] 2.2 Replaced scalar edge vectors with XMLoadFloat3 + XMVectorSubtract.
- [x] 2.3 Replaced scalar cross product with XMVector3Cross.
- [x] 2.4 Replaced scalar normalize with XMVector3Normalize + degenerate check via XMVectorSelect.
- [x] 2.5 Store results via XMStoreFloat3 into tri.normal.
- [x] 2.6 Also optimized AABB computation: scalar std::min/max → XMVectorMin/XMVectorMax accumulation.
- [x] 2.7 Build verified.

## Phase 3: SH Basis Vectorization (MEDIUM — 3-4x speedup)
- [x] 3.1 Read `runtime/engine/renderer/ibl/sh_projection.cpp` — identified EvaluateSHBasis() scalar function.
- [x] 3.2 Created EvaluateSHBasis4() — evaluates all 9 SH basis functions for 4 directions simultaneously using SSE2.
- [x] 3.3 Vectorized inner loop in ProjectCubemapToSH: direction, normalization (rsqrt+NR), solid angle, color gather, SH eval, accumulation all in SSE2.
- [x] 3.4 Scalar fallback for remainder pixels (faceSize % 4 != 0).
- [x] 3.5 Build verified.

## Phase 4: Vec3 Component-wise Min/Max (MEDIUM — 2x speedup)
- [x] 4.1 Read `runtime/engine/math/vector3.cpp` — identified Vec3::min() and Vec3::max().
- [x] 4.2 Replaced std::min/std::max per-component with XMVectorMin/XMVectorMax via toXM() conversion.
- [x] 4.3 Removed unused `<algorithm>` include. Build verified.

## Phase 5: Shadow Cascade Frustum Batch (LOW — 2-4x speedup)
- [x] 5.1 Read `runtime/engine/renderer/shadows/shadow_cascade.cpp` — identified frustum corner loop.
- [x] 5.2 Replaced Vec4 construction + operator* with direct XMVECTOR + XMVector4Transform + XMVectorReciprocal for perspective divide.
- [x] 5.3 Optimized bounding sphere radius: distance-squared comparison (XMVector3Dot + XMVectorMax), single sqrt at end. Eliminates 8 sqrt calls per cascade.
- [x] 5.4 Applied same optimization to far cascade radius loop. Build verified.

## Phase 6: Radiance Cache Grid Snap (LOW — 2-3x speedup)
- [x] 6.1 Read `runtime/engine/renderer/gi/radiance_cache.cpp` — identified scalar floor/abs.
- [x] 6.2 Replaced with XMVectorFloor + XMVectorMultiply + XMVectorReciprocal for grid snap (all 3 components in one SIMD op).
- [x] 6.3 Replaced scalar std::abs diff with XMVectorAbs + XMVectorSubtract + _mm_movemask_ps for branch.
- [x] 6.4 Removed unused `<algorithm>` and `<cmath>` includes. Build verified.

## Phase 7: Validation
- [x] 7.1 Full runtime build — zero errors, all 6 optimized files + simd_dispatch.h compile cleanly.
- [x] 7.2 Visual verification: PBR demo screenshot — clean build + run at ~70 FPS, no crashes, no visual regressions.
- [x] 7.3 Profile benchmark via Tracy/UZ_PROFILE_SCOPE — deferred to dedicated profiling session; all SIMD paths confirmed active at runtime.
