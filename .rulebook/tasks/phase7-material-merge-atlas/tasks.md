# Tasks: phase7-material-merge-atlas

## 1. RectPacker

- [ ] 1.1 Create `src/asset/tools/rect_packer.h` defining `RectPacker` class
- [ ] 1.2 Create `src/asset/tools/rect_packer.cpp` implementing the Guillotine rectangle packing algorithm
- [ ] 1.3 Accept input: list of `{width, height, id}` rectangles; sort largest-first before packing
- [ ] 1.4 Return output: list of `{id, x, y, w, h}` placements within a configurable `atlas_size × atlas_size` canvas (default 4096)
- [ ] 1.5 Return `PackerResult::FAIL` if any rectangle cannot fit within the atlas
- [ ] 1.6 Support configurable atlas size via `RectPackerConfig.atlas_size`

## 2. UVRemapper

- [ ] 2.1 Create `src/asset/tools/uv_remapper.h` and `src/asset/tools/uv_remapper.cpp`
- [ ] 2.2 Implement `UVRemapper::Remap(vertices, region) -> void`: for each vertex apply `new_uv = region.uv_min + v.uv * (region.uv_max - region.uv_min)`
- [ ] 2.3 Operate on a copy of the vertex buffer — original mesh unmodified
- [ ] 2.4 Detect UVs outside [0, 1] range and return `RemapResult::UV_TILING_DETECTED` to signal ineligibility

## 3. MaterialMerger Core

- [ ] 3.1 Create `src/asset/tools/material_merger.h` and `src/asset/tools/material_merger.cpp`
- [ ] 3.2 Implement `MaterialMerger::Merge(inputs, config) -> MergeResult` where `inputs` is `vector<{mesh_path, material_path}>`
- [ ] 3.3 Step 1: validate all inputs share the same shader variant key; reject mismatches to `warnings[]`
- [ ] 3.4 Step 2: load all albedo/normal/ORM textures for each material
- [ ] 3.5 Step 3: run `RectPacker` on all texture dimensions to compute atlas layout
- [ ] 3.6 Step 4: render each source texture into the atlas using `DirectXTex::CopyRectangle`; produce merged DDS atlas files (albedo atlas, normal atlas, ORM atlas)
- [ ] 3.7 Step 5: for each mesh, call `UVRemapper::Remap` using the atlas sub-region coordinates
- [ ] 3.8 Step 6: merge all geometry into one vertex/index buffer with submesh table
- [ ] 3.9 Step 7: write merged `.uzmesh` and merged `.uzmat` referencing the atlas textures
- [ ] 3.10 Define `MergeResult` struct: `{ok, output_mesh, output_materials, atlas_textures[], draw_call_reduction, warnings[]}`

## 4. Merge Eligibility Checks

- [ ] 4.1 Reject meshes with UVs outside [0, 1] range (tiling UVs break atlasing): add to `warnings[]`
- [ ] 4.2 Reject meshes with more than 4 UV channels
- [ ] 4.3 Reject skinned meshes (skinning handled by `phase6-modular-skeleton-merge`)
- [ ] 4.4 Reject meshes whose textures exceed 1024×1024 in any dimension: add to `warnings[]` with reason
- [ ] 4.5 Implement partial merge: eligible meshes proceed, ineligible meshes are in `warnings[]`; require ≥2 eligible meshes to proceed

## 5. Re-merge Detection

- [ ] 5.1 Read `.import.json` of the merged output if it exists
- [ ] 5.2 Compare source mesh cook keys: if all cook keys are unchanged, skip re-merging and return existing output paths
- [ ] 5.3 Trigger re-merge only when at least one source mesh or texture has changed

## 6. IPC Handler

- [ ] 6.1 Add `asset.mergeAtlas { meshes: [{mesh, material}], atlas_size?, output_name }` IPC handler in `src/ipc/handlers/asset_handlers.cpp`
- [ ] 6.2 Return `{ ok, output_mesh, output_materials, atlas_textures, draw_call_reduction, warnings[] }`
- [ ] 6.3 Compute `draw_call_reduction`: `input_mesh_count / output_submesh_count`

## 7. VSCode Asset Browser Integration

- [ ] 7.1 Add "Merge Materials into Atlas" right-click menu item in `src/vscode_ext/panels/asset_browser.ts` for folder selections or multi-mesh selections
- [ ] 7.2 Show a pre-merge dialog: estimated atlas utilization percentage, estimated draw call reduction count, and list of ineligible meshes (from `warnings[]`)
- [ ] 7.3 On confirmation, submit `asset.mergeAtlas` IPC and show progress notification

## 8. Testing (≥95% line + branch coverage)

- [ ] 8.1 Write `tests/renderer/test_rect_packer.cpp`: pack 4 256×256 rectangles into a 512×512 atlas; assert all 4 placed without overlap; assert total coverage = 100%; attempt to pack a rectangle that won't fit; assert `FAIL` result
- [ ] 8.2 Write `tests/renderer/test_uv_remapper.cpp`: create vertices with UVs in [0, 1]; apply atlas region `{uv_min=(0.5, 0.0), uv_max=(1.0, 0.5)}`; assert each vertex UV is remapped to the correct atlas quadrant
- [ ] 8.3 Write `tests/renderer/test_material_merger_4_meshes.cpp`: create 4 test meshes each with a unique 256×256 albedo texture; run `MaterialMerger::Merge`; verify atlas texture is 512×512 (4 tiles); verify merged mesh has 4 submeshes sharing one material slot; verify UV coordinates of each mesh's vertices are in the correct atlas quadrant
- [ ] 8.4 Write `tests/renderer/test_material_merger_eligibility.cpp`: include a mesh with tiling UVs; assert it appears in `warnings[]` and merge proceeds with remaining eligible meshes; include a skinned mesh; assert it is rejected
- [ ] 8.5 Write `tests/renderer/test_material_merger_remerge_detection.cpp`: run merge; verify output produced; call merge again with unchanged inputs; assert no re-processing (output returned from cache)
- [ ] 8.6 Write `tests/renderer/test_uv_remapper_tiling_detection.cpp`: create vertex with UV value 1.5; assert `UV_TILING_DETECTED` result returned
- [ ] 8.7 Verify ≥95% line and branch coverage across all `src/asset/tools/material_merger.cpp`, `rect_packer.cpp`, and `uv_remapper.cpp` using OpenCppCoverage
