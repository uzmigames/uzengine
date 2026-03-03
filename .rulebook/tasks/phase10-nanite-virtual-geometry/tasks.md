## 1. Meshlet Generation at Asset Import

- [ ] 1.1 Add `meshoptimizer` to `vcpkg.json` and verify it builds in the Dev Container with the existing CMake configuration
- [ ] 1.2 Implement `MeshClusterizer::Build(vertices, indices)` in `src/engine/assets/mesh_clusterizer.cpp` calling `meshopt_buildMeshlets()` with `max_vertices=64` and `max_triangles=124` (hardware-optimal for mesh shaders); return a `MeshletData` struct containing the meshlet array, meshlet vertices, and meshlet triangles
- [ ] 1.3 Add `virtual_geometry: true` flag support in the glTF extras parser (`src/engine/assets/gltf_importer.cpp`); only run `MeshClusterizer::Build()` when this flag is present
- [ ] 1.4 Extend the `.uzmesh` binary format to include a `MESHLET` chunk: chunk header with `meshlet_count`, `meshlet_vertex_count`, `meshlet_triangle_count` followed by packed meshlet data
- [ ] 1.5 Store the per-meshlet bounding sphere (center + radius) computed via `meshopt_computeMeshletBounds()` alongside each meshlet in the `.uzmesh` MESHLET chunk for GPU-side visibility culling

## 2. Cluster BVH Construction

- [ ] 2.1 Implement `ClusterBvhBuilder::Build(meshlets, bounds)` in `src/engine/assets/cluster_bvh_builder.cpp` constructing a 4-wide AABB BVH over the meshlet bounding spheres using a SAH (Surface Area Heuristic) split strategy; store nodes as `ClusterBvhNode` structs with child indices and merged AABB
- [ ] 2.2 Serialize the cluster BVH into a `BVH` chunk in the `.uzmesh` file immediately following the `MESHLET` chunk; store node count and node data as a flat array for GPU buffer upload
- [ ] 2.3 At runtime, upload the `ClusterBvhNode` array to a `StructuredBuffer<ClusterBvhNode>` in GPU memory via `IblBaker`-style `CopyBufferRegion` during mesh asset load

## 3. Visibility Buffer Pass

- [ ] 3.1 Create `src/engine/renderer/passes/visibility_pass.cpp` and `visibility_pass.h` implementing a render pass that outputs a 32-bit `DXGI_FORMAT_R32_UINT` visibility buffer encoding `cluster_id` in the upper 20 bits and `instance_id` in the lower 12 bits per pixel
- [ ] 3.2 Write `src/shaders/visibility_pass.hlsl`: a vertex/pixel shader pair where the pixel shader writes `(cluster_id << 12) | instance_id` to the visibility buffer; use amplification/mesh shaders if the device supports `D3D12_MESH_SHADER_TIER_1`, otherwise fall back to a conventional draw
- [ ] 3.3 Integrate `VisibilityPass` into the render graph in `src/engine/renderer/render_graph.cpp` as the first geometry pass, replacing the GBuffer pass for meshes with `virtual_geometry: true`

## 4. GPU-Driven Cluster Culling

- [ ] 4.1 Write `src/shaders/cluster_cull.hlsl`: a compute shader that traverses the cluster BVH on the GPU, tests each cluster's bounding sphere against the view frustum using the current frame's view-projection matrix, and writes surviving cluster draw arguments into a `RWStructuredBuffer<D3D12_DRAW_ARGUMENTS>`
- [ ] 4.2 Implement cluster-level LOD selection in `cluster_cull.hlsl`: compute each cluster's projected screen-space error from the camera distance and the meshlet's maximum edge length; skip clusters whose screen-space error is below `r.nanite_error_threshold` CVar (float, default `1.0` pixel)
- [ ] 4.3 Dispatch `cluster_cull.hlsl` in `src/engine/renderer/passes/cluster_cull_pass.cpp` before the visibility pass; pass the output indirect draw argument buffer to `ExecuteIndirect`

## 5. Software Rasterizer for Small Clusters

- [ ] 5.1 Write `src/shaders/software_rasterizer.hlsl`: a compute shader that handles clusters projecting to fewer than 32 screen pixels; each thread in a 32-wide group processes one triangle, atomically writes the encoded visibility value and depth to the visibility buffer using `InterlockedMax` on packed `uint64_t depth_vis` values
- [ ] 5.2 Add a cluster classification pass before the visibility pass that separates clusters into "hardware" (large) and "software" (small) bins based on projected screen area; dispatch the software rasterizer for small clusters and `ExecuteIndirect` for large clusters

## 6. Deferred Material Evaluation Pass

- [ ] 6.1 Create `src/engine/renderer/passes/material_eval_pass.cpp` implementing a fullscreen compute pass that reads the visibility buffer, reconstructs the triangle's barycentric coordinates for each pixel, fetches the material index from the `MaterialBuffer` via `cluster_id`, and evaluates PBR shading using the existing `pbr_lighting.hlsli`
- [ ] 6.2 Write `src/shaders/material_eval.hlsl` reading `cluster_id` from the visibility buffer, indexing `g_Meshlets[cluster_id]` to get the triangle indices, and computing interpolated attributes (UVs, normals, tangents) from the raw vertex buffers via `g_VertexBuffer`

## 7. Continuous LOD via meshopt_simplify

- [ ] 7.1 Add per-cluster LOD simplification during asset import: call `meshopt_simplify()` on each meshlet's triangle list at 4 target ratios (`1.0`, `0.5`, `0.25`, `0.1`) and store the simplified variants in the `MESHLET` chunk with their associated `max_error` values
- [ ] 7.2 Extend the cluster BVH with LOD transitions: each BVH node stores a `parent_error` value; the GPU culling shader selects the coarsest LOD level whose `max_error` is below `r.nanite_error_threshold` to enable continuous LOD without discrete pop-in

## 8. Testing

- [ ] 8.1 Create `tests/assets/test_mesh_clusterizer.cpp` testing `MeshClusterizer::Build()` on a 500-triangle sphere mesh produces meshlets where each meshlet contains no more than 64 vertices and 124 triangles
- [ ] 8.2 Test that all triangles from the input mesh appear exactly once across all output meshlets (no missing or duplicate triangles)
- [ ] 8.3 Test `meshopt_computeMeshletBounds()` for a known flat quad produces a bounding sphere with radius less than the quad's diagonal length
- [ ] 8.4 Create `tests/assets/test_cluster_bvh_builder.cpp` testing `ClusterBvhBuilder::Build()` on 64 meshlets produces a BVH where every leaf node covers exactly one meshlet and every parent node's AABB contains all its children's AABBs
- [ ] 8.5 Test `.uzmesh` round-trip: serialize a `MeshletData` + `ClusterBvhNode` array to the `MESHLET` and `BVH` chunks, reload from disk, and verify all meshlet counts and node counts match
- [ ] 8.6 Create `tests/renderer/test_visibility_pass.cpp` using mock GPU resources to verify `VisibilityPass::Execute()` outputs a non-zero visibility buffer value for a triangle that covers the center pixel
- [ ] 8.7 Test cluster culling with a frustum that excludes all clusters: verify the indirect draw argument buffer is populated with zero instance counts for all clusters
- [ ] 8.8 Test LOD selection: given a cluster at 10 units distance with `max_error=0.5px` and `r.nanite_error_threshold=1.0`, verify the cluster is selected (error < threshold); at 1000 units distance with `max_error=2.0px`, verify it is skipped
- [ ] 8.9 Test that meshes without `virtual_geometry: true` in glTF extras are not processed by `MeshClusterizer` and do not have `MESHLET`/`BVH` chunks in the output `.uzmesh`
- [ ] 8.10 Run coverage -- all new files in `src/engine/assets/` and `src/engine/renderer/passes/` must reach >= 95% line and branch coverage before marking complete
