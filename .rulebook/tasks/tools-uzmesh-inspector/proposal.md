# Proposal: tools-uzmesh-inspector

## Why

The `.uzmesh` binary format is the engine's runtime mesh representation — produced by the glTF/FBX importer, consumed by the renderer and physics system. After a mesh is imported, developers currently have no way to inspect what was actually produced: how many vertices the optimizer retained, whether LOD generation produced the expected polygon reduction ratios, how many material slots were created, whether bones were imported correctly. This opacity creates a diagnostic gap: a character looks wrong in-engine, but the developer cannot tell whether the problem is in the source FBX, the import pipeline, or the renderer — without writing custom debug code to dump the buffer.

The `tools-uzmesh-inspector` is a VSCode panel that opens any `.uzmesh` file and displays its full internal structure as a human-readable table. The panel shows per-LOD statistics (vertex count, index count, estimated triangle count, disk size vs. GPU buffer size), material slot bindings (name, referenced material path, UV set index), skeleton data (bone count, bone names, hierarchy depth), AABB dimensions, and mesh flags (has normals, has tangents, has blend shapes, has skinning weights). The panel includes a mini 3D wireframe preview rendered by the engine in a 256×256 thumbnail viewport.

For AI-assisted asset pipelines — where an AI agent imports hundreds of assets from a vendor pack — the inspector's IPC endpoint `tools.inspect_uzmesh(path)` enables the agent to verify import quality on every mesh without human review: checking that LOD1 is approximately 33% of LOD0's polygon count, that all expected bones are present, and that the AABB is plausible for the mesh's intended scale in the scene.

## What Changes

- `.uzmesh` reader library: `UzMeshInspector` class in `tools/uzmesh_inspector/` — parses binary .uzmesh format and returns structured `MeshInspectReport` (no GPU, no engine required — pure file I/O)
- `MeshInspectReport` struct: `{lods: [{vertex_count, index_count, triangle_count, disk_size_bytes, gpu_vb_size_bytes, gpu_ib_size_bytes}], material_slots: [{name, mat_path, uv_set}], skeleton: {bone_count, bone_names[], max_depth}, aabb: {min, max, extents}, flags: {has_normals, has_tangents, has_blend_shapes, has_skinning}}`
- IPC handler `tools.inspect_uzmesh(path)` returns JSON-serialized `MeshInspectReport`
- VSCode panel `UzMeshInspectorPanel.ts`: tabs for LOD Stats, Material Slots, Skeleton, Raw Flags; LOD stats table shows polygon reduction ratio (LOD1/LOD0 %) colored green if within expected range (25–40%)
- 3D wireframe thumbnail: engine renders mesh at LOD0 in a 256×256 offscreen pass, returns JPEG via `tools.render_mesh_thumbnail(path)` IPC call, displayed in inspector header
- Asset Browser integration: double-clicking a `.uzmesh` file opens the UzMesh Inspector panel instead of generic file viewer
- Anomaly highlighting: LOD reduction ratio < 10% or > 60% is highlighted in yellow (potentially bad import); zero-bone mesh with skinning flag is highlighted in red
- Unit tests: synthetic .uzmesh files with known content; assert MeshInspectReport fields match expected values

## Impact

- Affected specs: `docs/specs/asset-pipeline.md` (document .uzmesh format internals and inspection API)
- Affected code: `tools/uzmesh_inspector/uzmesh_inspector.cpp` (new), `engine/src/ipc/handlers/tools_handlers.cpp` (add inspect_uzmesh and render_mesh_thumbnail), `vscode-extension/src/panels/UzMeshInspectorPanel.ts` (new), `vscode-extension/src/panels/AssetBrowser.ts` (register .uzmesh double-click handler)
- Breaking change: NO (read-only inspection tool; .uzmesh format unchanged)
- User benefit: Gives developers immediate visibility into mesh import quality without custom debug code; enables AI agents to programmatically verify asset pipeline output; surfaces LOD generation anomalies before meshes are placed in scenes
