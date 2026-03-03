## 1. Offline Baking Compute Shaders

- [ ] 1.1 Write `src/shaders/ibl_equirect_to_cubemap.hlsl`: a compute shader with 8x8 threadgroup dispatch that converts an equirectangular HDR image (loaded as `Texture2D<float4>`) into a 512x512 cubemap by computing spherical UV coordinates from each cube face texel direction and sampling the equirectangular input with `SampleLevel`
- [ ] 1.2 Write `src/shaders/ibl_irradiance_convolution.hlsl`: a compute shader that generates a 64x64 irradiance cubemap by integrating the cosine-weighted hemisphere over the input environment cubemap using a Riemann sum with 512 sample directions per texel; output to `RWTexture2DArray<float4>` with 6 array slices
- [ ] 1.3 Write `src/shaders/ibl_prefilter_env.hlsl`: a compute shader that generates a 128x128 prefiltered environment map with 7 roughness mip levels using GGX importance sampling (128 samples per texel per mip); read roughness from a root constant and dispatch once per mip level; store results in `RWTexture2DArray<float4>` at the correct mip slice
- [ ] 1.4 Write `src/shaders/ibl_brdf_integration.hlsl`: a compute shader dispatched once for the entire 512x512 RG16F BRDF LUT; each texel at `(NdotV, roughness)` accumulates the split-sum GGX BRDF integral using 1024 Hammersley sequence samples; store to `RWTexture2D<float2>`

## 2. IBL Baker Pipeline

- [ ] 2.1 Create `src/engine/assets/ibl_baker.h` and `src/engine/assets/ibl_baker.cpp` implementing `IblBaker::Bake(cmd_list, equirect_srv)` that orchestrates the four compute shaders in order: equirect-to-cubemap, irradiance convolution, prefiltered env (7 dispatches for mip levels 0-6), and BRDF integration; return `IblBakeResult` containing the four output texture handles
- [ ] 2.2 Create the BRDF LUT as a persistent engine asset baked once at startup (not per-scene) and reused across all environments; store the baked LUT in the `BindlessManager` and include its heap index in `SceneCBV`
- [ ] 2.3 Implement `IblBaker::ExportToUzenv(result, output_path)` that serializes the baked irradiance cubemap and prefiltered env map to a `.uzenv` file using DDS BC6H compression via `DirectXTex::Compress`

## 3. .uzenv Asset Format

- [ ] 3.1 Define the `.uzenv` binary format in `docs/specs/rendering/ibl-pipeline.md`: a 16-byte header (`uint32_t magic = "UZEV"`, `uint32_t version = 1`, `uint32_t irradiance_offset`, `uint32_t prefilter_offset`), followed by the DDS BC6H irradiance cubemap, followed by the DDS BC6H prefiltered env cubemap
- [ ] 3.2 Implement `IblLoader::Load(path)` in `src/engine/assets/ibl_loader.cpp` that reads the `.uzenv` header, validates the magic number, and uploads the two cubemaps to GPU as `DXGI_FORMAT_BC6H_UF16` via `CopyTextureRegion`
- [ ] 3.3 Register both the irradiance and prefiltered env cubemaps in `BindlessManager` and store their heap indices in the scene's `IblBindings` struct used during PBR lighting

## 4. PBR Shader Integration

- [ ] 4.1 Implement `ComputeIBL(N, V, roughness, metallic, base_color)` function in `src/shaders/pbr_lighting.hlsli` using the split-sum approximation: `irradiance * diffuse_color + prefiltered_env * (F * brdf.x + brdf.y)` where `F` is the Fresnel-Schlick approximation at grazing angle
- [ ] 4.2 Sample the irradiance cubemap using the surface normal `N` with `SampleLevel(sampler, N, 0)`, sample the prefiltered env with `reflect(-V, N)` at mip level `roughness * 6`, and sample the BRDF LUT at `(NdotV, roughness)`
- [ ] 4.3 Add `ibl_irradiance_index`, `ibl_prefilter_index`, and `ibl_brdf_lut_index` as `uint32_t` fields in `SceneCBV` so all PBR draw calls access the same IBL textures without per-material rebinding

## 5. Scene Configuration

- [ ] 5.1 Add `[environment]` section parsing in `src/engine/scene/scene_loader.cpp` reading `ibl = "environments/studio.uzenv"` (project-relative path); call `IblLoader::Load()` and register the result with the current `IblBindings`
- [ ] 5.2 Implement scene unload cleanup in `SceneLoader::Unload()` that calls `BindlessManager::Unregister()` for the IBL heap indices and releases the GPU texture memory
- [ ] 5.3 Add `scene.set_ibl` IPC handler accepting `{"path": "environments/studio.uzenv"}` that hot-swaps the active IBL environment without scene reload; update `IblBindings` and the `SceneCBV` for the next frame
- [ ] 5.4 Add `scene.get_ibl_info` IPC handler returning `{"path": "...", "irradiance_size": 64, "prefilter_mips": 7, "brdf_lut_size": 512, "loaded": true}`

## 6. Testing

- [ ] 6.1 Create `tests/renderer/test_ibl_baker.cpp` using a mock command list recorder that verifies `IblBaker::Bake()` dispatches the four compute shaders in the correct order with valid UAV/SRV bindings
- [ ] 6.2 Test irradiance convolution correctness: provide a uniform white environment (all texels `(1,1,1)`) and verify the output irradiance cubemap is also `(1/pi, 1/pi, 1/pi)` (cosine-weighted integral of a constant function) within `1e-3` tolerance
- [ ] 6.3 Test BRDF LUT values: verify `brdf_lut(NdotV=1.0, roughness=0.0)` returns `(1.0, 0.0)` (perfect mirror at normal incidence) and `brdf_lut(NdotV=0.0, roughness=1.0)` is within a physically plausible range `[0, 0.1]`
- [ ] 6.4 Create `tests/assets/test_ibl_loader.cpp` testing `IblLoader::Load()` with a hand-crafted valid `.uzenv` file (minimal 1x1 cubemaps with magic header) verifies the magic number and returns non-null texture handles
- [ ] 6.5 Test `IblLoader::Load()` with a corrupted magic number returns an error without crashing
- [ ] 6.6 Test `scene.set_ibl` IPC handler on a valid path returns `ok: true` and the `IblBindings` heap indices are updated to non-zero values
- [ ] 6.7 Test `scene.set_ibl` IPC handler on a missing file returns `ok: false` with a file-not-found error and the previous IBL bindings are unchanged
- [ ] 6.8 Test `scene.get_ibl_info` IPC handler after a successful `scene.set_ibl` returns the expected `irradiance_size: 64` and `prefilter_mips: 7` values
- [ ] 6.9 Test `ComputeIBL` on a null material (metallic=0, roughness=1, base_color=(0.5, 0.5, 0.5)) returns a non-zero diffuse contribution when irradiance is non-zero
- [ ] 6.10 Test scene IBL cleanup on unload: verify `BindlessManager::Unregister()` is called for both irradiance and prefilter indices after `SceneLoader::Unload()`
- [ ] 6.11 Run coverage -- all new files in `src/engine/assets/`, `src/engine/renderer/`, and `src/engine/scene/` must reach >= 95% line and branch coverage before marking complete
