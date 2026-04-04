## 1. Phase 1 — Basic Smooth Normals

### 1.1 Normal Averaging Algorithm
- [ ] 1.1.1 Create `runtime/engine/asset/mesh/normal_smoother.h/.cpp` with `ComputeSmoothNormals(vertices, indices, smoothingAngle)`. Algorithm: for each vertex, accumulate face normals weighted by `triangleArea * cornerAngle` (UE5 pattern). Normalize final sum.
- [ ] 1.1.2 Implement hard edge detection: compute dihedral angle between adjacent faces sharing an edge. If angle > `smoothingAngle` (default 60°), split the vertex — each face gets its own copy with distinct normal.
- [ ] 1.1.3 Build adjacency map: for each vertex, find all triangles that share it. Use hash map keyed by vertex position (with epsilon tolerance 1e-5 for floating-point matching).
- [ ] 1.1.4 Add `@ref Thürey 2000 "Weighted Vertex Normals"` and `@ref Max 1999 "Weights for Computing Vertex Normals from Facet Normals"` citations.

### 1.2 Built-in Tangent Computation
- [ ] 1.2.1 Implement `ComputeTangents(vertices, indices, uvs, normals)` — Lengyel's method: compute per-triangle tangent/bitangent from UV gradients, accumulate per-vertex, Gram-Schmidt orthogonalize against normal.
- [ ] 1.2.2 Store tangent as `float4` with `w = sign(dot(cross(N, T), B))` for bitangent handedness (standard convention used by UE5, Unity, Three.js).
- [ ] 1.2.3 Handle degenerate UVs: when UV triangle is degenerate (area ≈ 0), fall back to arbitrary tangent perpendicular to normal.

### 1.3 FBX Importer Integration
- [ ] 1.3.1 After ufbx geometry extraction, call `ComputeSmoothNormals()` when source normals are missing or flat (all normals equal to face normal).
- [ ] 1.3.2 After smooth normals, call `ComputeTangents()` when tangents are missing.
- [ ] 1.3.3 Add import option `recomputeNormals` (bool, default=true) to control whether normals are recomputed or imported as-is.
- [ ] 1.3.4 Add import option `smoothingAngle` (float, default=60.0°) for hard edge threshold.

### 1.4 glTF Importer Integration
- [ ] 1.4.1 In fastgltf importer: if glTF primitive has NORMAL attribute, import as-is (glTF normals are typically correct). If missing, call `ComputeSmoothNormals()`.
- [ ] 1.4.2 If glTF primitive has TANGENT attribute, import as-is. If missing, call `ComputeTangents()`.
- [ ] 1.4.3 Preserve glTF morph target normals (NORMAL in morph targets).

## 2. Phase 2 — Production Quality

### 2.1 MikkTSpace Integration
- [ ] 2.1.1 Add mikktspace C library to `scripts/setup.ps1` — single header+source from https://github.com/mmikk/MikkTSpace.
- [ ] 2.1.2 Create `runtime/engine/asset/mesh/mikktspace_tangents.h/.cpp` wrapping `genTangSpaceDefault()` with UzEngine vertex format adapter (SMikkTSpaceInterface callbacks).
- [ ] 2.1.3 Replace built-in Lengyel tangents with MikkTSpace as default. Keep Lengyel as fallback for meshes without UVs.
- [ ] 2.1.4 Verify MikkTSpace output matches UE5/Unity/Blender tangent space — import same mesh in all 4, compare tangent vectors.

### 2.2 FBX Smoothing Group Import
- [ ] 2.2.1 Read `ufbx_mesh.face_smoothing[i]` (uint32 per face) from ufbx during FBX import.
- [ ] 2.2.2 When smoothing groups are present, use them for hard edge decisions instead of angle threshold: two adjacent faces share a smooth edge only if `(groupA & groupB) != 0`.
- [ ] 2.2.3 When smoothing groups are absent (all zero or all same), fall back to angle-based threshold.
- [ ] 2.2.4 Support both methods simultaneously: smoothing groups take priority, angle threshold fills gaps.

### 2.3 Packed Normal Encoding
- [ ] 2.3.1 Add `PackedNormal` type (int8x4 = 4 bytes): encode `n.xyz` as `int8(n * 127)`, w=0.
- [ ] 2.3.2 Add `PackedTangent` type (int8x4 = 4 bytes): encode `t.xyz` as `int8(t * 127)`, w = sign(bitangent).
- [ ] 2.3.3 Add vertex format variant with packed normals/tangents (8 bytes vs 28 bytes = 71% savings).
- [ ] 2.3.4 Update vertex shader to decode packed normals: `float3 N = float3(packed.xyz) / 127.0`.
- [ ] 2.3.5 Add CVar `r.Mesh.PackedNormals` (bool, default=true) to toggle packed vs float3.

### 2.4 Vertex Deduplication
- [ ] 2.4.1 After normal/tangent computation, merge coincident vertices that have identical position + normal + UV + tangent (within epsilon). Reduces vertex count 10-30% on typical meshes.
- [ ] 2.4.2 Use spatial hash for O(n) merge instead of O(n²) brute force.
- [ ] 2.4.3 Update index buffer after merging.

## 3. Phase 3 — Polish

### 3.1 Import Options
- [ ] 3.1.1 Add `r.Mesh.NormalImportMode` CVar: 0=Import (use source), 1=Recompute (always smooth), 2=Auto (recompute if flat detected).
- [ ] 3.1.2 Add `r.Mesh.SmoothingAngle` CVar (float, default=60.0, range [0,180]).
- [ ] 3.1.3 Add `r.Mesh.TangentMode` CVar: 0=MikkTSpace, 1=Lengyel, 2=Import.
- [ ] 3.1.4 Read import options from `.import.json` sidecar files for per-asset override.

### 3.2 Normal Debug Visualization
- [ ] 3.2.1 Add `--view-mode worldnormal` debug mode showing world-space normals as RGB.
- [ ] 3.2.2 Add `--view-mode tangent` debug mode showing tangent vectors as RGB.
- [ ] 3.2.3 Add `--view-mode faceted` debug mode highlighting edges where normals are split (hard edges shown in red).

### 3.3 Mesh Optimization
- [ ] 3.3.1 Run meshoptimizer vertex cache optimization after normal/tangent computation (already integrated in engine).
- [ ] 3.3.2 Run meshoptimizer overdraw optimization for better rasterization order.
- [ ] 3.3.3 Verify LOD generation preserves smooth normals across LOD levels.

## 4. Testing

### 4.1 Unit Tests
- [ ] 4.1.1 Test `ComputeSmoothNormals` on a cube (6 faces): verify each vertex has averaged normal, hard edges at 90° corners produce split vertices.
- [ ] 4.1.2 Test `ComputeSmoothNormals` on a UV sphere: verify all normals point radially outward, no faceting visible.
- [ ] 4.1.3 Test `ComputeTangents` on a plane with UV: verify tangent aligns with U direction, bitangent with V.
- [ ] 4.1.4 Test hard edge detection: two triangles at 45° (below 60° threshold) share smooth normal; at 75° they split.
- [ ] 4.1.5 Test degenerate cases: zero-area triangle, coincident vertices, missing UVs.
- [ ] 4.1.6 Test packed normal encode/decode roundtrip: error < 1/127 per component.

### 4.2 Visual Regression
- [ ] 4.2.1 Import DamagedHelmet.glb — compare before/after smooth normals. Specular highlights should be smooth, not faceted.
- [ ] 4.2.2 Import FBX mushroom — verify bark trunk is smooth, cap top has correct hard edges.
- [ ] 4.2.3 Import a UV sphere — verify no visible polygon edges under any lighting.
- [ ] 4.2.4 Import a hard-surface model (robot, weapon) — verify intentional hard edges are preserved.
- [ ] 4.2.5 PSNR comparison: smooth vs flat normals on same mesh (expect visible difference = low PSNR is expected).

### 4.3 Performance
- [ ] 4.3.1 Benchmark normal computation: target < 100ms for 100K triangle mesh on single thread.
- [ ] 4.3.2 Benchmark MikkTSpace tangent generation: target < 200ms for 100K triangle mesh.
- [ ] 4.3.3 Compare GPU vertex fetch with packed (8 bytes) vs float (28 bytes) normals+tangents — measure bandwidth savings.

### 4.4 Coverage
- [ ] 4.4.1 Verify 95%+ coverage on all new C++ code (normal_smoother, mikktspace_tangents, packed_normal).

## 5. Documentation
- [ ] 5.1 Update `docs/specs/asset-pipeline.md` with normal smoothing pipeline description.
- [ ] 5.2 Update `docs/specs/rendering/shader-library.md` with packed normal decode functions.
- [ ] 5.3 Update `docs/guides/asset-pipeline-guide.md` with import options for normals/tangents.
- [ ] 5.4 Add `@ref` citations: Max 1999, Thürey 2000, Mikkelsen 2010 "Simulation of Wrinkled Surfaces Revisited".
