## 1. Dependencies

- [ ] 1.1 Add `ufbx` to vcpkg.json (FBX import)
- [ ] 1.2 Add `meshoptimizer` to vcpkg.json (LOD generation)
- [ ] 1.3 `fastgltf` already planned — confirm in vcpkg.json
- [ ] 1.4 CMakeLists: link ufbx, meshoptimizer, fastgltf

## 2. Internal Mesh Format

- [ ] 2.1 Define `StaticVertex` struct: float3 pos, float3 normal, float4 tangent, float2 uv0
- [ ] 2.2 Define `SkinnedVertex` struct: StaticVertex + uint8x4 bone_indices + uint8x4 bone_weights
- [ ] 2.3 Define `MeshData` struct: vertices, indices, lods, submeshes, optional skeleton path
- [ ] 2.4 Implement `.uzmesh` binary writer (see format in spec)
- [ ] 2.5 Implement `.uzmesh` binary reader (for GPU upload at runtime)

## 3. Skeleton Format

- [ ] 3.1 Define `BoneInfo` struct (name, parent_index, bind transforms, inverse_bind_world)
- [ ] 3.2 Implement `.uzskel` text writer (INI-like)
- [ ] 3.3 Implement `.uzskel` text reader
- [ ] 3.4 `Skeleton::compute_inverse_bind_poses()` — walk hierarchy, compute world-space inverse

## 4. FBX Importer (`src/assets/fbx_importer.h/cpp`)

- [ ] 4.1 `ufbx_load_file()` with Y-up right-hand axes, unit_meters=0.01
- [ ] 4.2 Extract static mesh: positions, normals, UV0, tangents
- [ ] 4.3 Auto-generate tangents if missing (ufbx provides tangent generation)
- [ ] 4.4 Weld vertices: deduplicate by position+normal+UV threshold
- [ ] 4.5 Extract skeleton: bone names, parent indices, bind pose matrices
- [ ] 4.6 Extract skin weights: up to 4 influences, normalize, quantize to uint8
- [ ] 4.7 Extract morph targets (blend channels): sparse delta position+normal
- [ ] 4.8 `ufbx_bake_anim()` — bake all animation stacks to keyframe tracks
- [ ] 4.9 Write `.uzmesh`, `.uzskel`, `.uzanim` outputs
- [ ] 4.10 Write `.import.json` with hash, settings, output paths

## 5. GLTF Importer (`src/assets/gltf_importer.h/cpp`)

- [ ] 5.1 `fastgltf::Parser::loadFile()` with accessor extraction
- [ ] 5.2 Same output format as FBX importer (`.uzmesh`, `.uzskel`, `.uzanim`)
- [ ] 5.3 Extract PBR materials → generate `.uzmat` per material
- [ ] 5.4 Normalize coordinate system (GLTF is already Y-up right-hand)
- [ ] 5.5 Round-trip test: export from Blender as GLTF, import, render

## 6. LOD Generation

- [ ] 6.1 `generate_lods(positions, indices, lod_ratios, lod_errors)` using meshoptimizer
- [ ] 6.2 4 LOD levels: ratio [1.0, 0.5, 0.25, 0.10]
- [ ] 6.3 Per-LOD: `meshopt_simplify` + `meshopt_optimizeVertexCache`
- [ ] 6.4 Sloppy simplify for LOD2/3 (5–10× faster, acceptable for distant meshes)
- [ ] 6.5 LOD table stored in `.uzmesh` header

## 7. GPU Upload (`src/renderer/mesh_buffer.h/cpp`)

- [ ] 7.1 `MeshBuffer::upload(device, cmd_list, MeshData*)` — VB + IB in DEFAULT heap via UPLOAD staging
- [ ] 7.2 `MeshManager`: resource registry, dedup by res:// path
- [ ] 7.3 `MeshBuffer` destructor releases DX12 resources (GPU-owned unique_ptr)
- [ ] 7.4 LOD buffer: one VB per LOD or indexed ranges within shared VB

## 8. Model Preview (VSCode panel)

- [ ] 8.1 `asset.preview` IPC — engine loads mesh into an isolated preview scene
- [ ] 8.2 Preview scene: mesh + default material + orbital camera at asset center
- [ ] 8.3 Preview streams JPEG frames on port 9004 (separate from editor stream)
- [ ] 8.4 VSCode `ModelPreviewPanel` connects to port 9004, shows Canvas
- [ ] 8.5 Toolbar: Wireframe / Solid / UV checker / Bones toggle
- [ ] 8.6 LOD selector: [Auto / LOD0 / LOD1 / LOD2 / LOD3] — force specific LOD
- [ ] 8.7 Status bar: triangle count for selected LOD, bone count, material count
- [ ] 8.8 Auto-rotate toggle (turntable)

## 9. Testing

- [ ] 9.1 Import `assets/samples/box.fbx` (Blender export) — visible in viewport
- [ ] 9.2 Import `assets/samples/character.fbx` (rigged character) — skeleton visible
- [ ] 9.3 LOD switch at distance: LOD0 near, LOD3 far (verify in scene)
- [ ] 9.4 GLTF `DamagedHelmet.glb` (Khronos sample) — correct PBR materials
- [ ] 9.5 Model preview panel shows correct wireframe and bone overlay
