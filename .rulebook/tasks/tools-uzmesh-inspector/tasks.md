# Tasks: tools-uzmesh-inspector

## 1. UzMeshInspector Reader Library

- [ ] 1.1 Create `tools/uzmesh_inspector/uzmesh_inspector.h` defining `UzMeshInspector` class with `Inspect(path) -> MeshInspectReport` method; pure file I/O, no GPU required
- [ ] 1.2 Create `tools/uzmesh_inspector/uzmesh_inspector.cpp` implementing binary `.uzmesh` format parser
- [ ] 1.3 Define `MeshInspectReport` struct: `{lods: [{vertex_count, index_count, triangle_count, disk_size_bytes, gpu_vb_size_bytes, gpu_ib_size_bytes}], material_slots: [{name, mat_path, uv_set}], skeleton: {bone_count, bone_names[], max_depth}, aabb: {min, max, extents}, flags: {has_normals, has_tangents, has_blend_shapes, has_skinning}}`
- [ ] 1.4 Parse LOD table from `.uzmesh` header: read LOD count, per-LOD vertex and index counts, disk offsets
- [ ] 1.5 Parse material slot table: read slot names, referenced `.uzmat` paths, and UV set indices
- [ ] 1.6 Parse skeleton data: read bone count, bone name strings, and compute maximum hierarchy depth via parent-index traversal
- [ ] 1.7 Parse AABB: read `min` and `max` float3 values from header; compute `extents = max - min`
- [ ] 1.8 Parse mesh flags bitfield: extract `has_normals`, `has_tangents`, `has_blend_shapes`, `has_skinning` from the flags field

## 2. LOD Reduction Ratio Computation

- [ ] 2.1 Compute `lod_reduction_ratio[]` array: `ratio[i] = lods[i].triangle_count / lods[0].triangle_count`
- [ ] 2.2 Define expected ratio ranges: LOD1 should be 25–40% of LOD0, LOD2 should be 8–15%, LOD3 should be 2–5%
- [ ] 2.3 Flag any LOD with reduction ratio < 10% or > 60% as anomalous (for yellow highlighting in the panel)
- [ ] 2.4 Flag any mesh with `has_skinning == true` but `skeleton.bone_count == 0` as anomalous (red highlight)

## 3. IPC Handlers

- [ ] 3.1 Add `tools.inspect_uzmesh(path)` IPC handler in `engine/src/ipc/handlers/tools_handlers.cpp` returning JSON-serialized `MeshInspectReport`
- [ ] 3.2 Add `tools.render_mesh_thumbnail(path)` IPC handler: render the mesh at LOD0 in a 256×256 offscreen render pass, encode as JPEG, return as base64 string
- [ ] 3.3 Implement offscreen render setup for thumbnail: create temporary render target, set up a camera framing the mesh AABB, render one frame, read back pixels
- [ ] 3.4 Register both IPC handlers in the engine's IPC dispatch table

## 4. MeshInspectReport JSON Serialization

- [ ] 4.1 Implement `MeshInspectReport::ToJson()` returning a structured JSON object with all fields
- [ ] 4.2 Include computed fields in JSON: `lod_reduction_ratios[]`, `anomalies[]` (list of anomaly descriptions)
- [ ] 4.3 Include `gpu_total_mb` summary field: sum of all `gpu_vb_size_bytes + gpu_ib_size_bytes` across all LODs, converted to MB

## 5. VSCode Extension Panel

- [ ] 5.1 Create `vscode-extension/src/panels/UzMeshInspectorPanel.ts` as a `WebviewPanel` with tabbed layout
- [ ] 5.2 Implement "LOD Stats" tab: table showing vertex count, triangle count, disk size, GPU size, and reduction ratio per LOD; highlight anomalous ratios in yellow/red
- [ ] 5.3 Implement "Material Slots" tab: table showing slot name, `.uzmat` path, and UV set index per material slot
- [ ] 5.4 Implement "Skeleton" tab: bone count, max depth, and scrollable bone name list
- [ ] 5.5 Implement "Raw Flags" tab: binary display of `has_normals`, `has_tangents`, `has_blend_shapes`, `has_skinning`
- [ ] 5.6 Implement inspector header: 256×256 thumbnail fetched via `tools.render_mesh_thumbnail` IPC call, displayed above the tabs; show loading spinner while in flight
- [ ] 5.7 Update `vscode-extension/src/panels/AssetBrowser.ts`: register `.uzmesh` double-click handler that opens `UzMeshInspectorPanel` instead of the generic file viewer

## 6. Testing (≥95% line + branch coverage)

- [ ] 6.1 Write `tests/tools/test_uzmesh_inspector_basic.cpp`: create a synthetic `.uzmesh` binary with 2 LODs, 3 material slots, 20-bone skeleton, and all flag bits set; call `Inspect()`; assert all `MeshInspectReport` fields match expected values exactly
- [ ] 6.2 Write `tests/tools/test_uzmesh_inspector_lod_ratios.cpp`: create a `.uzmesh` with LOD1 = 50% of LOD0 triangles (within range); assert no anomaly flagged; create one with LOD1 = 5% (anomalous); assert anomaly flagged
- [ ] 6.3 Write `tests/tools/test_uzmesh_inspector_skinning_anomaly.cpp`: create a `.uzmesh` with `has_skinning == true` and `bone_count == 0`; assert anomaly flagged with correct description
- [ ] 6.4 Write `tests/tools/test_uzmesh_inspector_json.cpp`: call `MeshInspectReport::ToJson()`; assert all fields including `lod_reduction_ratios` and `gpu_total_mb` are present with correct values
- [ ] 6.5 Write `tests/tools/test_uzmesh_inspector_no_skeleton.cpp`: create a static mesh `.uzmesh` with zero bones; assert `skeleton.bone_count == 0` and `has_skinning == false`
- [ ] 6.6 Verify ≥95% line and branch coverage across all `tools/uzmesh_inspector/uzmesh_inspector.cpp` using OpenCppCoverage
