## 1. Bake Compute Shader

- [ ] 1.1 Create `shaders/bake/normal_map_bake.hlsl` implementing a compute shader that casts rays from each low-poly surface texel along the interpolated normal, samples the high-poly mesh SDF or triangle list at the hit point, and writes the tangent-space normal to the output UAV
- [ ] 1.2 Bind the high-poly mesh's triangle buffer as a `StructuredBuffer<Triangle>` in the compute shader
- [ ] 1.3 Bind the low-poly mesh's UV1 texel positions and per-texel normals/tangents as input buffers in the compute shader
- [ ] 1.4 Write the baked tangent-space normal `(nx, ny, nz)` remapped from [-1,1] to [0,1] into the output `RWTexture2D<float4>` UAV

## 2. Cage Mesh Support

- [ ] 2.1 Add an optional cage mesh parameter to the bake shader: when a cage mesh is provided, offset the ray origin outward along the cage surface normal before casting
- [ ] 2.2 In `src/asset_pipeline/normal_map_baker.cpp`, accept an optional cage asset path; if provided, upload the cage mesh vertex/normal buffer before dispatch
- [ ] 2.3 When no cage mesh is provided, use the low-poly normal as the ray origin offset direction with a small fixed bias (configurable, default 0.001 units)

## 3. MikkTSpace Tangent Generation

- [ ] 3.1 Add `mikktspace` as a vcpkg dependency in `vcpkg.json`
- [ ] 3.2 In `src/asset_pipeline/normal_map_baker.cpp`, call `genTangSpaceDefault()` on the low-poly mesh before beginning the bake to generate a consistent tangent basis
- [ ] 3.3 Upload generated tangent vectors alongside normals into the compute shader input buffer

## 4. Normal Map Baker Pipeline Class

- [ ] 4.1 Create `src/asset_pipeline/normal_map_baker.h` declaring `NormalMapBaker` with method `bake(BakeParams params)` returning `bool`
- [ ] 4.2 Define `struct BakeParams` in `normal_map_baker.h` with fields: `low_poly_asset` (string), `high_poly_asset` (string), `output_path` (string), `resolution` (uint32_t: 512/1024/2048/4096), `cage_asset` (optional string)
- [ ] 4.3 Create `src/asset_pipeline/normal_map_baker.cpp` implementing `bake()`: upload meshes to GPU buffers, dispatch the compute shader with resolution/16 thread groups in X and Y, read back the output texture via DX12 readback heap, and save as PNG/DDS via the texture importer

## 5. IPC Handler

- [ ] 5.1 Register IPC handler `asset.bake_normals` in `src/ipc/ipc_handlers.cpp` accepting JSON `{ "low_poly_asset": "...", "high_poly_asset": "...", "output_path": "...", "resolution": 1024, "cage_asset": null }`
- [ ] 5.2 Call `NormalMapBaker::bake()` from the IPC handler on a background thread to avoid blocking the main loop
- [ ] 5.3 Emit IPC progress messages `{ "event": "bake_progress", "percent": N }` at 10% increments during the bake
- [ ] 5.4 Emit `{ "event": "bake_complete", "output_path": "..." }` on success and `{ "event": "bake_failed", "error": "..." }` on failure

## 6. Testing

- [ ] 6.1 Create `tests/asset_pipeline/test_normal_map_baker.cpp` testing `NormalMapBaker::bake()` with a simple quad low-poly mesh and a bumpy high-poly mesh; verify the output PNG is non-null and has pixel values other than flat (0.5, 0.5, 1.0)
- [ ] 6.2 Test that output pixel values are in the valid normal map range: R and G components in [0, 255], B in [128, 255] (z always positive in tangent space)
- [ ] 6.3 Test bake at resolutions 512, 1024, and 2048; verify output image dimensions match requested resolution
- [ ] 6.4 Test that providing a cage mesh produces a different (less self-intersecting) output than baking without a cage on a concave mesh
- [ ] 6.5 Test MikkTSpace: verify that tangent vectors are generated and non-zero for the low-poly mesh before bake
- [ ] 6.6 Test IPC handler: send a valid `asset.bake_normals` request; verify `bake_complete` event is emitted and output file exists
- [ ] 6.7 Test IPC handler with non-existent high-poly asset path: verify `bake_failed` event is emitted, no crash
- [ ] 6.8 Test progress events: verify at least one `bake_progress` IPC event is emitted during a bake
- [ ] 6.9 Achieve >= 95% line and branch coverage across `normal_map_baker.cpp` and the IPC handler code path

## 7. Documentation

- [ ] 7.1 Update `docs/specs/asset-pipeline.md` documenting `asset.bake_normals` IPC parameters, output format, and cage mesh usage
- [ ] 7.2 Update `docs/specs/renderer.md` noting that baked normal maps from this pipeline use MikkTSpace tangents and should be sampled with matching tangent-space setup in the PBR shader
- [ ] 7.3 Update `docs/specs/shader-system.md` documenting the `normal_map_bake.hlsl` compute shader inputs and outputs
