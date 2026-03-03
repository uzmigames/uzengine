# Tasks: phase8-impostor-billboards

## 1. ImpostorBaker

- [ ] 1.1 Create `src/asset/tools/impostor_baker.h` defining `ImpostorBaker` class with `Bake(mesh, material, config) -> ImpostorBakeResult` method
- [ ] 1.2 Create `src/asset/tools/impostor_baker.cpp` implementing the 8-angle atlas bake pipeline
- [ ] 1.3 Define `ImpostorBakeConfig` struct: `output_resolution: int` (default 256), `num_angles: int` (default 8), `capture_elevation_deg: float` (default 15°)
- [ ] 1.4 For each angle in [0°, 45°, 90°, ..., 315°] (8 angles at 45° intervals): position a camera at the specified azimuth + 15° elevation at a distance that fills the frame
- [ ] 1.5 Render the mesh to an offscreen `output_resolution × output_resolution` render target capturing albedo + alpha channel
- [ ] 1.6 Pack 8 captures into a `(num_angles × output_resolution) × output_resolution` atlas strip DDS file
- [ ] 1.7 Capture a per-angle normal-direction atlas for per-pixel normal-based impostor lighting; write as second DDS file
- [ ] 1.8 Write `{mesh_name}_impostor.dds` and `{mesh_name}_impostor_normal.dds`
- [ ] 1.9 Define `ImpostorBakeResult` struct: `{ok, atlas_path, normal_atlas_path, error_message?}`

## 2. `.import.json` Extensions

- [ ] 2.1 Add `impostor_enabled: bool` (default `false`) to `.import.json` schema
- [ ] 2.2 Add `impostor_distance: float` (distance at which impostor activates, default 80.0)
- [ ] 2.3 Add `impostor_resolution: int` (per-angle resolution, default 256)
- [ ] 2.4 Add `impostor_num_angles: int` (default 8)
- [ ] 2.5 Store generated atlas paths in `.import.json` under `impostor_atlas` and `impostor_normal_atlas` fields

## 3. Mesh Importer Integration

- [ ] 3.1 In `src/asset/importer/mesh_importer.cpp`: if `impostor_enabled: true` and mesh is a `StaticMesh`, trigger `ImpostorBaker::Bake` after standard LOD generation
- [ ] 3.2 Store `ImpostorBakeResult.atlas_path` and `normal_atlas_path` in the `.import.json` sidecar

## 4. Impostor HLSL Shader

- [ ] 4.1 Create `src/shaders/impostor/impostor.hlsl` containing vertex and pixel shader for impostor quad rendering
- [ ] 4.2 Vertex shader: expand a point (or unit quad) into a camera-facing billboard quad in world space; cylindrical billboard for trees (Y-axis locked); spherical for compact objects
- [ ] 4.3 Pixel shader: compute `angle_to_camera`; select `frame_a` and `frame_b` atlas columns using `angle_normalized × num_angles`; blend between them using `frac(angle_normalized × num_angles)`
- [ ] 4.4 Alpha-clip below 0.1 for clean silhouettes; sample normal atlas for per-pixel lighting

## 5. ImpostorRenderer

- [ ] 5.1 Create `src/renderer/impostor_renderer.h` and `src/renderer/impostor_renderer.cpp`
- [ ] 5.2 Per frame: for each visible entity in `LOD_IMPOSTOR` rendering state, enqueue an impostor instance `{position, scale, orientation}` into an `ImpostorBatch`
- [ ] 5.3 After all entities processed: issue one `DrawIndexedInstanced` per unique impostor atlas (all instances sharing the same atlas in a single batch)
- [ ] 5.4 Each instance reads from a `StructuredBuffer<ImpostorInstance>` containing world position, scale, and angle_to_camera

## 6. LOD Selector Integration

- [ ] 6.1 Add `LOD_IMPOSTOR` level to the LOD selection enum in `src/renderer/lod_selector.cpp`
- [ ] 6.2 Add impostor selection after LOD3, before distance-cull: if `screen_coverage < impostor_threshold` AND `impostor_atlas` is valid, mark entity as `LOD_IMPOSTOR`
- [ ] 6.3 Route `LOD_IMPOSTOR` entities to `ImpostorRenderer` instead of the standard draw path

## 7. Foliage Integration

- [ ] 7.1 Configure foliage density system to use `LOD_IMPOSTOR` as the last foliage representation tier before full cull
- [ ] 7.2 Foliage tiles beyond `impostor_distance` (default 60m) render as impostor clusters via `ImpostorRenderer`

## 8. IPC Handler and Editor Integration

- [ ] 8.1 Add `asset.bakeImpostor { mesh_path, resolution?, num_angles? } -> { ok, atlas_path, normal_atlas_path }` IPC handler in `src/ipc/handlers/asset_handlers.cpp`
- [ ] 8.2 Add `[Preview Impostor]` button to the Model Preview panel for meshes with `impostor_enabled: true`: shows the billboard rotating through all 8 angles

## 9. Testing (≥95% line + branch coverage)

- [ ] 9.1 Write `tests/renderer/test_impostor_baker.cpp`: bake impostor for a test sphere mesh (deterministic geometry); assert DDS atlas has 8 columns at `output_resolution` each; assert normal atlas exists and has same dimensions
- [ ] 9.2 Write `tests/renderer/test_impostor_lod_selection.cpp`: configure a mesh with `impostor_distance: 80.0`; place mock entity at distance 88.0 (= `impostor_distance × 1.1`); assert `lod_selector` returns `LOD_IMPOSTOR`; place at distance 70.0; assert `LOD_IMPOSTOR` not selected
- [ ] 9.3 Write `tests/renderer/test_impostor_renderer_batching.cpp`: submit 100 impostor instances all sharing the same atlas; assert `ImpostorRenderer` issues exactly 1 `DrawIndexedInstanced` call; submit 100 instances across 2 different atlases; assert exactly 2 draw calls
- [ ] 9.4 Write `tests/renderer/test_impostor_import_disabled.cpp`: import mesh with `impostor_enabled: false`; assert `ImpostorBaker::Bake` is never called and no atlas DDS files written
- [ ] 9.5 Write `tests/renderer/test_impostor_alpha_clip.cpp`: render impostor pixel with atlas sample alpha < 0.1; assert pixel is discarded (alpha-clipped)
- [ ] 9.6 Verify ≥95% line and branch coverage across all `src/asset/tools/impostor_baker.cpp` and `src/renderer/impostor_renderer.cpp` using OpenCppCoverage
