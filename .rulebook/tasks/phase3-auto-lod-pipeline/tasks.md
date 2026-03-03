# Tasks: phase3-auto-lod-pipeline

## 1. LOD Generator Module

- [ ] 1.1 Create `src/asset/importer/lod_generator.h` defining `LodGenerator` class with `GenerateLods(vertices, indices, settings) -> vector<LodLevel>` public method
- [ ] 1.2 Create `src/asset/importer/lod_generator.cpp` calling `meshopt_simplify` (from meshoptimizer via vcpkg) for LOD1 (50% target), LOD2 (25% target), LOD3 (10% target)
- [ ] 1.3 Define `LodSettings` struct: `auto_lod: bool` (default `true`), `lod_screen_sizes: float[4]` (default `[0.50, 0.25, 0.10, 0.02]`), `lod_error_thresholds: float[4]` (default `[0.0, 0.01, 0.05, 0.15]`)
- [ ] 1.4 Define `LodLevel` struct: `{index_buffer, vertex_count, index_count, screen_size_threshold, source: "auto"|"authored"}`
- [ ] 1.5 Run `meshopt_optimizeVertexCache` and `meshopt_optimizeOverdraw` on each LOD output after simplification

## 2. Mesh Importer Integration

- [ ] 2.1 Integrate `LodGenerator::GenerateLods` call in `src/asset/importer/mesh_importer.cpp` after base mesh geometry import
- [ ] 2.2 Read `auto_lod` from `.import.json` settings (default `true`); skip LOD generation if `auto_lod: false`
- [ ] 2.3 Store per-LOD data in `.uzmesh` LOD table (already exists in format)
- [ ] 2.4 Set bit3 `HAS_AUTO_LOD` in `.uzmesh` `flags` field when LODs are generated automatically

## 3. `.import.json` Schema Extensions

- [ ] 3.1 Add `auto_lod: bool` (default `true`) field to the `.import.json` schema
- [ ] 3.2 Add `lod_screen_sizes: float[4]` (default `[0.50, 0.25, 0.10, 0.02]`) field
- [ ] 3.3 Add `lod_error_thresholds: float[4]` (default `[0.0, 0.01, 0.05, 0.15]`) field
- [ ] 3.4 Add `nanite_fallback: bool` (default `false`) and `nanite_threshold_triangles: int` (default `100000`) fields
- [ ] 3.5 Update JSON schema documentation in `docs/specs/assets/asset-pipeline.md`

## 4. LOD Selector

- [ ] 4.1 Create `src/renderer/lod_selector.cpp` (or update if existing) implementing LOD selection per visible entity
- [ ] 4.2 Compute `screen_coverage = mesh_radius / (distance * tan(fov_half))` per entity
- [ ] 4.3 Select LOD index by comparing `screen_coverage` against per-mesh `lod_screen_sizes` thresholds; return the first LOD whose threshold is ≤ current coverage
- [ ] 4.4 Apply `CCullDistance` hard cull beyond LOD3 if present on the entity

## 5. Nanite Fallback Path

- [ ] 5.1 Create `src/asset/importer/nanite_fallback.cpp` checking `nanite_fallback: true` and mesh triangle count ≥ `nanite_threshold_triangles`
- [ ] 5.2 When Nanite fallback is enabled and `phase4-nanite-virtual-geometry` system is compiled in, delegate LOD0 to the Nanite cluster LOD pipeline
- [ ] 5.3 Guard Nanite path with `#ifdef UZ_NANITE` compile flag

## 6. Asset Browser Integration

- [ ] 6.1 Update the mesh inspector panel in the VSCode Asset Browser to show LOD triangle counts per level and screen-size thresholds
- [ ] 6.2 Add `[Regenerate LODs]` button: sends `asset.regenerateLods(mesh_path)` IPC without requiring full reimport
- [ ] 6.3 Implement `asset.regenerateLods` IPC handler to re-run LOD generation on the existing cached vertex/index data

## 7. Asset Validation Warning

- [ ] 7.1 Add warning rule to the asset validation pass: emit `W_SINGLE_LOD_LARGE_MESH` warning for any `.uzmesh` with `lod_count == 1` and `auto_lod: false` where triangle count > 5000
- [ ] 7.2 Warning message: "Mesh {name} has {N} triangles and no LODs. Consider enabling auto_lod in .import.json"

## 8. IPC Handler Update

- [ ] 8.1 Update `asset.getInfo` IPC handler response to include `lod_levels: [{triangles, screen_size_min, source}]` array
- [ ] 8.2 Update handler in `src/ipc/handlers/asset_handlers.cpp`

## 9. Testing (≥95% line + branch coverage)

- [ ] 9.1 Write `tests/renderer/test_lod_generator.cpp`: create a synthetic sphere mesh with known triangle count; call `GenerateLods`; assert LOD1 triangle count is within ±5% of 50% target; assert LOD2 within ±5% of 25%; assert LOD3 within ±5% of 10%
- [ ] 9.2 Write `tests/renderer/test_lod_selector.cpp`: mock entity at `distance=0` (LOD0), `distance=50m` (LOD1 expected), `distance=200m` (LOD3 expected); assert correct LOD index returned at each distance
- [ ] 9.3 Write `tests/renderer/test_auto_lod_disabled.cpp`: set `auto_lod: false` in import settings; call mesh importer; assert `.uzmesh` has `lod_count == 1`
- [ ] 9.4 Write `tests/renderer/test_lod_vertex_cache_opt.cpp`: assert LOD output after `meshopt_optimizeVertexCache` has lower ACMR (average cache miss ratio) than input
- [ ] 9.5 Write `tests/renderer/test_lod_import_json_defaults.cpp`: import mesh with no `.import.json`; assert LOD generation runs with default parameters and produces 4 LOD levels
- [ ] 9.6 Verify ≥95% line and branch coverage across all `src/asset/importer/lod_generator.cpp` and `src/renderer/lod_selector.cpp` using OpenCppCoverage
