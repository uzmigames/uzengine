# Proposal: phase2-auto-lod-pipeline

## Why

Every 3D mesh imported into a game engine will eventually be seen at multiple distances. At 200 meters, a hero character model with 80,000 triangles is covering four pixels on screen — yet the GPU is still processing all 80,000 triangles, their normals, tangents, and UV channels. Multiplied across a scene with hundreds of unique meshes, this produces the classic "triangle budget exceeded on everything except the hero" situation that plagues shipped games.

In Unreal Engine 4 and 5, LOD generation for static meshes is available but **off by default**. The recommended workflow is to configure LODs per mesh in the Static Mesh Editor, which requires manual attention for every asset in the project. Under deadline pressure, this step is frequently skipped. Projects ship with 200-triangle rocks and 500-triangle tree stumps sitting at their LOD0 resolution regardless of viewing distance. The engine's LOD system is capable but invisible to the developer until they actively seek it out.

The fix is simple: **make LOD generation automatic at import time, on by default, with sensible defaults that work for 90% of assets without any configuration**. The `phase2-auto-lod-pipeline` task implements this as a first-class behavior of UzEngine's asset pipeline. Every `.fbx` and `.glb` import generates four LOD levels using meshoptimizer's `meshopt_simplify` function with screen-coverage thresholds. Developers who need to override the defaults (e.g., disable LODs for a first-person weapon that is always close to camera) can do so via a single `.import.json` setting.

## What Changes

- **`asset/importer/mesh_importer.cpp`**: After importing base mesh geometry, always invoke LOD generation pass. Read `auto_lod` from import settings (default: `true`). If true, call `meshopt_simplify` for LOD1 (50% target), LOD2 (25% target), LOD3 (10% target) with per-level error thresholds.
- **`.import.json` schema**: Add `auto_lod: bool` (default `true`), `lod_screen_sizes: float[4]` (default `[0.50, 0.25, 0.10, 0.02]`), `lod_error_thresholds: float[4]` (default `[0.0, 0.01, 0.05, 0.15]`), `nanite_fallback: bool` (default `false`), `nanite_threshold_triangles: int` (default `100000`).
- **`asset/importer/lod_generator.cpp`**: New file. Encapsulates LOD generation: takes base indices/vertices, returns array of LOD index buffers. Also runs `meshopt_optimizeVertexCache` + `meshopt_optimizeOverdraw` on each LOD output.
- **`.uzmesh` binary format**: `flags` field gains bit3 `HAS_AUTO_LOD` to indicate LODs were generated automatically (vs. manually authored). LOD table already exists in the format; no binary format change needed.
- **`renderer/lod_selector.cpp`**: Compute `screen_coverage = mesh_radius / (distance * tan(fov_half))` per visible entity. Select LOD index from per-mesh `lod_screen_sizes` thresholds. Use `CCullDistance` if present for hard cull beyond LOD3.
- **`asset/importer/nanite_fallback.cpp`**: When `nanite_fallback: true` and the `phase4-nanite-virtual-geometry` system is compiled in, delegate LOD0 to the Nanite cluster LOD pipeline instead of traditional 4-level LOD.
- **VSCode Asset Browser**: Mesh inspector panel shows LOD triangle counts per level and the screen-size thresholds. Adds a `[Regenerate LODs]` button that re-runs LOD generation with current settings without re-importing the source file.
- **IPC handler `asset.getInfo`**: LOD data added to response: `lod_levels: [{ triangles, screen_size_min, source }]`.
- **Asset validation**: Emit warning (not error) for any `.uzmesh` with `lod_count == 1` and `auto_lod: false` where the mesh has more than 5,000 triangles, to catch accidentally disabled LODs.
- **Documentation**: Update `docs/specs/assets/asset-pipeline.md` with `auto_lod` setting and new LOD generation section. Update `docs/specs/engine/optimization-systems.md` section 1.
- **Tests**: Unit test `lod_generator.cpp` with a known mesh (e.g., sphere); verify LOD triangle counts are within ±5% of target ratios. Integration test: import a test FBX, verify `.uzmesh` has 4 LOD levels with correct `screen_coverage_threshold` values.

## Impact

- **Affected specs**: `docs/specs/assets/asset-pipeline.md`, `docs/specs/engine/optimization-systems.md`
- **Affected code**: `src/asset/importer/mesh_importer.cpp`, `src/asset/importer/lod_generator.cpp` (new), `src/renderer/lod_selector.cpp`, `src/vscode_ext/panels/asset_browser.ts`
- **Breaking change**: NO — new fields in `.import.json` have defaults; existing imported meshes re-import with auto LOD on next cook (cook key changes due to new settings field)
- **User benefit**: Every mesh in the project automatically renders at appropriate detail level, reducing GPU triangle load by 60-80% for typical scenes with objects at various distances. Zero developer effort required for the common case.
