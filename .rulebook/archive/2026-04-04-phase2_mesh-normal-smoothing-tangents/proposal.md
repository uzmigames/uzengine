# Proposal: Mesh Normal Smoothing + MikkTSpace Tangents (Phase 2)

## Why
Imported FBX/glTF meshes render with visible faceting — flat per-face normals instead of smooth per-vertex normals. This causes harsh polygon edges, incorrect AO accumulation at face boundaries, and blocky specular highlights on curved surfaces. Every major engine (UE5, Godot, Unity, Three.js) computes smooth vertex normals via weighted averaging during import. UzEngine currently passes through raw importer normals without recomputation, and has no MikkTSpace tangent generation — causing normal map seams on mirrored UV meshes. This is the single most visible mesh quality issue.

## What Changes

### Phase 1 — Basic Smooth Normals (2-3 days)
1. `NormalSmoother` — Vertex normal averaging with area+angle weighting (UE5 pattern)
2. Hard edge detection — Edges > smoothing angle threshold (default 60°) split vertex normals
3. Built-in tangent averaging — Basic tangent computation from UV gradients

### Phase 2 — Production Quality (2-3 days)
4. MikkTSpace integration — Industry-standard tangent generation via mikktspace C library
5. FPackedNormal encoding — Compress normals from float3 (12 bytes) to int8x4 (4 bytes), 66% savings
6. FBX smoothing group import — Read smoothing groups from ufbx and use for hard edge decisions

### Phase 3 — Polish (3-5 days)
7. Import options — CVar/config for: recompute normals (yes/no), smoothing angle, normal source (import/compute)
8. glTF normal preservation — Respect glTF normals when present, recompute only when missing
9. Overlapping vertex merging — Merge coincident vertices with matching normals (deduplication)
10. Normal visualization debug mode — `--view-mode normals` showing smooth vs flat regions

### Refactoring
- FBX importer (ufbx): add normal recomputation pass after geometry extraction
- glTF importer (fastgltf): add fallback normal computation when normals missing
- Vertex format: add packed normal + tangent support alongside existing float3
- Mesh GPU upload: support both float3 and packed normal formats

## Impact
- Affected specs: docs/specs/asset-pipeline.md, docs/specs/rendering/shader-library.md
- Affected code: runtime/engine/asset/importers/fbx_importer.cpp, runtime/engine/asset/importers/gltf_importer.cpp, runtime/engine/renderer/mesh/, runtime/shaders/runtime/geometry/
- Breaking change: NO — existing meshes get smoother normals automatically, visual improvement only
- User benefit: Smooth curved surfaces, correct AO, proper normal map rendering, no faceting artifacts
- Enables: High-quality character rendering, architectural visualization, automotive/product viz
