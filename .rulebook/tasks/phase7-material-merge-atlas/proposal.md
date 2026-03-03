# Proposal: phase3-material-merge-atlas

## Why

GPU instancing in UzEngine (phase2-gpu-instancing) dramatically reduces draw calls for scenes where many instances of the same mesh+material pair are visible simultaneously. However, a common scenario defeats instancing entirely: an environment with hundreds of unique static props (benches, mailboxes, trash cans, fence posts, fire hydrants, planters, newspaper boxes) where each prop has its own unique material because each has its own unique texture. Even if all props use the identical PBR shader, the different material handles prevent BatchBuilder from grouping them.

A city street scene with 600 unique prop meshes, each with its own albedo/normal/ORM textures, can produce 600+ draw calls per frame — not because the geometry is complex but because of material fragmentation. Unreal Engine's solution is "Material Merging and Texture Atlasing" in the HLOD (Hierarchical LOD) system. However, Unreal's HLOD is opaque, requires complex configuration, and the UV remapping is baked into the HLOD mesh in ways that make the result hard to inspect or re-generate.

UzEngine's `MaterialMerger` solves this with a transparent, auditable process: given a list of static meshes, it packs their textures into a shared atlas, remaps UV coordinates in the vertex data, and produces new `.uzmesh` and `.uzmat` assets that are single-material. The output is fully inspectable — the artist can see the atlas texture in the Texture Preview panel and the merged mesh in the Model Preview panel. Since the merged output is a distinct asset (not a modification of the originals), the originals remain intact for individual editing.

## What Changes

- **`src/asset/tools/material_merger.cpp`**: New file. Core atlas generation and UV remapping logic. Input: list of `(mesh_path, material_path)` pairs. Steps: (1) validate all inputs share the same shader variant key, (2) load all albedo/normal/ORM textures, (3) run Guillotine rect packer to compute atlas layout, (4) render each source texture into the atlas using DirectXTex `CopyRectangle`, (5) for each mesh, remap vertex UVs from [0,1] to atlas sub-region, (6) merge all geometry into one vertex/index buffer with submesh table, (7) write merged `.uzmesh`, atlas `.dds` files, and merged `.uzmat`.
- **`src/asset/tools/rect_packer.cpp`**: New file. Guillotine rectangle packing algorithm. Input: list of `{width, height, id}` rectangles sorted largest-first. Output: list of `{id, x, y, w, h}` placements within a `atlas_size x atlas_size` canvas (configurable, default 4096). Returns failure if any rect cannot fit.
- **`src/asset/tools/uv_remapper.cpp`**: New file. `UVRemapper::remap(vertices, region) -> void`. For each vertex, applies `new_uv = region.uv_min + v.uv * (region.uv_max - region.uv_min)`. Operates on a copy of the vertex buffer (original mesh unmodified).
- **IPC handler**: `asset.mergeAtlas { meshes: [{ mesh, material }], atlas_size?, output_name } -> { ok, output_mesh, output_materials, atlas_textures, draw_call_reduction, warnings[] }`. Warnings include: meshes skipped because they have incompatible shader keys, textures that did not fit in the atlas (returned unpacked), UVs outside [0,1] range (tiling UVs break with atlasing).
- **VSCode Asset Browser**: Right-click menu on a folder or selection of meshes: "Merge Materials into Atlas". Opens a dialog showing the merge preview (estimated atlas utilization, estimated draw call reduction). Submits `asset.mergeAtlas` IPC.
- **Merge eligibility check**: `MaterialMerger` rejects meshes with UVs outside [0,1] (tiling would break), meshes with more than 4 UV channels (complex UV sets), skinned meshes (phase3-modular-skeleton-merge handles those), and meshes whose textures exceed 1024x1024 in any dimension (unlikely to pack efficiently into a shared atlas without quality loss).
- **`src/asset/tools/material_merger.cpp`**: Supports partial merge — meshes that fail eligibility are returned in the `warnings[]` list, while eligible meshes proceed. The output always contains at least two meshes merged (if only one eligible mesh is found, the operation is skipped with a warning).
- **Re-merge detection**: If `.import.json` for the merged output exists and source mesh cook keys are unchanged, skip re-merging. Only re-merge if a source texture or mesh has changed.
- **Documentation**: Add material merging section to `docs/guides/asset-pipeline-guide.md`. Update `docs/specs/engine/optimization-systems.md` section 5.
- **Tests**: Integration test: create 4 test meshes each with a unique 256x256 albedo texture. Run `MaterialMerger::merge`. Verify: atlas texture is 512x512 (4 tiles), merged mesh has 4 submeshes sharing one material slot, UV coordinates of each mesh's vertices are remapped to the correct atlas quadrant, output `.uzmat` references the atlas texture.

## Impact

- **Affected specs**: `docs/specs/assets/asset-pipeline.md`, `docs/specs/rendering/gpu-instancing.md`, `docs/specs/engine/optimization-systems.md`
- **Affected code**: `src/asset/tools/material_merger.cpp` (new), `src/asset/tools/rect_packer.cpp` (new), `src/asset/tools/uv_remapper.cpp` (new), `src/vscode_ext/panels/asset_browser.ts`, `src/ipc/handlers/asset_handlers.cpp`
- **Breaking change**: NO — additive. Produces new merged assets; does not modify originals.
- **User benefit**: City/environment scenes with hundreds of unique static props can reduce draw calls from 500+ to single digits through atlas merging. Combined with GPU instancing (identical merged props can now be instanced), an environment that was hitting 800 draw calls can reach 20-30 draw calls. This is often the difference between 30fps and 60fps for complex environments on mid-range hardware.
