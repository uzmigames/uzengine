## 1. xatlas Dependency Integration

- [ ] 1.1 Add `xatlas` as a vcpkg dependency in `vcpkg.json` with a pinned version compatible with the current toolchain
- [ ] 1.2 Update `CMakeLists.txt` to find and link `xatlas` into the asset pipeline target
- [ ] 1.3 Add `#include <xatlas.h>` to `src/asset_pipeline/uv_unwrap.h` and verify the include path resolves in CI

## 2. UV Unwrap Pipeline Pass

- [ ] 2.1 Create `src/asset_pipeline/uv_unwrap.h` declaring `UVUnwrapPass` with method `unwrap(MeshData& mesh, UVUnwrapQuality quality)` returning `bool` (true if UV1 was generated or already existed)
- [ ] 2.2 Create `src/asset_pipeline/uv_unwrap.cpp` implementing `UVUnwrapPass::unwrap()`: skip if mesh already has a valid UV channel 1; otherwise call xatlas to generate UV1
- [ ] 2.3 Define `enum class UVUnwrapQuality { Fast, Balanced, Best }` in `uv_unwrap.h`; map to xatlas `PackOptions` with increasing chart resolution and pack iteration counts
- [ ] 2.4 Call `UVUnwrapPass::unwrap()` from the mesh import path in `src/asset_pipeline/mesh_importer.cpp` after UV0 is loaded but before the mesh is serialized
- [ ] 2.5 Detect missing UV channel 1 by checking if `MeshData::uv1` is empty after loading the source asset

## 3. Texel Density Normalization

- [ ] 3.1 After xatlas packing, compute the physical surface area of the mesh in world units
- [ ] 3.2 Scale xatlas chart UVs so the resulting texel density is uniform regardless of mesh scale; store the normalization factor in `MeshData::uv1_texel_density`
- [ ] 3.3 Apply normalization before writing UV1 to the internal `.uzmesh` format

## 4. Import Settings

- [ ] 4.1 Extend the mesh import settings struct in `src/asset_pipeline/mesh_import_settings.h` with fields: `auto_uv1` (bool, default true), `uv1_quality` (UVUnwrapQuality, default Balanced), `force_regenerate_uv1` (bool, default false)
- [ ] 4.2 When `force_regenerate_uv1 == true`, discard any existing UV channel 1 from the source asset and re-run xatlas regardless
- [ ] 4.3 Expose these fields in the editor import settings UI panel via IPC

## 5. Generated UV1 Storage

- [ ] 5.1 Add `uv1` field of type `std::vector<Vec2>` to `MeshData` struct in `src/asset_pipeline/mesh_data.h`
- [ ] 5.2 Write UV1 alongside UV0 in the internal mesh serializer `src/asset_pipeline/mesh_serializer.cpp`
- [ ] 5.3 Read UV1 from `.uzmesh` in the runtime mesh loader; expose as vertex attribute bound to TEXCOORD1

## 6. IPC Status Reporting

- [ ] 6.1 Emit an IPC status message `{ "event": "uv_unwrap_started", "mesh": "<name>" }` before beginning xatlas on a mesh
- [ ] 6.2 Emit `{ "event": "uv_unwrap_complete", "mesh": "<name>", "chart_count": N }` on success
- [ ] 6.3 Emit `{ "event": "uv_unwrap_warning", "mesh": "<name>", "message": "overlapping source UV1 detected" }` when the source asset already has UV1 but `force_regenerate_uv1` is false and overlap is detected

## 7. Testing

- [ ] 7.1 Create `tests/asset_pipeline/test_uv_unwrap.cpp` testing `UVUnwrapPass::unwrap()` on a minimal triangle mesh with no UV1; verify UV1 is populated after the call
- [ ] 7.2 Test that `unwrap()` is a no-op (returns true, UV1 unchanged) when the mesh already has UV1 and `force_regenerate_uv1 == false`
- [ ] 7.3 Test `force_regenerate_uv1 == true` discards existing UV1 and produces a new xatlas-generated UV1
- [ ] 7.4 Test all three quality levels (Fast, Balanced, Best) produce valid UV1 with no UV values outside [0, 1]
- [ ] 7.5 Test texel density normalization: two meshes of different physical size produce the same texel density in their UV1
- [ ] 7.6 Test IPC status messages: verify `uv_unwrap_started` and `uv_unwrap_complete` events are emitted for a successful unwrap
- [ ] 7.7 Test overlap detection warning: provide a mesh with a known overlapping UV1 and verify the warning IPC message is emitted
- [ ] 7.8 Test serialization round-trip: serialize a `MeshData` with UV1, deserialize, and verify UV1 values are identical
- [ ] 7.9 Achieve >= 95% line and branch coverage across `uv_unwrap.cpp` and `mesh_serializer.cpp` UV1 paths

## 8. Documentation

- [ ] 8.1 Update `docs/specs/asset-pipeline.md` documenting UV1 auto-unwrap behavior, quality settings, and force-regenerate flag
- [ ] 8.2 Update `docs/specs/renderer.md` documenting that UV1/TEXCOORD1 is now available as a vertex attribute for lightmap and decal shaders
