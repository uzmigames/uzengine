## 1. Dependencies

- [ ] 1.1 Add `fastgltf` to `vcpkg.json`
- [ ] 1.2 Add `meshoptimizer` to `vcpkg.json` (LOD simplification + vertex cache optimization)
- [ ] 1.3 CMakeLists: link `fastgltf::fastgltf`, `meshoptimizer`

## 2. Vertex Layout

- [ ] 2.1 Define `Vertex` struct: `float3 pos`, `float3 normal`, `float4 tangent`, `float2 uv`
- [ ] 2.2 Vertex buffer in default heap (UPLOAD → DEFAULT copy at import time)
- [ ] 2.3 Index buffer: `uint32_t` indices

## 3. GLTF Loader (`src/assets/gltf_loader.h/cpp`)

- [ ] 3.1 `GltfLoader::load(path, Arena& arena)` → `MeshData*`
- [ ] 3.2 Extract positions, normals, tangents from GLTF primitive
- [ ] 3.3 Auto-generate tangents if missing (Mikktspace algorithm via `meshoptimizer`)
- [ ] 3.4 Auto-generate normals if missing (flat normals from cross product)
- [ ] 3.5 Support multi-primitive meshes (each primitive = separate draw call)
- [ ] 3.6 Extract material data: albedo factor, metallic, roughness, texture paths

## 4. LOD Generation

- [ ] 4.1 Use `meshopt_simplify` to generate LOD1 (50% triangles) and LOD2 (25%)
- [ ] 4.2 Apply `meshopt_optimizeVertexCache` on each LOD level
- [ ] 4.3 Apply `meshopt_optimizeOverdraw` after vertex cache
- [ ] 4.4 Store LODs in `MeshData::lods[]`

## 5. GPU Upload (`src/renderer/mesh_buffer.h/cpp`)

- [ ] 5.1 `MeshBuffer::upload(device, cmd_list, MeshData*)` — creates VB + IB in DEFAULT heap
- [ ] 5.2 Staging buffer via UPLOAD heap, copy command, resource barrier
- [ ] 5.3 `MeshBuffer` destructor releases DX12 resources (unique_ptr ComPtr)
- [ ] 5.4 `MeshManager`: registry of loaded `MeshBuffer` by resource path

## 6. Material Extraction

- [ ] 6.1 GLTF material → generate `.uzmat` file alongside the `.glb`
- [ ] 6.2 Copy texture paths from GLTF into `.uzmat`
- [ ] 6.3 Duplicate detection: don't regenerate `.uzmat` if it already exists and is newer

## 7. Asset Registry Integration

- [ ] 7.1 Import pipeline runs GLTF loader, stores `MeshData` in `.import/` as binary (or re-parses on load)
- [ ] 7.2 `.import.json` records: vertex count, triangle count, LOD levels, material list
- [ ] 7.3 `asset.import` IPC handler for manual reimport

## 8. Testing

- [ ] 8.1 Load `assets/meshes/box.glb` (Blender export) — renders correctly in viewport
- [ ] 8.2 Load `assets/meshes/damaged_helmet.glb` (Khronos sample) — PBR materials correct
- [ ] 8.3 LOD switching at distance: LOD0 near, LOD2 far
- [ ] 8.4 Meshes survive scene save/load round-trip via resource path
