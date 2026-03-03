## 1. V-HACD Library Integration
- [ ] 1.1 Add `vhacd` to `vcpkg.json` as an import-time-only dependency; update `CMakeLists.txt` to link `vhacd` only to the `tools/importer` and `src/asset` import pipeline targets, not to the runtime engine binary
- [ ] 1.2 Verify V-HACD compiles with zero warnings under `/W4` (MSVC) and `-Wall -Wextra` (Clang/GCC); scope any required warning suppression pragmas tightly around the V-HACD include block
- [ ] 1.3 Add a `VHACD_AVAILABLE` CMake option (default ON) that disables V-HACD and the `CConvexDecomposedCollider` build path for platforms where V-HACD is unavailable

## 2. glTF Import Flag Parsing
- [ ] 2.1 In `src/asset/mesh_importer.cpp`, check for `_vhacd_decompose: true` in glTF node `extras` during mesh import; if present, trigger the V-HACD decomposition pass for that mesh's vertex/index data
- [ ] 2.2 Read the following optional import settings from glTF node `extras` with defaults: `_vhacd_max_hulls` (int, default 16), `_vhacd_resolution` (int, default 100000 voxels), `_vhacd_concavity` (float, default 0.001)
- [ ] 2.3 Validate import settings on read: `max_hulls` clamped to [1, 64], `resolution` clamped to [10000, 10000000], `concavity` clamped to [0.0001, 1.0]; log a warning with the clamped value if any parameter was out of range

## 3. V-HACD Decomposition Pass
- [ ] 3.1 In `src/asset/mesh_importer.cpp`, implement `run_vhacd_decomposition(vertex_data, index_data, VHACDSettings)` that calls `VHACD::CreateVHACD()`, fills `VHACD::IVHACD::Parameters` from the import settings, and calls `Compute()`
- [ ] 3.2 After computation, iterate the resulting hull array via `GetNConvexHulls()` and `GetConvexHull()`, copying each hull's point cloud into a `ConvexHullData` struct (vertex list only; indices are regenerated at runtime)
- [ ] 3.3 Log the following per-mesh import statistics: total hull count, max error distance (reported by V-HACD), total hull vertex count across all hulls, and elapsed decomposition time in milliseconds
- [ ] 3.4 Free the V-HACD instance via `Release()` immediately after extracting hull data to avoid heap leaks during batch imports

## 4. .uzmesh Format Extension
- [ ] 4.1 Update `src/asset/uzmesh_format.h` to add a `convex_hulls` field (`std::vector<ConvexHullData>`) alongside the existing render mesh data; bump the `.uzmesh` format version constant
- [ ] 4.2 Define `ConvexHullData` struct with `std::vector<float3> vertices` and `hull_index` (int) for ordering; no index buffer stored (Jolt rebuilds indices)
- [ ] 4.3 Implement serialization of `convex_hulls` in the `.uzmesh` writer: write hull count, then per-hull vertex count and vertex array as binary data
- [ ] 4.4 Implement deserialization in the `.uzmesh` loader: detect version bump, read hull array into `convex_hulls`; meshes without hull data (old format) load with an empty hull list

## 5. CConvexDecomposedCollider ECS Component
- [ ] 5.1 Define `CConvexDecomposedCollider` struct in `src/scene/components.h` with fields: `mesh_asset` (AssetHandle referencing a `.uzmesh` with hull data), `use_primitive_fallback` (bool, if true uses a bounding box if hull list is empty), `is_dirty` (bool)
- [ ] 5.2 Register `CConvexDecomposedCollider` in the ECS component registry with reflection metadata

## 6. Jolt Physics Backend
- [ ] 6.1 In `src/physics/collider_factory.cpp`, implement `ColliderFactory::build_convex_decomposed_body(entity)` that loads the `.uzmesh` asset, iterates `convex_hulls`, and builds a `JPH::StaticCompoundShapeSettings` from one `JPH::ConvexHullShapeSettings` per hull
- [ ] 6.2 If `convex_hulls` is empty and `use_primitive_fallback` is true, fall back to a `JPH::BoxShapeSettings` using the mesh AABB half-extents and log a warning
- [ ] 6.3 If `convex_hulls` is empty and `use_primitive_fallback` is false, log an error and do not create a physics body for the entity

## 7. Editor Visualization
- [ ] 7.1 In the physics debug view, when an entity with `CConvexDecomposedCollider` is selected, render each convex hull as a wireframe using the debug draw system, with each hull drawn in a distinct color cycling through the debug palette

## 8. Testing
- [ ] 8.1 Create `tests/physics/test_convex_decomposition.cpp` with Google Test cases: importing a box mesh with `_vhacd_decompose: true` produces at least 1 hull with valid vertices, `run_vhacd_decomposition()` with a known icosphere produces <= 16 hulls at default settings, hull vertex count is > 3 for all returned hulls
- [ ] 8.2 Add import settings validation tests: `max_hulls = 0` is clamped to 1 with warning, `resolution = 1` is clamped to 10000 with warning, `concavity = 2.0` is clamped to 1.0 with warning
- [ ] 8.3 Add `.uzmesh` format round-trip tests: write a file with 3 hulls, reload, assert hull count and vertex counts match exactly
- [ ] 8.4 Add Jolt body construction tests: `build_convex_decomposed_body()` with 3 hulls produces a Jolt body with a compound shape of 3 sub-shapes; empty hull list with `use_primitive_fallback = true` produces a box body; empty hull list with fallback disabled skips body creation
- [ ] 8.5 Add a test that V-HACD memory is freed after decomposition (no heap leak detectable via valgrind / ASAN)
- [ ] 8.6 Verify line coverage >= 95% and branch coverage >= 95% across `mesh_importer.cpp` V-HACD code paths and `collider_factory.cpp` new function

## 9. Documentation
- [ ] 9.1 Update `docs/specs/asset-pipeline.md` with the `_vhacd_decompose` glTF extras flag, all three import settings with types, defaults, and clamping rules, and the import log output format
- [ ] 9.2 Update `docs/specs/physics-system.md` with `CConvexDecomposedCollider` component field reference, fallback behavior, and a note on the Jolt `StaticCompoundShapeSettings` construction from hull list
