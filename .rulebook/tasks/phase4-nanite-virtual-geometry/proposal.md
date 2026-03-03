# Proposal: phase4-nanite-virtual-geometry

## Why
Triangle count is the primary bottleneck for detailed environments. A single rock or wall asset at 500K triangles cannot be placed freely in a scene without manual LOD authoring and draw call budgeting. Nanite-like virtual geometry streams and rasterizes only the triangles visible at the current camera distance, removing both the LOD authoring burden and the triangle budget constraint. This is a prerequisite for photorealistic environment density comparable to Unreal Engine 5 shipped titles.

## What Changes
- Mesh cluster (meshlet) generation using `meshoptimizer::meshopt_buildMeshlets` during asset import, stored in `.uzmesh`
- Cluster BVH construction for hierarchical visibility culling on the GPU
- Software rasterizer compute shader for small clusters (< 32 triangles projected screen area) to avoid hardware rasterizer overhead
- Visibility buffer pass rendering cluster ID and instance ID per pixel instead of final shading output
- Deferred material evaluation pass reading the visibility buffer to shade only surviving pixels once
- Cluster-level LOD selection driven by projected screen-space error per cluster, computed on the GPU
- GPU-driven cluster draw dispatch replacing the mesh draw loop (requires `phase4-gpu-driven-rendering` as prerequisite)
- `meshopt_simplify` per-cluster DAG for continuous LOD transitions without discrete LOD levels
- Asset importer flag `virtual_geometry: true` in glTF extras to opt meshes into the cluster pipeline

## Impact
- Affected specs: `docs/specs/renderer.md`, `docs/specs/asset-pipeline.md`, `docs/specs/gpu-instancing.md`
- Affected code: `src/asset/mesh_importer.cpp`, `src/renderer/visibility_pass.cpp`, `src/renderer/cluster_cull.comp.hlsl`, `src/rhi/dx12/dx12_command_list.cpp`
- Breaking change: NO (opt-in per mesh asset; standard LOD path continues to work)
- User benefit: Artists can import high-resolution photogrammetry and CAD assets without manual LOD creation, and scenes can contain millions of triangles with no per-object draw call cost.
