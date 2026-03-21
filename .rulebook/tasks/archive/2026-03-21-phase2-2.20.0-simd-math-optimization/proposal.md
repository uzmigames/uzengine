# Proposal: phase2-2.20.0-simd-math-optimization

## Why

UzEngine already uses DirectXMath SIMD for core Vec3/Vec4/Mat4/Quaternion operations, but
several CPU-intensive hotpaths still use **scalar code** where SIMD intrinsics would provide
2-10x speedups. The biggest offenders are:

1. **Texture mipchain generation** — scalar uint8 averaging in a tight loop (5-10x gain)
2. **Mesh SDF triangle normal computation** — scalar cross product per triangle (3-4x gain)
3. **SH basis evaluation** — 9 scalar float ops per cubemap pixel, ~98k evaluations (3-4x gain)
4. **Vec3::min/max** — uses `std::min`/`std::max` instead of `XMVectorMin`/`XMVectorMax` (2x gain)
5. **Shadow cascade frustum corners** — 8 one-at-a-time matrix-vector multiplies (2-4x gain)
6. **Radiance cache grid snap** — scalar floor/abs instead of `_mm_floor_ps` (2-3x gain)

These affect asset loading performance (textures, meshes) and per-frame rendering setup.

## What Changes

### Phase 1: Critical — Texture Mipchain SIMD (5-10x)
- Replace scalar 2x2 box filter downsample in `async_texture_decoder.cpp` with SSE2/AVX2
  intrinsics that process 4-16 pixels per iteration using `_mm_avg_epu8` or unpack-add-shift
- File: `runtime/engine/renderer/upload/async_texture_decoder.cpp` ~L147-168

### Phase 2: High — Mesh SDF Triangle Normals (3-4x)
- Replace scalar edge vector + cross product + normalize in `mesh_sdf_baker.cpp` with
  `XMVector3Cross` + `XMVector3Normalize` (already available via DirectXMath)
- File: `runtime/engine/renderer/gi/mesh_sdf_baker.cpp` ~L123-146

### Phase 3: Medium — SH Basis Vectorization (3-4x)
- Pack 4 directions into XMVECTOR and evaluate all 9 SH basis functions in parallel
- File: `runtime/engine/renderer/ibl/sh_projection.cpp` ~L56-81

### Phase 4: Medium — Vec3 Component-wise Min/Max (2x)
- Replace `std::min`/`std::max` with `XMVectorMin`/`XMVectorMax`
- File: `runtime/engine/math/vector3.cpp` ~L37-43

### Phase 5: Low — Shadow Cascade Frustum (2-4x)
- Batch 4 NDC corner transforms at a time using SIMD mat-vec multiply
- File: `runtime/engine/renderer/shadows/shadow_cascade.cpp` ~L92-97

### Phase 6: Low — Radiance Cache Grid Snap (2-3x)
- Replace scalar floor/abs with `_mm_floor_ps` + SIMD abs
- File: `runtime/engine/renderer/gi/radiance_cache.cpp` ~L105-129

## Impact
- Affected specs: none (internal optimization, no API changes)
- Affected code: 6 source files (async_texture_decoder.cpp, mesh_sdf_baker.cpp, sh_projection.cpp, vector3.cpp, shadow_cascade.cpp, radiance_cache.cpp)
- Breaking change: NO
- User benefit: 2-10x faster asset loading and per-frame rendering setup; lower CPU usage
