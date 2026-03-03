# Proposal: phase2-simd-math-optimization

## Why
The current `Vec3`, `Vec4`, and `Mat4` types use scalar float operations. In tight per-frame loops — scene graph transform propagation, frustum culling of 10,000+ entities, and CPU-side skinning fallback — this processes one float per instruction instead of eight (AVX2). On a scene with 10,000 entities, scalar transform propagation measurably extends the frame budget. Modern CPUs have had AVX2 since 2013 (Haswell) and it is safe to require as a minimum target for UzEngine.

The bottleneck in transform propagation is not arithmetic throughput — it is memory bandwidth and instruction count. Each scalar `Mat4 * Mat4` multiplication executes 64 multiplications and 48 additions in series. An `XMMatrixMultiply` call with AVX2 reduces this to 8 VFMADD256 instructions operating on 8 floats in parallel. The wall-clock speedup on a transform propagation loop over 10,000 nodes is typically 4-8x compared to scalar code compiled at the same optimisation level, because the SIMD path also enables the compiler to pipeline loads from the next node while computing the current one.

The alignment requirement is equally important: a misaligned 128-bit SSE load (`_mm_loadu_ps` instead of `_mm_load_ps`) costs an extra cycle per instruction on every modern x86 microarchitecture. ECS component arrays that are 16-byte aligned and laid out as structure-of-arrays (SoA) allow the CPU to prefetch the next cache line while processing the current one, converting the transform loop from memory-bound to compute-bound.

## What Changes
- Replace `Vec3`, `Vec4`, `Mat4` in `src/math/` with thin wrappers around DirectXMath's `XMFLOAT3`, `XMFLOAT4`, `XMFLOAT4X4` storage types, with conversion operators to/from `XMVECTOR`/`XMMATRIX` for hot-path computation
- Add explicit `alignas(16)` to all storage types and `alignas(32)` to `Mat4` arrays to enable AVX2 aligned loads across 256-bit registers
- Rewrite the transform propagation loop in `src/scene/scene_graph.cpp` to load parent and local matrices into `XMMATRIX`, multiply with `XMMatrixMultiply`, and store results in breadth-first traversal order to maximise cache line reuse
- Rewrite the frustum culling loop in `src/renderer/culling.cpp` to load AABB min/max as `XMVECTOR` and test all 6 frustum planes using `XMVector3Dot` and `XMVectorGreaterOrEqual`, eliminating all branches inside the entity loop
- Add a SoA (structure-of-arrays) layout for the ECS transform component in `src/scene/components/transform_component.h`: separate `float world_mat[N][16]` arrays instead of per-entity structs, enabling sequential SIMD loads
- Add a compile-time feature detection header `src/math/simd_caps.h` that selects SSE4.1, AVX, or AVX2 code paths using `__cpuid` at startup; emit a warning if AVX2 is not available and fall back to SSE4.1
- Add `static_assert(sizeof(Vec4) == 16 && alignof(Vec4) == 16)` and `static_assert(sizeof(Mat4) == 64 && alignof(Mat4) == 16)` in math headers
- Rewrite the CPU-side skinning fallback in `src/renderer/skinning_pass.cpp` to use `XMVector3TransformNormal` and `XMVector3Transform` in a joint-weight accumulation loop
- Add a microbenchmark suite `tests/bench/math_bench.cpp` using Google Benchmark measuring transform propagation throughput for 1K/10K/100K entities in both scalar and SIMD paths
- Update all call sites that access `.x/.y/.z` on `Vec3`/`Vec4` through accessor methods rather than direct member access, using a provided script `tools/migrate_math_access.py` to automate the migration

## Impact
- Affected specs: `docs/specs/renderer.md`, `docs/specs/memory-system.md`, `docs/specs/skeletal-mesh.md`, `docs/specs/animation-system.md`
- Affected code: `src/math/vec3.h`, `src/math/vec4.h`, `src/math/mat4.h`, `src/math/simd_caps.h`, `src/scene/scene_graph.cpp`, `src/scene/components/transform_component.h`, `src/renderer/culling.cpp`, `src/renderer/skinning_pass.cpp`
- Breaking change: YES — existing code using `.x/.y/.z` direct member access on `Vec3`/`Vec4` must be updated to use accessor methods; a compatibility shim (`src/math/math_compat.h`) will bridge the gap during the migration window and will be removed in the subsequent release
- User benefit: Transform propagation and culling are 4-8x faster for scenes with thousands of entities; CPU-side skinning fallback is similarly accelerated; the microbenchmarks give developers a reproducible baseline for future regressions
