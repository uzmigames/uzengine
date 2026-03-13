# Rendering — Visibility Pipeline

> **Status**: Planned (Milestone 2.0–4.0)
> **Source**: `runtime/engine/renderer/`
> **Depends on**: Render World (§12-01), RHI (§03)
> **References**: UE5 Nanite visibility, GPU-driven rendering papers

## Purpose

Three-phase culling pipeline that determines which proxies are visible each frame: CPU frustum cull → GPU occlusion cull → LOD selection. Progressively enhanced across milestones.

## Phase 1: Frustum Cull (CPU)

Test `bounding_sphere` vs 6 frustum planes (SIMD, 4 spheres at a time). Rejects ~60-80% of proxies.

Cost: ~0.1ms for 10k proxies.

## Phase 2: GPU Occlusion Cull (Compute Shader)

Input: AABB list + Hierarchical Z-Buffer (HZB) depth pyramid from previous frame.

Per-instance:
1. Project AABB to screen rect
2. Sample HZB at appropriate mip
3. If `max_depth < HZB_depth` → occluded, reject

Output: indirect draw args (only visible instances survive). Rejects ~20-50% of frustum-visible proxies. Cost: ~0.05ms GPU for 4k instances.

## Phase 3: LOD Selection

```
screen_size = bounding_sphere.radius / distance_to_camera
```

Select LOD level from mesh LOD chain. Virtual geometry (Nanite-style) overrides this for enabled meshes.

## Draw Call Sorting

After culling:
- **Opaque**: sort by PSO → material → mesh → distance (front-to-back for early-Z)
- **Transparent**: sort by distance (back-to-front)
- **Shadow**: per-light frustum cull → sort by mesh

## GPU Instance Culling — Indirect Draw Pipeline

For scenes with thousands of instances, CPU draw call submission becomes the bottleneck. GPU instance culling produces `DrawIndexedInstancedIndirect` arguments directly on the GPU.

```
CPU (once per frame):
    1. Upload instance buffer: [world_matrix, bounds, mesh_id, material_id] × N
    2. Upload HZB depth pyramid
    3. Dispatch CullInstances compute shader

GPU Compute (CullInstances.hlsl):
    - Frustum test (6 planes vs AABB)
    - HZB occlusion test
    - Surviving instances → append to indirect draw args
    - Batch key = hash(mesh + material) → same key = auto-instance

GPU Draw:
    DrawIndexedInstancedIndirect per batch
```

Benefits:
- Zero CPU-side per-instance work after initial upload
- Draw call count = unique mesh+material combos (50-200), not instance count (10k+)
- HZB occlusion is free — reuses depth pyramid from post-processing

## Virtual Geometry — Nanite-Style (Phase 4+)

For extremely dense scenes, traditional LOD breaks down. Virtual geometry uses GPU-driven mesh streaming at cluster granularity.

```
Source Mesh (millions of triangles)
    ↓
Offline: Build Cluster Hierarchy (uzcook)
    - Split mesh into ~128-triangle clusters
    - Build BVH over clusters
    - Generate DAG at multiple detail levels
    - Store as .uzvmesh
    ↓
Runtime: GPU-Driven Cluster Selection
    1. Traverse cluster BVH on GPU
    2. Screen-space error test per cluster group
    3. Small triangles → software rasterizer (compute)
    4. Large triangles → hardware rasterizer
    ↓
No LOD pops, no artist-authored LODs
```

## Implementation Phases

| Feature | MVP (Phase 1-2) | Phase 3 | Phase 4+ |
|---------|-----------------|---------|----------|
| RenderProxy in HandleMap | Yes | Yes | Yes |
| Frustum culling (CPU) | Yes | Yes | Yes |
| Traditional LOD | Yes | Yes | Fallback |
| GPU instance culling | — | Yes | Yes |
| HZB occlusion | — | Yes | Yes |
| Virtual geometry (cluster BVH) | — | — | Yes |
| Software rasterizer | — | — | Yes |
| Visibility buffer | — | — | Yes |

## Engine Comparison

| Feature | Unreal 5 | Godot 4 | **UzEngine** |
|---------|----------|---------|--------------|
| CPU culling | Frustum | CPU HZB | **Frustum (SIMD)** |
| GPU culling | GPU instance culling (indirect) | None | **GPU instance culling (indirect)** |
| Occlusion | GPU HZB | CPU HZB | **GPU HZB (fully dynamic)** |
| Virtual geometry | Nanite | None | **Phase 4+ (cluster BVH + sw raster)** |
| Draw call batching | GPU instancing + Nanite | MultiMesh | **GPU instancing + virtual geometry** |
